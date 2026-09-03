# Repository Guidelines

## Project Overview

Minefolio is a self-hosted personal finance and investment tracker. It supports multiple account types (cash, bank, credit card, loan), tracks investment positions (stocks, funds, bonds, crypto) with full cost-basis and PnL reporting, and offers AI-powered chat. The backend is C23 using the csilk HTTP framework with SQLite or PostgreSQL; the frontend is Vue 3 + TypeScript served as a SPA. A Capacitor-based mobile build uses sql.js WASM for fully offline operation.

## Architecture & Data Flow

### Backend — Three-Tier C Architecture

```
HTTP Layer    controllers/     Parse params, call service, format response
Business Layer services/       Orchestrate repos, balance ops, transactions
              services/ai/     Enterprise AI: runtime, model, workflow, tools, policy, trace
Core Layer    core/financial/  Fixed-point core: money, decimal, quantity, price, rate, pnl
              core/ledger/     Ledger engine: transaction replay, rebuild, balance/cost basis
Data Layer    repositories/    Raw SQL, return csilk_json_t*
Shared        common/          db, jwt, balance, response, ctx, csv, tx_types
              config/          db_config, key_manager (RSA keys), secret (Secret Provider)
              dtos/            request/response struct definitions (reflection macros)
              models/          C domain structs
              middlewares/     jwt, cors, csrf, security-headers, rate-limit
```

**Dependency direction is strict and one-way:**
- `main.c` → includes only `controllers/*_controller.h`
- `controllers/` → `services/`, `dtos/`
- `services/` → `repositories/`, `common/`, balance logic
- `repositories/` → `common/db.h` ONLY (no HTTP/framework knowledge)

Complex domains split read/query from write: `transaction_query.c` / `transaction_write.c`, `daily_expense_query.c` / `daily_expense_write.c`.

### Frontend — Vue 3 SPA

```
Entry       main.ts (desktop) / main-mobile.ts (mobile)
Router      router/index.ts / router/mobile.ts
State       Pinia stores (auth, category, chat, sync)
API         src/api/*.ts — wraps http.ts (axios with JWT + CSRF)
Views       src/views/ (desktop) / src/views-mobile/ (mobile)
Components  Auto-registered via unplugin-vue-components (Element Plus)
Types       src/types/index.ts
Charts      ECharts (src/components/*.vue)
Offline DB  src/db/ — sql.js WASM with local SQLite for mobile
```

**Desktop and mobile share the same API layer** (`src/api/`) and TypeScript types. Mobile has its own view set and a lightweight layout. The mobile build hardcodes `VITE_API_URL` at compile time.

### Data Flow: Transaction Write Path

Vue component → api/transactions.ts → http.ts (JWT + CSRF)
  → POST /api/transactions
  → transaction_controller → transaction_write_service
    → transaction_repo INSERT (parent row)
    → apply_position() updates asset quantity/cost_basis/net_value
    → balance_apply_delta() debits linked funding account
    → if fee > 0: raw SQL inserts fee row with parent_tx_id linking to parent
  → respond_ok() returns {code:0, data:{id,...}}
