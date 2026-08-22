# Minefolio — Agent Instructions

## Project at a Glance

Personal finance tracker. SQLite or PostgreSQL backend (C23 + [csilk v0.5.0](https://github.com/quintin-lee/csilk)) + Vue 3 / TypeScript frontend.

```
backend/src/          Three-tier: controllers/ services/ repositories/ common/
backend/sql/          migration.sql (SQLite) + migration_postgres.sql
backend/tests/        test_link.sh — full HTTP & DB integration test suite
frontend/src/         Vue 3 + Pinia + Element Plus + ECharts
```

### Layered Architecture

```
HTTP Layer    controllers/   — Parse request params, call service, format response
Business Layer services/     — Orchestrate repo calls, balance ops, transactions
Data Layer    repositories/  — All SQL queries, return csilk_json_t*
Common        common/        — db.h/.c  jwt.h/.c  balance.h/.c  response.h  ctx.h  csv_utils.h/.c
Config        config/        — db_config.h/.c  key_manager.h/.c
DTOs          dtos/          — request.h  response.h (reflection macros)
Models        models/        — asset.h  category.h  transaction.h  tag.h  etc.
```

**Dependency rules:**
- `controllers` → `services`, `dtos/`
- `services` → `repositories`, `common/`, `balance`
- `repositories` → `common/` ONLY (zero HTTP/framework knowledge)
- `main.c` includes only `controllers/*_controller.h`

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
cmake --build backend/build --parallel && npm --prefix frontend run build
```

---

## Architecture Rules & Code Quality Standards

### 1. Repository Pattern (STRICT)
All repositories follow this interface:
```c
// repo.h
#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

/* List with pagination — returns csilk_json_t array, sets *total */
csilk_json_t* entity_list(csilk_db_pool_t* pool, int64_t user_id,
                           int64_t page, int64_t page_size, ...filter params..., int64_t* total);

/* Get single entity — returns csilk_json_t (NULL if not found) */
csilk_json_t* entity_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/* Insert — returns new ID or 0 on failure */
int64_t entity_insert(csilk_db_pool_t* pool, int64_t user_id, ...params...);

/* Update — returns 1 if found/updated, 0 otherwise */
int entity_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, ...params...);

/* Delete — returns 1 if deleted, 0 otherwise */
int entity_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
```

**Key conventions:**
- Repo functions take `(pool, user_id, ...)` — never extract `user_id` from ctx
- Return `csilk_json_t*` directly (no model struct conversion layer)
- ALL SQL must use `?` placeholders with `csilk_db_query_param_json()`
- **Never** put raw SQL in service files — all queries belong in repositories

### 2. Service Pattern
Services orchestrate business logic:
- Extract params from `csilk_ctx_t*` using `ctx_user_id(c)`, `csilk_get_param()`, `csilk_bind_json()`
- Call repositories for all data access
- Handle `balance_apply_delta()` / `apply_position()` for investment/balance ops
- Build response `csilk_json_t*` and call `respond_ok()` / `respond_error()`
- Keep `BEGIN/COMMIT/ROLLBACK` transaction blocks in services

### 3. Controller Pattern
Controllers are thin HTTP handlers:
```c
// controller.c
#include "controllers/<domain>_controller.h"
#include "services/<domain>_service.h"

void <action>(csilk_ctx_t* c) {
    <domain>_service_<action>(c);  // delegate to service
}

void register_<domain>_routes(csilk_app_t* app) {
    csilk_app_get_ext(app, "/api/<domain>", <action>, ...);
    // ... more routes
}
```
- Include service header, not repository header
- Register routes via `register_*_routes(app)` in controller
- `main.c` includes controller headers only

### 4. Database Helpers & Request Body Parsing (STRICT)
- **SQL Parameterization**: ALL database queries **MUST** use `?` placeholders with `csilk_db_query_param_json(pool, sql, params)` for parameterized queries. **NEVER** interpolate user input into SQL.
- **Exception — self-contained literals**: when constructing a SQL string from known-safe C strings (e.g., building an INSERT with only locally formatted numbers), `csilk_db_exec(pool, sql)` is acceptable. See `transaction_write.c` fee-row creation for the pattern.
- **Number Parsing**: csilk JSON parser returns DB columns as strings, and HTTP clients may submit numbers as either strings or numeric primitives in JSON bodies.
  - **ALWAYS** use `db_get_num(obj, "key")` and `db_get_int(obj, "key")` from `db.h`.
  - **NEVER** call `csilk_json_get_number()` directly on DB query results or request JSON bodies, as it returns `0.0` for string nodes.

### 5. Backend C Response Envelope & Transaction Safety
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

### 6. Category System & Debt Asset Direction
- **Category Types**: Categories are strictly segregated into four types: `asset`, `income`, `expense`, and `transaction`.
- **Category Caching**: `useCategoryStore` caches category trees; call `invalidate()` after any category mutation.
- **Debt Asset Direction**: `balance_apply_delta()` in `balance.c` flips the sign of `delta` for `loan`, `credit_card`, and `other_liability` categories so net-worth calculations stay correct. Do not modify this logic without understanding liability accounting.

### 7. Investment Asset Transactions (buy/sell stock/fund/bond/crypto)
Investment assets (`asset_type` in `stock`, `fund`, `bond`, `crypto`) have special handling:
- **Position tracking**: `apply_position()` updates `quantity`, `cost_basis`, and `net_value` on the asset row. `cost_basis` includes the purchase amount **plus** fee.
- **Balance linkage**: The linked funding account (e.g., wallet) is debited via `balance_apply_delta`. The fund asset's `current_value` is updated via `balance_apply_delta` using `position_delta` (computed as `new_qty * new_net - old_current`).
- **Fee row**: When `fee > 0`, a secondary `transaction_type='fee'` row is inserted via raw SQL (`csilk_db_exec`). The fee is deducted from the linked account. **The fee row's `note` must be non-empty** — fall back to the literal string `"fee"` when the original note is empty, otherwise test queries like `note LIKE '%fee%'` will miss it.
- **PnL calculation** (report services):
  - `total_cost_for_pnl` tracks buy amounts **excluding** fees (used as numerator for `avg_cost`).
  - `total_cost_basis` tracks buy amounts **including** fees (matches the database column for display).
  - On sell, `realized_pnl += amount - qty * avg_cost` where `avg_cost = total_cost_for_pnl / total_quantity`.
  - `floating_pnl = total_market_value - total_cost_basis_remaining` where values come from `SUM(current_value)` and `SUM(cost_basis)` on investment assets.
  - **Only rows with `qty > 0`** affect position counters (buy/sell); `fee` rows and other types are skipped.

### 8. Net Value Update (Asset PUT)
When updating an investment asset's `net_value` via `PUT /api/assets/:id`:
- The `net_value` field **must** be included in the `UPDATE` statement. A common bug is declaring `char nv_str[64]` **after** `upd_params[]` — the compiler may reuse stack space and produce wrong values. Always declare `nv_str` **before** building the params array.

### 9. Frontend UI & API Consistency Standard (STRICT)
- **API Layer**: **NEVER** call raw `fetch()` or `axios` in Vue components. Every API call **MUST** be placed in `frontend/src/api/<domain>.ts` using the central `http` client (`frontend/src/utils/http.ts`), which handles JWT tokens, CSRF headers, and error unwrapping automatically.
  - **Exception**: `axios.get()` with `responseType: 'blob'` is used for file downloads (e.g., CSV export) because `http.get()` does not support blob response types. Use `http.get()` for all other calls.
- **Page Layout**: `.page-header` uses `justify-content: space-between` — title left, button group right. Wrap action buttons in `<div class="header-actions">` with `gap: 8px`.
- **Scroll Layout**: `.main` in `Layout.vue` is the single scroll container (`height: calc(100vh - 72px); overflow-y: auto`). Page containers use `height: 100%; overflow: hidden` so only data areas scroll internally.
- **Error Resilience**: Every `onMounted` hook **MUST** wrap async initialization in `try/catch` to prevent a single failed API call from breaking the entire component tree. Use `v-loading` directive for loading states. For parallel API calls, prefer `Promise.allSettled` over `Promise.all` so partial failures don't crash the entire page. `Layout.vue` uses `<suspense>` as a fallback.

---

## Conventions

- **C Code Structure**: Repository functions named `<entity>_<action>` (e.g., `tx_list`, `tx_insert`). Service functions keep existing names (`transactions_list`, `transactions_create`). Controller functions delegate to service. Header files use `#pragma once`.
- **Frontend API**: One API file per domain in `frontend/src/api/`, export named functions. TypeScript interfaces live in `frontend/src/types/index.ts`.
- **Auto-registered Components**: Element Plus components are auto-registered via `unplugin-vue-components`.
- **Transaction Type Registry**: `backend/src/common/tx_types.c` defines all transaction types with balance direction, linked direction, and PnL semantics. Use `tx_type_lookup()` instead of hard-coding type checks.
- **Build**: Use `cmake -B build -G "Unix Makefiles"` (NOT Ninja — Ninja has stale-dependency bugs with csilk).

---

## Security

- Passwords: RSA-OAEP (SHA-256) encryption for transport; bcrypt (csilk `CSILK_BCRYPT_DEFAULT_COST=12`) for storage.
- CSRF: Controlled by `MINEFOLIO_ENABLE_CSRF`.
- Multi-tenancy: Single `users` table, each query filters by `user_id` extracted from JWT.
- JWT: `MINEFOLIO_JWT_SECRET` env var required in production; fails hard if unset.
- OpenAPI: All routes registered via `csilk_app_get_ext` / `csilk_app_post_ext` with metadata.

---

## Key Files Reference

| Task | Start Here |
|------|-----------|
| Add new domain (full stack) | `backend/src/repositories/`, `backend/src/services/`, `backend/src/controllers/`, `backend/src/main.c` |
| Add repository function | `backend/src/repositories/<domain>_repo.h/.c` |
| Modify service logic | `backend/src/services/<domain>_service.c` or `<domain>_query.c` / `<domain>_write.c` |
| Modify controller routes | `backend/src/controllers/<domain>_controller.c` |
| DB & JSON Helpers | `backend/src/common/db.h` |
| Balance Logic | `backend/src/common/balance.h` + `balance.c` |
| Transaction type registry | `backend/src/common/tx_types.h` + `tx_types.c` |
| Config persistence | `backend/src/common/config.h` + `config.c` |
| CSV utilities | `backend/src/common/csv_utils.h` + `csv_utils.c` |
| Context helpers | `backend/src/common/ctx.h` |
| Response helpers | `backend/src/common/response.h` |
| Frontend Page Reference | `frontend/src/views/DailyExpenses.vue` / `Transactions.vue` |
| Auth Flow | `backend/src/services/auth_service.c` + `frontend/src/stores/auth.ts` |
| Docker Deployment | `Dockerfile` |

---

## Known Gotchas

1. **Curl `-d` strips newlines**: curl 8.x removes `\n` from `curl -d @file`. For CSV imports in tests, use `curl --data-binary @file` or write CSV content via heredoc (`cat > file << 'EOF'`).
2. **Ninja stale dependencies**: The backend build may produce stale binaries with Ninja. Always use Makefiles (`-G "Unix Makefiles"`) or run `rm -rf build && cmake -B build` to force a clean rebuild.
3. **`components.d.ts` is auto-generated**: After adding any new Element Plus component usage, run `npm run build` once to regenerate `frontend/src/components.d.ts`. Don't commit changes to it manually.
4. **Investment sell cost_basis**: `apply_position()` reduces `cost_basis` proportionally on sell. The performance report uses a separate `total_cost_for_pnl` (excludes fee) for `avg_cost` computation — do not mix the two.
5. **Fee row note must be non-empty**: Test queries use `note LIKE '%fee%'`. Always ensure the fee row's `note` contains a non-empty string.
6. **Mobile wasm loading**: Capacitor WebView cannot reliably `fetch` local assets via `capacitor://` scheme. sql.js wasm MUST be embedded as base64 in the JS bundle (`src/db/generated/sql-wasm-base64.ts`). Never revert to `locateFile` + network fetch for mobile builds.
7. **Gradle incremental builds**: After `npx cap sync android`, always run `./gradlew clean assembleDebug` to avoid stale cached assets.
8. **Password length validation**: All password entry points (register, setup, change_password) require ≥6 characters. Do not reduce the threshold.
9. **Investment transaction rollback**: When updating a transaction from investment to non-investment type (or vice versa), the old position MUST be rolled back AND its balance delta reversed before applying the new transaction's balance change. Never skip the old balance reversal.
10. **JWT secret in production**: `MINEFOLIO_JWT_SECRET` must be set in production. The server fails hard if it's missing.
