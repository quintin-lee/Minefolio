# Minefolio — Agent Instructions

## Project at a Glance

Personal finance tracker. SQLite or PostgreSQL backend (C23 + [csilk](https://github.com/quintin-lee/csilk)) + Vue 3 / TypeScript frontend.

```
backend/src/          C handlers — one .c per domain (auth, assets, transactions, …)
backend/src/common/   db.h/.c  jwt.h/.c  balance.h/.c  response.h  config.h/.c  tx_types.h/.c
backend/sql/          migration.sql (SQLite) + migration_postgres.sql
backend/tests/        test_link.sh — full HTTP & DB integration test suite (103 PASS)
frontend/src/         Vue 3 + Pinia + Element Plus + ECharts
```

---

## Commands

### Backend (local dev & testing)
```bash
cd backend
cmake -B build -G "Unix Makefiles"   # use Makefiles, NOT Ninja — Ninja has stale-dependency bugs
cmake --build build --parallel
./build/minefolio                    # reads sql/migration.sql relative to cwd
./tests/test_link.sh                 # run full integration test suite
```

**DB driver selection** (env vars checked before `config/db.json`):
```bash
MINEFOLIO_DB_DRIVER=sqlite  MINEFOLIO_DB_DSN=./data/minefolio.db   ./build/minefolio
MINEFOLIO_DB_DRIVER=postgres MINEFOLIO_DB_DSN="host=… user=… dbname=…"  ./build/minefolio
```
The Setup page writes `config/db.json` on first init; the server reads it on every startup via `config_get_str()`.

### Frontend (local dev & build)
```bash
cd frontend
npm install
npm run dev                # :5173, proxies /api → localhost:8080
npm run build              # vue-tsc -b && vite build (must build cleanly with 0 errors)
```

### Mobile (Capacitor) build
```bash
cd frontend
npm run build:mobile       # vite build --config vite.config.mobile.ts --mode mobile
npx cap sync android       # copy dist-mobile → android/app/src/main/assets/public
cd android && ./gradlew assembleDebug   # build APK (requires full Android env)
```

**Mobile API URL**: configure in `frontend/.env.mobile` via `VITE_API_URL`. The APK hardcodes this at build time.

**sql.js WASM embedding**: The Capacitor WebView cannot reliably `fetch` wasm files from `capacitor://` assets. The wasm binary is embedded as base64 in the JS bundle via `frontend/src/db/generated/sql-wasm-base64.ts`. To regenerate:
```bash
base64 -w0 node_modules/sql.js/dist/sql-wasm.wasm > /tmp/wasm_b64.txt
# then use the Python script in commit 218eb88 to produce the TS module
```

### Verification
Before declaring any task complete, run:
```bash
cmake --build backend/build --parallel && npm --prefix frontend run build && bash backend/tests/test_link.sh
```

---

## Architecture Rules & Code Quality Standards

### 1. Database Helpers & Request Body Parsing (STRICT)
- **SQL Parameterization**: ALL database queries **MUST** use `?` placeholders with `csilk_db_query_param_json(pool, sql, params)` for parameterized queries. **NEVER** interpolate user input into SQL.
- **Exception — self-contained literals**: when constructing a SQL string from known-safe C strings (e.g., building an INSERT with only locally formatted numbers), `csilk_db_exec(pool, sql)` is acceptable. See `transactions.c` fee-row creation for the pattern.
- **Number Parsing**: csilk JSON parser returns DB columns as strings, and HTTP clients may submit numbers as either strings or numeric primitives in JSON bodies.
  - **ALWAYS** use `db_get_num(obj, "key")` and `db_get_int(obj, "key")` from `db.h`.
  - **NEVER** call `csilk_json_get_number()` directly on DB query results or request JSON bodies, as it returns `0.0` for string nodes.

### 2. Backend C Response Envelope & Transaction Safety
- Backend always returns HTTP 200. Business errors use `{code, message, data}`:
  | code | meaning |
  |------|---------|
  | 0    | OK |
  | 1001 | Unauthorized (JWT missing/expired) |
  | 1002 | Bad request / Validation failed |
  | 1003 | Not found |
  | 1004 | Conflict / Forbidden |
- Response macros: **MUST** use `respond_ok / respond_error / respond_bad_request / respond_unauthorized` from `common/response.h`.
- Database Transactions: When executing multi-step balance or record updates (`BEGIN TRANSACTION`), always execute `ROLLBACK` on failure before returning an error response.

### 3. Category System & Debt Asset Direction
- **Category Types**: Categories are strictly segregated into four types: `asset`, `income`, `expense`, and `transaction`.
- **Category Caching**: `useCategoryStore` caches category trees; call `invalidate()` after any category mutation.
- **Debt Asset Direction**: `balance_apply_delta()` in `balance.c` flips the sign of `delta` for `loan`, `credit_card`, and `other_liability` categories so net-worth calculations stay correct. Do not modify this logic without understanding liability accounting.

### 4. Investment Asset Transactions (buy/sell stock/fund/bond/crypto)
Investment assets (`asset_type` in `stock`, `fund`, `bond`, `crypto`) have special handling in `transactions.c`:
- **Position tracking**: `apply_position()` updates `quantity`, `cost_basis`, and `net_value` on the asset row. `cost_basis` includes the purchase amount **plus** fee.
- **Balance linkage**: The linked funding account (e.g., wallet) is debited via `balance_apply_delta`. The fund asset's `current_value` is updated via `balance_apply_delta` using `position_delta` (computed as `new_qty * new_net - old_current`).
- **Fee row**: When `fee > 0`, a secondary `transaction_type='fee'` row is inserted via raw SQL (`csilk_db_exec`). The fee is deducted from the linked account. **The fee row's `note` must be non-empty** — fall back to the literal string `"fee"` when the original note is empty, otherwise test queries like `note LIKE '%fee%'` will miss it.
- **PnL calculation** (`reports.c`):
  - `total_cost_for_pnl` tracks buy amounts **excluding** fees (used as numerator for `avg_cost`).
  - `total_cost_basis` tracks buy amounts **including** fees (matches the database column for display).
  - On sell, `realized_pnl += amount - qty * avg_cost` where `avg_cost = total_cost_for_pnl / total_quantity`.
  - `floating_pnl = total_market_value - total_cost_basis_remaining` where values come from `SUM(current_value)` and `SUM(cost_basis)` on investment assets.
  - **Only rows with `qty > 0`** affect position counters (buy/sell); `fee` rows and other types are skipped.

### 5. Net Value Update (Asset PUT)
When updating an investment asset's `net_value` via `PUT /api/assets/:id`:
- The `net_value` field **must** be included in the `UPDATE` statement. A common bug is declaring `char nv_str[64]` **after** `upd_params[]` — the compiler may reuse stack space and produce wrong values. Always declare `nv_str` **before** building the params array.

### 6. Frontend UI & API Consistency Standard (STRICT)
- **API Layer**: **NEVER** call raw `fetch()` or `axios` in Vue components. Every API call **MUST** be placed in `frontend/src/api/<domain>.ts` using the central `http` client (`frontend/src/utils/http.ts`), which handles JWT tokens, CSRF headers, and error unwrapping automatically.
  - **Exception**: `axios.get()` with `responseType: 'blob'` is used for file downloads (e.g., CSV export) because `http.get()` does not support blob response types. Use `http.get()` for all other calls.
- **Page Layout**: `.page-header` uses `justify-content: space-between` — title left, button group right. Wrap action buttons in `<div class="header-actions">` with `gap: 8px`.
- **Scroll Layout**: `.main` in `Layout.vue` is the single scroll container (`height: calc(100vh - 72px); overflow-y: auto`). Page containers use `height: 100%; overflow: hidden` so only data areas scroll internally.
- **Error Resilience**: Every `onMounted` hook **MUST** wrap async initialization in `try/catch` to prevent a single failed API call from breaking the entire component tree. Use `v-loading` directive for loading states. For parallel API calls, prefer `Promise.allSettled` over `Promise.all` so partial failures don't crash the entire page. `Layout.vue` uses `<suspense>` as a fallback.

---

## Conventions

- **C Code Structure**: One handler function per `.c` file, named `<domain>_<action>`. Forward-declare in `main.c`. Header files use `#pragma once`.
- **Frontend API**: One API file per domain in `frontend/src/api/`, export named functions. TypeScript interfaces live in `frontend/src/types/index.ts`.
- **Auto-registered Components**: Element Plus components are auto-registered via `unplugin-vue-components`.
- **Transaction Type Registry**: `backend/src/common/tx_types.c` defines all transaction types with balance direction, linked direction, and PnL semantics. Use `tx_type_lookup()` instead of hard-coding type checks.
- **Verification Rule**: Before declaring any task complete, run `cmake --build backend/build --parallel && npm --prefix frontend run build && bash backend/tests/test_link.sh` to ensure zero build errors and all tests pass.

---

## Security (Known Limitations)

- Passwords: RSA-OAEP (SHA-256) encryption for transport; bcrypt (csilk `CSILK_BCRYPT_DEFAULT_COST=12`) for storage.
- CSRF: Controlled by `MINEFOLIO_ENABLE_CSRF`.
- Multi-tenancy: Single `users` table, each query filters by `user_id` extracted from JWT.

---

## Key Files Reference

| Task | Start Here |
|------|-----------|
| Add API endpoint | `backend/src/main.c` (route registration) + existing `backend/src/*.c` |
| DB & JSON Helpers | `backend/src/common/db.h` |
| Asset & Balance Logic | `backend/src/common/balance.h` + `backend/src/common/balance.c` |
| Transaction type registry | `backend/src/common/tx_types.h` + `tx_types.c` |
| Config persistence | `backend/src/common/config.h` + `config.c` (reads/writes `config/db.json`) |
| Frontend Page Reference | `frontend/src/views/DailyExpenses.vue` / `Transactions.vue` |
| Auth Flow | `backend/src/auth.c` + `frontend/src/stores/auth.ts` + `frontend/src/utils/http.ts` |
| Docker Deployment | `Dockerfile` |

---

## Known Gotchas

1. **Curl `-d` strips newlines**: curl 8.x removes `\n` from `curl -d @file`. For CSV imports in tests, use `curl --data-binary @file` or write CSV content via heredoc (`cat > file << 'EOF'`).
2. **Ninja stale dependencies**: The backend build may produce stale binaries with Ninja when switching between SQLite and PostgreSQL builds. Use Makefiles (`-G "Unix Makefiles"`) or run `rm -rf build && cmake -B build` to force a clean rebuild.
3. **`components.d.ts` is auto-generated**: After adding any new Element Plus component usage, run `npm run build` once to regenerate `frontend/src/components.d.ts`. Don't commit changes to it manually.
4. **Investment sell cost_basis**: `apply_position()` reduces `cost_basis` proportionally on sell. The performance report uses a separate `total_cost_for_pnl` (excludes fee) for `avg_cost` computation — do not mix the two.
5. **Fee row note must be non-empty**: Test queries use `note LIKE '%fee%'`. Always ensure the fee row's `note` contains a non-empty string.
6. **Mobile wasm loading**: Capacitor WebView cannot reliably `fetch` local assets via `capacitor://` scheme. sql.js wasm MUST be embedded as base64 in the JS bundle (`src/db/generated/sql-wasm-base64.ts`). Never revert to `locateFile` + network fetch for mobile builds.
7. **Gradle incremental builds**: After `npx cap sync android`, always run `./gradlew clean assembleDebug` to avoid stale cached assets. `assembleDebug` alone may skip re-packaging if timestamps appear fresh.