```

### Transaction Delete — Fee Child Rollback

When a transaction is deleted, the service MUST:
1. Query all fee child rows via `tx_child_fee_rows(pool, user_id, parent_tx_id)`
2. Reverse each fee row's balance delta via `balance_apply_delta()`
3. Delete the fee child rows via `tx_delete_fee_children()`
4. Then delete the parent transaction

This prevents orphaned fee rows from leaving incorrect balance state.

### Schema Note: parent_tx_id Column

The `transactions` table has an optional `parent_tx_id` column (SQLite: INTEGER, Postgres: BIGINT) with `ON DELETE CASCADE`. Fee rows are inserted as children of their parent transaction, enabling the rollback logic above. Migration is applied at runtime via `db.c` if the column is absent.

Balance direction is handled centrally: `balance_apply_delta()` flips the sign for liability assets (`loan`, `credit_card`, `other_liability`) so net-worth calculations stay correct. Transaction types are registered in `common/tx_types.c` — always use `tx_type_lookup()` instead of hard-coding type checks.

## Key Directories

| Path | Purpose |
|------|---------|
| `backend/src/main.c` | Entry point: DB init, migrations, middleware stack, route registration, static serve |
| `backend/src/controllers/` | Thin HTTP handlers; one per domain; `register_*_routes(app)` |
| `backend/src/services/` | Business logic; query and write files coexist per domain |
| `backend/src/services/ai/` | Decoupled AI architecture: runtime, model, workflow, tools, policy, trace |
| `backend/src/core/financial/` | Fixed-point core arithmetic: money, decimal, quantity, price, rate, pnl |
| `backend/src/core/ledger/` | Ledger engine: single source of truth, position calculation, history replay/rebuild |
| `backend/src/repositories/` | All SQL; return `csilk_json_t*`; never touch HTTP |
| `backend/src/common/` | Cross-cutting: `db.h`, `balance.h`, `jwt.h`, `response.h`, `ctx.h`, `tx_types.h` |
| `backend/src/config/` | `db_config.h/.c` (DSN), `key_manager.h/.c` (RSA-OAEP keys), `secret.h/.c` (Secret Provider) |
| `backend/sql/` | `migration.sql` (SQLite), `migration_postgres.sql` |
| `backend/tests/unit/` | 13 CTest unit test suites (financial core, ledger, AI tools/policy, secrets) |
| `backend/tests/test_link.sh` | 38-case integration test suite (139 assertions, HTTP + sqlite3 verification) |
| `frontend/src/main.ts` | Desktop entry: Pinia, router, Element Plus, i18n |
| `frontend/src/main-mobile.ts` | Mobile entry: separate router, sql.js init |
| `frontend/src/api/` | One file per domain; never call fetch/axios directly in components |
| `frontend/src/stores/` | Pinia stores (auth, category, chat, sync) |
| `frontend/src/views/` | Desktop pages |
| `frontend/src/views-mobile/` | Mobile pages |
| `frontend/src/db/` | sql.js wrapper + schema for offline mobile SQLite |
| `scripts/` | `build.sh` (production), `dev.sh` (local dev with both servers) |

## Development Commands

### Backend

```bash
cd backend
cmake -B build -G "Unix Makefiles"   # MUST use Makefiles — Ninja has stale-dependency bugs
cmake --build build --parallel
./build/minefolio                    # reads config/db.json relative to cwd
./tests/test_link.sh                 # full integration test suite
```

**DB driver selection** (env vars override `config/db.json`):
```bash
MINEFOLIO_DB_DRIVER=sqlite  MINEFOLIO_DB_DSN=./data/minefolio.db   ./build/minefolio
MINEFOLIO_DB_DRIVER=postgres MINEFOLIO_DB_DSN="host=… user=… dbname=…"  ./build/minefolio
```

### Frontend

```bash
cd frontend
npm install
npm run dev                # :5173, proxies /api → localhost:8080
npm run build              # vue-tsc -b && vite build — must compile with 0 errors
npm test                   # vitest (mobile config, jsdom environment)
```

### Mobile

```bash
npm run build:mobile       # vite build --config vite.config.mobile.ts --mode mobile
npx cap sync android       # copy dist-mobile → android/app/src/main/assets/public
cd android && ./gradlew clean assembleDebug
```

### Full Verification

```bash
cmake --build backend/build --parallel && npm --prefix frontend run build
```

### Scripts

```bash
./scripts/dev.sh   # builds backend, starts server on :8080, starts frontend on :5173, traps signals
./scripts/build.sh # Release backend build then frontend build
```

## Code Conventions & Common Patterns

### C — Repository Pattern (STRICT)

```c
// repositories/<entity>_repo.h
#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

csilk_json_t* entity_list(csilk_db_pool_t* pool, int64_t user_id,
                           int64_t page, int64_t page_size, ...filters..., int64_t* total);
csilk_json_t* entity_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int64_t entity_insert(csilk_db_pool_t* pool, int64_t user_id, ...params...);
int entity_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, ...params...);
int entity_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
// Fee-child helpers (transaction domain):
csilk_json_t* tx_child_fee_rows(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_tx_id);
int           tx_delete_fee_children(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_tx_id);
```
- Return `csilk_json_t*` directly; no model-struct conversion layer
- ALL SQL uses `?` placeholders with `csilk_db_query_param_json(pool, sql, params)`
- Raw SQL via `csilk_db_exec` is only acceptable for self-contained literals (e.g. fee-row insertion)

### C — Service Pattern

- Extract params with `ctx_user_id(c)`, `csilk_get_param()`, `csilk_bind_json()`
- Call repositories; handle `balance_apply_delta()` / `apply_position()` for balance ops
- Build `csilk_json_t*` response; call `respond_ok()` / `respond_error()`
- Keep `BEGIN/COMMIT/ROLLBACK` blocks inside services

### C — Controller Pattern

```c
#include "controllers/<domain>_controller.h"
#include "services/<domain>_service.h"

void <action>(csilk_ctx_t* c) {
    <domain>_service_<action>(c);
}

