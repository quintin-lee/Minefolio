# Minefolio — Agent Instructions

## Project at a Glance

Personal finance tracker. Single-user SQLite backend (C23 + [csilk](https://github.com/quintin-lee/csilk)) + Vue 3 / TypeScript frontend.

```
backend/src/          C handlers — one .c per domain (auth, assets, transactions, …)
backend/src/common/   db.h/.c  jwt.h/.c  balance.h/.c  response.h
backend/sql/          migration.sql — idempotent, runs on every startup
backend/tests/        test_link.sh — full HTTP & DB integration test suite
frontend/src/         Vue 3 + Pinia + Element Plus + ECharts
docs/superpowers/     Design specs and implementation plans
```

---

## Commands

### Backend (local dev & testing)
```bash
cd backend
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/minefolio          # reads sql/migration.sql relative to cwd
./tests/test_link.sh       # run full backend integration test suite (21 PASS mandatory)
```

### Frontend (local dev & build)
```bash
cd frontend
npm install
npm run dev                # :5173, proxies /api → localhost:8080
npm run build              # vue-tsc -b && vite build (must build cleanly with 0 errors)
```

### Docker
```bash
docker compose up -d --build
```

### First-time setup
```bash
cp .env.example .env       # set MINEFOLIO_JWT_SECRET
```

---

## Architecture Rules & Code Quality Standards

### 1. Database Helpers & Request Body Parsing (STRICT)
- **SQL Parameterization**: ALL database queries **MUST** use `?` placeholders with `csilk_db_query_param_json(pool, sql, params)`. **NEVER** use `snprintf` or string concatenation to construct SQL query strings with parameters.
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
- **Category Types**: Categories are strictly segregated into four types: `asset`, `income`, `expense`, and `transaction` (used specifically for investment/transaction tracking).
- **Category Caching**: `useCategoryStore` caches category trees; call `invalidate()` after any category mutation.
- **Debt Asset Direction**: `balance_apply_delta()` in `balance.c` flips the sign of `delta` for `loan`, `credit_card`, and `other_liability` categories so net-worth calculations stay correct. Do not modify this logic without understanding liability accounting.

### 4. Frontend UI & API Consistency Standard (STRICT)
- **API Layer**: **NEVER** call raw `fetch()` or `axios` in Vue components. Every API call **MUST** be placed in `frontend/src/api/<domain>.ts` using the central `http` client (`frontend/src/utils/http.ts`), which handles JWT tokens, CSRF headers, and error unwrapping automatically.
- **Page Layout Uniformity**: Every main view component in `frontend/src/views/` MUST strictly follow Minefolio's unified design system:
  1. **Page Header**: `.page-header` containing `.title-accent` (gradient bar), `<h2>` page title, and primary action button (`.action-btn`).
  2. **KPI Summary Cards**: Top `.summary-cards` row with `.summary-card` and `.highlight-card` displaying key metrics.
  3. **Filter Bar**: `.filter-panel` with inline `.premium-filters` form.
  4. **Data Table**: `.premium-table` with `.mono-amount` for currency numbers (using JetBrains Mono font), light rounded badges for types (`.type-badge`), and clean action buttons.
  5. **Modal Dialogs**: `.premium-dialog` and `.premium-form` for creation/editing modals.

---

## Conventions

- **C Code Structure**: One handler function per `.c` file, named `<domain>_<action>`. Forward-declare in `main.c`. Header files use `#pragma once`.
- **Frontend API**: One API file per domain in `frontend/src/api/`, export named functions. TypeScript interfaces live in `frontend/src/types/index.ts`.
- **Auto-registered Components**: Element Plus components are auto-registered via `unplugin-vue-components`.
- **Verification Rule**: Before declaring any task complete, run `cmake --build backend/build && npm --prefix frontend run build && cd backend && ./tests/test_link.sh` to ensure zero build errors and 21 PASS integration tests.

---

## Security (Known Limitations)

- Passwords: bcrypt (csilk `CSILK_BCRYPT_DEFAULT_COST=12`) for personal use.
- CSRF: Controlled by `MINEFOLIO_ENABLE_CSRF`.
- Multi-tenancy: Single `users` table, each query filters by `user_id` extracted from JWT.

---

## Key Files Reference

| Task | Start Here |
|------|-----------|
| Add API endpoint | `backend/src/main.c` (route registration) + existing `backend/src/*.c` |
| DB & JSON Helpers | `backend/src/common/db.h` |
| Asset & Balance Logic | `backend/src/common/balance.h` + `backend/src/common/balance.c` |
| Frontend Page Reference | `frontend/src/views/DailyExpenses.vue` / `Transactions.vue` |
| Auth Flow | `backend/src/auth.c` + `frontend/src/stores/auth.ts` + `frontend/src/utils/http.ts` |
| Docker Deployment | `Dockerfile` |