void register_<domain>_routes(csilk_app_t* app) {
    csilk_app_get_ext(app, "/api/<domain>", <action>, ...);
}
```

- Include service header, not repository header
- `main.c` includes only controller headers

### C — Database & Number Parsing (STRICT)

- **NEVER** interpolate user input into SQL strings
- **ALWAYS** use `db_get_num(obj, "key")` and `db_get_int(obj, "key")` from `db.h`
- **NEVER** call `csilk_json_get_number()` directly — it returns `0.0` for string nodes

### C — Response Envelope

All responses are HTTP 200 with JSON `{code, message, data}`:

| code | meaning |
|------|---------|
| 0 | OK |
| 1001 | Unauthorized (JWT missing/expired) |
| 1002 | Bad request / Validation failed |
| 1003 | Not found |
| 1004 | Conflict / Forbidden |

Use `respond_ok` / `respond_error` / `respond_bad_request` / `respond_unauthorized` from `common/response.h`. On multi-step balance updates with `BEGIN TRANSACTION`, always `ROLLBACK` on failure before returning an error.

### C — Net Value PUT Gotcha

When updating an investment asset's `net_value` via `PUT /api/assets/:id`, declare `char nv_str[64]` **before** the `upd_params[]` array on the stack. Declaring it after can cause the compiler to reuse stack space and produce wrong values.

### Frontend — API Layer (STRICT)

- **NEVER** call raw `fetch()` or `axios` in Vue components
- Every API call lives in `frontend/src/api/<domain>.ts` using the central `http` client (`frontend/src/utils/http.ts`)
- **Exception**: `axios.get()` with `responseType: 'blob'` for CSV downloads (`http.get()` doesn't support blobs)

### Frontend — Page Layout

- `.page-header` uses `justify-content: space-between`; action buttons wrapped in `<div class="header-actions">` with `gap: 8px`
- `.main` in `Layout.vue` is the single scroll container (`height: calc(100vh - 72px); overflow-y: auto`)
- Page containers use `height: 100%; overflow: hidden` so only data areas scroll

### Frontend — Error Resilience

Every `onMounted` hook **MUST** wrap async initialization in `try/catch`. Use `v-loading` for loading states. Prefer `Promise.allSettled` over `Promise.all` for parallel API calls so partial failures don't crash the page.

### Frontend — Category Cache

`useCategoryStore` caches category trees. Call `invalidate()` after any category mutation (create, update, delete).

### Naming

- C repo functions: `<entity>_<action>` (e.g., `tx_list`, `tx_insert`)
- C service functions: keep existing names (e.g., `transactions_list`, `transactions_create`)
- C headers: `#pragma once`
- Frontend API files: one per domain, named after the REST path segment
- TypeScript interfaces: PascalCase in `frontend/src/types/index.ts`

## Important Files

| File | Role |
|------|------|
| `backend/src/main.c` | Entry point; middleware stack; route registration |
| `backend/CMakeLists.txt` | C23 standard; FetchContent for csilk v0.5.2; optional PostgreSQL |
| `backend/src/common/balance.h` | `balance_apply_delta()` (incl. liability sign flip); `apply_position()` |
| `backend/src/common/tx_types.h` | Transaction type registry — use `tx_type_lookup()`, don't hard-code |
| `backend/src/common/response.h` | `respond_ok`, `respond_error`, etc. |
| `backend/src/common/ctx.h` | `ctx_user_id(c)` — extract user from JWT |
| `backend/src/common/jwt.h` | JWT generate/verify with HS256 |
| `backend/sql/migration.sql` | SQLite schema (13 tables + parent_tx_id column) |
| `backend/sql/migration_postgres.sql` | PostgreSQL schema (BIGSERIAL, DOUBLE PRECISION) |
| `frontend/package.json` | Scripts, dependencies, vitest config |
| `frontend/vite.config.ts` | Desktop: port 5173, API proxy to :8080 |
| `frontend/vite.config.mobile.ts` | Mobile: port 5174, output to `dist-mobile/` |
| `frontend/src/utils/http.ts` | Central axios wrapper: JWT bearer, CSRF token, response unwrap |
| `frontend/src/stores/auth.ts` | Auth state: token, user, login/logout |
| `frontend/src/stores/category.ts` | Category tree cache with `invalidate()` |
| `Dockerfile` | Multi-stage: backend-build → frontend-build → nginx runtime |
| `docker-compose.yml` | Two-service compose: minefolio + nginx proxy on :80 |
| `scripts/dev.sh` | Local dev: build backend, start server :8080, start frontend :5173 |
| `scripts/build.sh` | Production build: cmake Release + npm run build |

## Runtime / Tooling Preferences

| Layer | Runtime | Package Manager | Notes |
|-------|---------|-----------------|-------|
| Backend | Linux (gcc-14) | CMake 3.16+ | C23 standard; GNU extensions |
| Frontend | Node.js 20 | npm | vue-tsc strict mode required |
| Mobile | Android (Gradle) | npm + Gradle | Capacitor 6; full Android SDK needed |
| Docker | Ubuntu 24.04 / nginx:alpine | apt / npm | Multi-stage; local `deps/` cache for offline csilk fetch |

- **Build system**: CMake with **Makefiles** — never Ninja (known stale-dependency bugs with csilk)
- **Frontend type-checking**: `vue-tsc -b` runs before `vite build`; both must pass with zero errors
- **Mobile wasm**: sql.js wasm is embedded as base64 in `frontend/src/db/generated/sql-wasm-base64.ts`. Never revert to `locateFile` + network fetch for mobile builds. Regenerate with:
  ```bash
  base64 -w0 node_modules/sql.js/dist/sql-wasm.wasm > /tmp/wasm_b64.txt
  ```
- **JWT secret**: `MINEFOLIO_JWT_SECRET` env var is **required** in production; server fails hard if unset

## Testing & QA

### Backend Unit & Integration Tests (13 CTest Suites + 7 Integration Suites)

```bash
cd backend
# 1. Run all 13 CTest unit test suites (sub-second fast feedback)
cd build && ctest --output-on-failure && cd ..

# 2. Run all 7 end-to-end integration test suites
./tests/test_link.sh          # 38 cases (139 assertions): auth, CRUD, balance联动, PnL, CSV, pagination, ledger rebuild
./tests/test_ledgers.sh       # 16 cases: multi-ledger spaces, member RBAC (Owner/Editor/Viewer)
./tests/test_2fa.sh           # 12 cases: TOTP 2FA secret gen, QR code, enable/disable, login verification
./tests/test_dca_cashflow.sh  # 18 cases: DCA periodic purchase execution, cashflow calendar projection
./tests/test_ai_trace.sh      # 17 cases: AI conversation traces, token usage, tool spans
./tests/test_market_sync.sh   # 18 cases: Multi-source market quotes, caching, bulk sync
./tests/test_fx_oauth.sh      # 20 cases: Exchange rates, FX gain/loss PnL, receipt OCR, OAuth2/OIDC SSO
```

Tests start real servers with temp SQLite DBs, exercise all API endpoints via curl/JSON, and verify database state directly with `sqlite3`. Run before committing any changes.

### Frontend Tests

```bash
cd frontend
npm test    # vitest run --config vite.config.mobile.ts (jsdom environment)
npm run build         # Desktop type-check + build
npm run build:mobile  # Mobile type-check + build
```

Frontend unit tests are currently limited; the integration test suite (`test_link.sh`) covers the critical paths end-to-end.

### Manual Smoke Test

```bash
# Start both servers
./scripts/dev.sh
# Open http://localhost:5173, create account, add an expense, verify balance updates
```

### Known Gotchas

1. **Curl `-d` strips newlines**: Use `curl --data-binary @file` or heredoc (`cat > file << 'EOF'`) for CSV content in tests.
2. **Ninja stale dependencies**: Always use `-G "Unix Makefiles"` or run `rm -rf build && cmake -B build`.
3. **`components.d.ts` is auto-generated**: Run `npm run build` after adding new Element Plus component usages; do not commit manual changes.
4. **Investment sell cost_basis**: `apply_position()` reduces `cost_basis` proportionally. The PnL report uses a separate `total_cost_for_pnl` (excludes fee) for `avg_cost` — do not mix the two.
5. **Fee row `note` must be non-empty**: Test queries use `note LIKE '%fee%'`. Fall back to literal `"fee"` when the original note is empty.
6. **Mobile wasm loading**: Capacitor WebView cannot reliably `fetch` from `capacitor://`. The base64 embedding in `sql-wasm-base64.ts` is mandatory.
7. **Gradle incremental builds**: After `npx cap sync android`, always run `./gradlew clean assembleDebug`.
8. **Password length validation**: All password entry points require ≥6 characters.
9. **Investment transaction rollback**: When changing a transaction from investment ↔ non-investment type, the old position MUST be rolled back and its balance delta reversed before applying the new transaction. Never skip the reversal.
10. **JWT secret in production**: `MINEFOLIO_JWT_SECRET` must be set; the server exits if absent.

## Commit Convention

```
type(scope): 🎯 subject
```

| Type | Emoji |
|------|-------|
| feat | ✨ |
| fix | 🐛 |
| docs | 📝 |
| style | 🎨 |
| refactor | ♻️ |
| test | ✅ |
| build | 📦 |
| ci | 👷 |
| chore | 🧹 |

Emoji goes after the colon, one per commit. Subject is lowercase imperative mood.
