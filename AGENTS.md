# Repository Guidelines

## Project Overview

Minefolio is a self-hosted personal finance and investment tracker. It supports multiple account types (cash, bank, credit card, loan), tracks investment positions (stocks, funds, bonds, crypto) with full cost-basis and PnL reporting, and offers AI-powered chat. The backend is C23 using the csilk HTTP framework with SQLite or PostgreSQL; the frontend is Vue 3 + TypeScript served as a SPA. A Capacitor-based mobile build uses sql.js WASM for fully offline operation.

## Architecture & Data Flow

### Backend — DDD Four-Layer C Architecture

```
Interfaces Layer   interfaces/http/controllers/  Parse params, call usecase, format response
Application Layer  application/*/{usecases,commands,dtos}.h/.c  Use case orchestration
Domain Layer       domain/*/{entity,repository,rules}.h/.c  Pure business rules, zero dependencies
Infrastructure     infrastructure/repositories/*_repo_impl.c  SQL implementations (20 files)
                   infrastructure/database/  Database abstraction and SQLite/Postgres adapters
                   (Migration engine, CSV parsing, TOTP 2FA, and REST response envelopes downstreamed to csilk framework)
Core Layer         core/financial/  Fixed-point core: money, decimal, quantity, price, rate, pnl
                   core/ledger/     Ledger engine: transaction replay, rebuild, balance/cost basis
Shared             common/  db, jwt, balance, response, ctx, tx_types, ledger_utils
                   config/  db_config, key_manager (RSA keys), secret (Secret Provider)
                   dtos/  request/response struct definitions
                   middlewares/  jwt, cors, csrf, security-headers, rate-limit
```

**Dependency direction is strict and one-way:**
- `main.c` → includes only `interfaces/http/controllers/*_controller.h`
- `interfaces/http/controllers/` → `application/*`, `dtos/`
- `application/*` → `domain/*`, `infrastructure/*`, `core/*`, `common/`
- `domain/*` → zero external dependencies (pure C, only `core/financial`)
- `infrastructure/repositories/*` → `common/db.h`, `domain/*` (contracts)

**All 17 business domains have been migrated to DDD architecture:**
tag, category, ledger, daily_expense, transfer, dca, report, file, import/export, auth, ai, market, asset, transaction, portfolio, cashflow, account.

**Legacy repos have been completely removed.** All SQL now lives in `infrastructure/repositories/*_repo_impl.c`.

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

**Desktop and mobile share the same API layer** (`src/api/`) and TypeScript types. Mobile has its own view set and a lightweight layout. The mobile build uses `VITE_API_URL` as a default compile-time fallback, but supports dynamic runtime server address configuration via `src/utils/server-url.ts` and `localStorage` (`minefolio_server_url`).

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

### Transfer Delete — Pairing Invariant

A transfer is stored as two rows: a `transfer_out` parent and a `transfer_in` child. Pairing on delete follows a strict precedence (see `application/transaction/usecases.c`):

1. **`parent_tx_id` is the authoritative key.** The create path always writes `transfer_in.parent_tx_id = transfer_out.id` (`application/transfer/usecases.c`). Deletion locates the counterpart via `parent_tx_id` first.
2. **`transfers` auxiliary table explicit foreign keys (V014).** Migration `V014__transfer_explicit_transactions.sql` introduced `from_tx_id` and `to_tx_id` referencing `transactions(id)`. Transfer cleanup deletes deterministically via `from_tx_id = ? OR to_tx_id = ?`.
3. **The `(from_asset_id, to_asset_id, amount)` attribute match is a legacy fallback only** — used solely for legacy rows written before V014 where `from_tx_id <= 0` or before `parent_tx_id` existed. Do not remove this fallback as it prevents orphaned legacy rows.

### Transaction Write Path — Savepoint Scope (db_tx_scope_t)

All multi-step balance/asset mutations in `application/{transaction,transfer,daily_expense,dca}/usecases.c` run inside `db_tx_scope_begin()` / `db_tx_scope_commit()` (csilk native nested-savepoint scope). Rules:

- `balance_apply_delta_m()` (fixed-point variant) opens its **own internal tx scope**, making the balance flip atomic even when called outside a caller scope; do not wrap it in a second explicit scope without understanding nesting (savepoints, not transactions, so nesting is safe but counts against SQLite's depth limit).
- Rollback on any failure path before responding; never partially commit position + balance + fee rows.

### Ledger Snapshots — Checkpoint Rebuild (V020)

Migration `V020__ledger_snapshots_and_tx_fx.sql` adds:
- `ledger_snapshots` table: per (user, asset, snapshot_date) position checkpoint (`quantity`, `cost_basis`, `net_value`, `current_value`, `realized_pnl`, `balance`, `tx_checkpoint_date`, `tx_checkpoint_id`), indexed `(user_id, asset_id, snapshot_date DESC)`. Written by `infrastructure/repositories/ledger_snapshot_repo_impl.c`; consumed by `core/ledger/ledger_engine.c` incremental rebuild (`ledger_rebuild_*` starts from the latest checkpoint instead of replaying full history).
- `transactions.fx_rate_to_cny REAL DEFAULT 1.0`: point-in-time exchange rate stamped at transaction create; historical asset-trend evaluation (`exchange_rate_service.c`) reads the stamped rate instead of today's rate. Backfill sets `1.0` for CNY-denominated assets.
- **Cascade invalidation**: editing/deleting a transaction dated on/before a snapshot date calls `ledger_snapshot_invalidate_from(pool, user_id, asset_id, from_date)` (`infrastructure/repositories/ledger_snapshot_repo_impl.c`), deleting that snapshot and all downstream ones; next read rebuilds from the prior valid checkpoint. Never write snapshots from use cases directly — go through the engine.

### AI Confirmation Nonces — Replay Protection (V019)

Migration `V019__ai_consumed_nonces.sql` adds `ai_consumed_nonces` (per-instance HMAC nonce ledger). `services/ai/policy/confirmation.c` persists consumed `draft_token` nonces via `ai_confirmation_verify_and_consume()` — a replayed confirmation token is rejected even across server restarts. Any new confirm-style tool MUST route through the policy verify/consume path, never a local boolean check.

### Financial Precision — 128-bit Decimal at Use-Case Boundary

`application/{asset,market}/usecases.c` MUST perform aggregation/arithmetic in `decimal_t` (128-bit fixed-point, `core/financial/decimal.h`) and convert to double only at the JSON DTO boundary (`mf_round_to_decimals`). `test_financial_precision_edge` pins float-accumulation regressions; do not reintroduce `double` accumulation loops in use cases.

### File Upload — mkstemps Temporary Parsing

`services/file_parser.c` writes uploaded bytes to `mkstemps`-created temp files (mode 0600, auto-unlinked) instead of predictable `/tmp` paths — no symlink races. New parsing entry points must follow `file_parser.h` temp-file contract.

### MCP HTTP Endpoint — JSON-RPC Error Protocol

Unauthenticated/rate-limited `POST /mcp` requests respond with JSON-RPC error `-32001` (not HTTP 401/429 bodies), aligned with the MCP spec; `jwt_middleware.c` and `rate_limit.c` special-case the `/mcp` path. `test_mcp_server.sh` pins the protocol.

### Accounts — Account-Dimension Asset Grouping (V017)

Assets group under **account** containers (bank card, Alipay, broker…). Accounts are **pure containers**: they hold no balance; an account's subtotal is always derived as `SUM(assets.current_value)` of its children (`asset_count`/`total_value` aggregates in `account_list`).

- **Schema (V017)**: `accounts` table — `UNIQUE(user_id, ledger_id, name)`, `institution` CHECK enum (`bank_card`/`platform`/`broker`/`fund_platform`/`cash_wallet`/`other`), soft-delete via `archived INTEGER`; `assets.account_id INTEGER REFERENCES accounts(id) ON DELETE RESTRICT` (usecase-level delete protection is the first gate: assets or transactions present → 1004, guide to archive).
- **Migration backfill**: per (user, ledger) creates `默认账户` (institution `cash_wallet`, icon `ph:wallet`) and re-parents all existing assets. **Never rename that literal** — `account_find_default`/`account_create_default`/the V017 backfill SQL all match on `name='默认账户'`.
- **Lazy seed**: `account_usecase_seed_default(pool, user_id, ledger_id)` is idempotent and is invoked at the same call sites as `category_usecase_seed_defaults` (`auth/usecases.c`, `admin_controller.c`, `category_controller.c`).
- **Asset fallback**: `asset_usecase_create/update` accept optional `account_id`; `account_id <= 0` falls back to the ledger's default account (keeps pre-V017 clients and `test_link.sh` asset cases passing). A provided `account_id > 0` is ownership-validated (user + ledger) → 1002 on mismatch.
- **List semantics**: `GET /api/accounts` is **unpaginated** (full array in `data`), ordered by `sort_order, id`; archived accounts are excluded unless `?include_archived=1`.
- **Frontend**: all 14 asset-picker entry points label options `{account_name} · {asset name}`; offline sql.js schema mirrors `accounts` + `assets.account_id`, and `sync.ts` pulls accounts first (before assets) with `remapAccountRefs()` for offline-created account IDs.

### Database Migration System

Database schema changes are managed strictly via csilk's native C migration engine (`<csilk/drivers/db/migration.h>`):
- **Versioned scripts**: `backend/sql/migrations/{sqlite,postgres}/` following Flyway naming `V<num>__<name>.sql` (`V001`~`V020`).
- **Tracking table**: `schema_migrations` records `version`, `name`, `checksum` (SHA-256 CRLF-normalized), `applied_at`, `execution_time_ms`, `execution_time`.
- **Concurrency lock**: `schema_migration_lock` table acts as a distributed mutex with lease timeout to prevent multi-instance migration races. Under PostgreSQL, csilk provides full native prepared statement support (`?` rewritten to `$N`, `PQprepare`/`PQexecPrepared`), ensuring lock acquisition and statement execution run reliably.
- **Engine lifecycle**: Automatic discovery, version sorting, checksum validation (tamper detection), transactional application, and status reporting.
- **Auto-Baseline**: When booting against a pre-existing legacy database (where `users` table already exists but `schema_migrations` is missing), the engine automatically baselines versions `V001`~`V007` as applied. **Never drop or reinitialize production databases**.

Balance direction is handled centrally: `balance_apply_delta()` flips the sign for liability assets (`loan`, `credit_card`, `other_liability`) so net-worth calculations stay correct. Transaction types are registered in `common/tx_types.c` — always use `tx_type_lookup()` instead of hard-coding type checks.

All asset mutations are **ownership-filtered** (`WHERE id=? AND user_id=?`) as defense-in-depth: `balance_apply_delta()`, `apply_position_fc()`, `rollback_position_fc()`, and the ledger rebuild path each qualify `asset_id` by `user_id`, so a caller can never read or write another user's asset. A non-matching asset is treated as "not found" (position update no-ops). Always pass the transaction's `user_id` into these functions; the invariant `asset.user_id == tx.user_id` is already enforced at create time by `tx_asset_exists()`.

### AI Architecture — Unified AI Runtime

```text
AI Runtime (services/ai/runtime/)
├── Session       Session persistence, auto-title, history retrieval (runtime/session.h)
├── Context       Unified ai_runtime_context_t container (user, session, messages, tools, perm, trace)
├── Model         Model provider abstraction, prompt format, token/cost estimation (model/)
├── Tool          Tool registry & schema definition — tools NEVER manage LLMs directly (tools/)
├── Workflow      High-level workflows delegating execution to Runtime (workflow/)
├── Policy        Pre-execution security evaluation: permissions, risk, confirmation, frequency (policy/)
├── Trace         End-to-end tracing, tool spans, latency & cost accounting (trace/)
└── Memory        Sliding-window context memory & token budget truncation (memory/)
```

**Unified Runtime Execution Rules:**
- Controllers (`ai_controller.c`) and Workflows (`workflow/executor.c`) **MUST NOT** manage LLM calls or build manual conversation loops directly.
- All chat requests and streaming reports execute via `ai_runtime_execute_stream()` or `ai_runtime_execute()`.
- Tools run strictly through the Policy evaluation pipeline (`ai_policy_evaluate()` -> `ai_tool_dispatch_parsed()`).
- Runtime strictly enforces budgets: `max_iterations`, `timeout_ms`, `token_budget`, `tool_budget`, `cost_budget`, and `cancellation`.
- Error taxonomy is strictly differentiated: `AI_RUNTIME_ERR_MODEL`, `AI_RUNTIME_ERR_TOOL`, `AI_RUNTIME_ERR_POLICY`, `AI_RUNTIME_ERR_TIMEOUT`, `AI_RUNTIME_ERR_CONTEXT_OVERFLOW`, `AI_RUNTIME_ERR_VALIDATION`, `AI_RUNTIME_ERR_CANCELLED`.

## Key Directories

| Path | Purpose |
|------|---------|
| `backend/src/main.c` | Entry point: DB init, migrations, middleware stack, route registration, static serve |
| `backend/src/interfaces/http/controllers/` | Thin HTTP handlers; one per domain; `register_*_routes(app)` |
| `backend/src/domain/` | Domain layer: entities, repository contracts, business rules (10+ domains) |
| `backend/src/application/` | Application layer: use case orchestration (10+ domains) |
| `backend/src/infrastructure/repositories/` | Infrastructure layer: SQL implementations of repository contracts (20 files, zero legacy) |
| `backend/src/services/ai/` | Unified AI Runtime: session, context, model, tool, workflow, policy, trace, memory |
| `backend/src/core/financial/` | Fixed-point core arithmetic: money, decimal, quantity, price, rate, pnl |
| `backend/src/core/ledger/` | Ledger engine: single source of truth, position calculation, history replay/rebuild |
| `backend/src/infrastructure/database/` | Database abstraction layer and SQLite/PostgreSQL native adapters (migration engine downstreamed to csilk) |
| `backend/src/common/` | Cross-cutting: `db.h`, `balance.h`, `jwt.h`, `response.h`, `ctx.h`, `tx_types.h`, `ledger_utils.h` (CSV, TOTP 2FA & REST response envelopes downstreamed to csilk) |
| `backend/src/config/` | `db_config.h/.c` (DSN), `key_manager.h/.c` (RSA-OAEP keys), `secret.h/.c` (Secret Provider) |
| `backend/sql/migrations/` | Versioned migration scripts (`sqlite/` & `postgres/`, `V001`~`V020`) |
| `backend/sql/` | `migration.sql` (SQLite full schema), `migration_postgres.sql` |
| `backend/tests/unit/` | 55 CTest unit test suites (financial core, ledger engine + checkpoint rebuild, domain rules, DB repository, migration engine, AI runtime/policy/nonces/batch confirmation, MCP client/pool/risk, FX semantics, portfolio PnL alignment) |
| `backend/tests/test_link.sh` | Core integration test suite (40 cases, 194 assertions: auth, accounts, CRUD, balance联动, PnL, CSV, pagination, ledger rebuild, AI trace/workflow) |
| `backend/src/domain/account/` | Account domain: `mf_account_t` entity, validation/delete rules, repository contract |
| `backend/src/application/account/usecases.c` | Account use cases: create/update/delete/archive/query, lazy `默认账户` seed, duplicate-name 1004, delete-protection 1004 |
| `backend/src/infrastructure/repositories/account_repo_impl.c` | Accounts SQL repo (with `asset_count`/`total_value` aggregates, archived filter) |
| `frontend/src/api/accounts.ts` | Account API: list (full-set array), create, update, delete |
| `frontend/src/components/AccountDialog.vue` | Account create/edit dialog shared by desktop and mobile |
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
./tests/test_link.sh                 # full integration test suite (SQLite by default)
```

**编译目录约定**：始终使用 `backend/build/` 作为 out-of-source 构建目录（即 `cmake -B build`，在 `backend/` 下执行）。禁止直接在 `backend/` 源码目录运行 in-source `cmake .`（会生成 `CMakeCache.txt`、`CMakeFiles/`、`_deps/`、`Makefile` 等构建产物污染源码目录，这些文件已在 `.gitignore` 中排除，但仍应通过正确的 `-B build` 用法避免产生它们）。

**DB driver selection** (env vars override `config/db.json`):
```bash
MINEFOLIO_DB_DRIVER=sqlite  MINEFOLIO_DB_DSN=./data/minefolio.db   ./build/minefolio
MINEFOLIO_DB_DRIVER=postgres MINEFOLIO_DB_DSN="host=… user=… dbname=…"  ./build/minefolio
```

**Dual-Engine Integration Testing (`backend/tests/db_helper.sh`)**:
All 8 integration test suites (`test_link.sh`, `test_2fa.sh`, `test_dca_cashflow.sh`, `test_ai_trace.sh`, `test_ai_tool_call.sh`, `test_market_sync.sh`, `test_ledgers.sh`, `test_fx_oauth.sh`) source `db_helper.sh` and support both SQLite and PostgreSQL.
To run tests against PostgreSQL locally:
```bash
export MINEFOLIO_DB_DRIVER=postgres
export MINEFOLIO_DB_DSN="host=localhost port=5432 user=postgres password=postgres_password dbname=minefolio_test sslmode=disable"
export PGHOST=localhost PGPORT=5432 PGUSER=postgres PGPASSWORD=postgres_password PGDATABASE=minefolio_test
./tests/test_db_helper.sh            # helper self-test
./tests/test_link.sh                 # core integration test suite
```
CI runs dual-engine verification automatically: `.github/workflows/ci.yml` executes `backend` (SQLite) and `backend-postgres` (PostgreSQL 16 Alpine container) in parallel.

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
// infrastructure/repositories/<entity>_repo_impl.h
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
- **No legacy `repositories/` directory** — all SQL lives in `infrastructure/repositories/`
- **Dynamic SQL fragments MUST use a matching `%s` specifier**: `snprintf(sql, ..., "...%s...", cond)` — a conditional SQL piece passed as an extra argument **without** a corresponding `%s` in the format string is silently dropped by `snprintf` (regression: `account_list` lost its `archived=0` filter and `ORDER BY` this way; guarded by test_link.sh Case 40 assertions).

### C — 用例层 `void* pool` 参数是刻意设计

`application/*/usecases.h` 中所有用例函数签名统一使用 `void* pool`（而非 `csilk_db_pool_t*`），这是 DDD 分层刻意的解耦设计——用例层不直接 include 基础设施层（`csilk_db_pool_t` 定义所在）头文件，仅在 `.c` 实现里做一次性强转 `(csilk_db_pool_t*)pool`。

这是**有意为之，不是疏漏**，请勿“修复”为强类型签名（改动面涉及 15 个 `usecases.h` 头文件 + 所有调用点，违背当前分层约束）。如需更强类型检查，可参考未来在 `common/db.h` 引入 `typedef csilk_db_pool_t* mf_db_pool_t` 的方案，但当前暂不实施。

### C — Service Pattern

- Extract params with `ctx_user_id(c)`, `csilk_get_param()`, `csilk_bind_json()`
- Call repositories; handle `balance_apply_delta()` / `apply_position()` for balance ops
- Build `csilk_json_t*` response; call `respond_ok()` / `respond_error()`
- Keep `BEGIN/COMMIT/ROLLBACK` blocks inside services

### C — Controller Pattern

```c
#include "interfaces/http/controllers/<domain>_controller.h"
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

### C — csilk JSON Ownership & DB Column Stringification Contract (STRICT)

`csilk_json_t` 的对象所有权模型具有三种必须严格遵守的隐式语义（详见 `deps/csilk/src/drivers/db/db.c:122`）：

1. **DB 驱动层统一序列化为 String 标量**：
   - `csilk_db_query_param_json` / `csilk_db_query_json_locked` 会将查询结果集中的**所有列值无差别序列化为 `csilk_json_string_new` 字符串标量**，不进行任何底层类型推断。
   - **数值与布尔**：必须通过 `common/db.h` 中的 `db_get_num()`、`db_get_int()`、`db_get_bool()`、`db_get_decimal()` 解析；**严禁**直接调用 `csilk_json_get_number()`（对字符串节点永远返回 `0.0`）。
   - **嵌套 JSON 列**（如 `tags`、`config_json`、`metadata`）：在 DB 返回的 JSON 行对象中是**未解析的 JSON 转义字符串标量**，绝非 JSON 数组或对象！向前端或上层交付前（如 `mf_format_expense_rows_with_tags`），必须显式执行一次 `csilk_json_parse()` 将字符串反序列化为真正的 `csilk_json_array/object`，并挂载回结果集。

2. **所有权转移（Ownership Transfer）**：
   - 将子对象/数组挂载到父节点（如 `csilk_json_array_append(arr, item)`、`csilk_json_add_object(parent, key, child)`）会**转移所有权**。父节点销毁时会级联 `csilk_json_free` 释放其所有子节点。挂载后严禁对子节点单独调用 `csilk_json_free`（会导致 Double-Free 崩溃）。

3. **视图借用 vs 跨文档深拷贝（View vs Deep-Copy）**：
   - `csilk_json_get(obj, key)` 与 `csilk_json_array_get(arr, idx)` 返回的是**借用指针（Borrowed View）**，生命周期与宿主文档绑定，禁止单独释放。
   - 若需将借用指针挂载到另一个独立的 JSON 文档中，**必须先通过 `csilk_json_copy()` 进行跨文档 Deep-Copy**。禁止跨文档直接指针挂载，否则原文档释放后立即引发 Use-After-Free (UAF)。

### C — Database & Number Parsing (STRICT)

- **NEVER** interpolate user input into SQL strings
- **ALWAYS** use `db_get_num(obj, "key")` and `db_get_int(obj, "key")` from `db.h`
- **NEVER** call `csilk_json_get_number()` directly — it returns `0.0` for string nodes
- **ALWAYS** use `parse_query_int_strict()` from `common/response.h` to parse HTTP query/path integer parameters; **NEVER** use `atoll()` which silently coerces invalid input to `0`. On failure, return HTTP 1002 (`respond_bad_request`).
- **ALWAYS** wrap multi-step balance/asset mutations in `db_tx_scope_begin()` / `db_tx_scope_commit()`, check `balance_apply_delta()` return codes, and rollback on failure.

### C — Response Envelope

All responses are HTTP 200 with JSON `{code, message, data}`:

| code | meaning |
|------|---------|
| 0 | OK |
| 1001 | Unauthorized (JWT missing/expired) |
| 1002 | Bad request / Validation failed |
| 1003 | Not found |
| 1004 | Conflict / Forbidden |

Use `respond_ok` / `respond_error` / `respond_bad_request` / `respond_unauthorized` / `respond_page_ok` from `common/response.h`. These functions are thin wrappers that delegate directly to csilk's core response envelope functions (`csilk_respond_ok`, `csilk_respond_fail`, `csilk_respond_page_ex`, `csilk_parse_pagination`), maintaining the legacy HTTP 200 contract and `"list"` pagination key for the Vue 3 frontend. On multi-step balance updates with `BEGIN TRANSACTION`, always `ROLLBACK` on failure before returning an error.

### C — Net Value PUT Gotcha

When updating an investment asset's `net_value` via `PUT /api/assets/:id`, declare `char nv_str[64]` **before** the `upd_params[]` array on the stack. Declaring it after can cause the compiler to reuse stack space and produce wrong values.

### Frontend — API Layer (STRICT)

- **NEVER** call raw `fetch()` or `axios` in Vue components
- Every API call lives in `frontend/src/api/<domain>.ts` using the central `http` client (`frontend/src/utils/http.ts`)
- **Exception**: `axios.get()` with `responseType: 'blob'` for CSV downloads (`http.get()` doesn't support blobs)

### Frontend — Page Layout

- `.page-header` uses `justify-content: space-between`; action buttons wrapped in `<div class="header-actions">` with `gap: 8px`
- `Layout.vue` locks `.layout-container` to `height: 100vh; overflow: hidden`; `.main` (`flex: 1 1 0%; min-height: 0; overflow-y: auto`) is the default single scroll container, and `.aside` scrolls its menu internally
- Page containers use `height: 100%; overflow: hidden` so only data areas scroll
- **Full-Bleed Route Mode**: For immersive, full-bleed workspaces (e.g. `/chat`), configure `meta: { fullBleed: true }` in `router/index.ts`. `Layout.vue` conditionally attaches `.is-full-bleed` to `<el-main>`, eliminating outer padding (`padding: 0`) and outer scrollbars (`overflow: hidden`).
- **Chat Flex Input Docking & Messages Container**: Never use `position: absolute; bottom: ...` for `.chat-input-area` to avoid masking trailing messages. Place it directly in normal Flex vertical document flow (`flex-shrink: 0`). Wrap `.messages` inside a dedicated `.messages-container` (`flex: 1; min-height: 0; position: relative`), with `.scroll-bottom-btn` inside `.messages-container` anchored at `bottom: 14px`.

### Frontend — Error Resilience

Every `onMounted` hook **MUST** wrap async initialization in `try/catch`. Use `v-loading` for loading states. Prefer `Promise.allSettled` over `Promise.all` for parallel API calls so partial failures don't crash the page.

### Frontend — Category Cache

`useCategoryStore` caches category trees. Call `invalidate()` after any category mutation (create, update, delete).

### Frontend — Local Date Helpers (STRICT)

**NEVER** use `new Date().toISOString().slice(0, 10)` or `.slice(0, 7)` for "today" / "this month" defaults. In UTC+ timezones (e.g. China), UTC dates drift by one day between 00:00–08:00 local time.

**ALWAYS** use the local-date helpers from `frontend/src/utils/format.ts`:
```ts
import { localToday, localThisMonth } from '@/utils/format'
const today = localToday()      // 'YYYY-MM-DD' in local timezone
const month = localThisMonth()  // 'YYYY-MM' in local timezone
```

### Frontend — Chat Session Race Guard

When switching chat sessions or loading more history, stale async results from a previous session must not contaminate the current one. The chat store uses a `sessionSwitchSeq` counter: `selectSession()` and `loadMoreMessages()` capture the current sequence number on entry and discard their result if the sequence advanced (i.e. the user switched sessions while the request was in flight).

**Rule:** Never bypass the sequence guard when adding new async operations to the chat store.

### Frontend — Logout Store Isolation

On logout, the auth store resets all account-scoped Pinia stores (chat, ledger, category) via dynamic imports to avoid import cycles. Chat drafts are also purged from `localStorage`. If you add a new store that holds per-account data, you **must** add a `reset()` method and call it from `auth.logout()`.

### Frontend — Mobile Offline Reads

Mobile views should fall back to the local sql.js DB when online API calls fail. Use the pattern from `DailyExpensesMobile.vue`:
```ts
try {
  const res = await api.list(params)
  data.value = res.list
} catch {
  loadLocalList()  // fallback to local sql.js
}
```
Do NOT mask a successful server response with stale local data — only use local fallback when the online request actually fails.

### Frontend — Mobile Server Address Configuration

Mobile users can customize the backend server address (e.g. LAN IP `http://192.168.1.100:8080` or domain reverse proxy) at runtime:
- **Persistence**: Managed via `src/utils/server-url.ts` under `localStorage` key `minefolio_server_url`. Fallback is `VITE_API_URL` or relative `/api`.
- **Normalization & Probing**: Automatically infers `http://` / `https://` protocol and trims trailing slashes. Includes `testServerConnection()` to probe `/api/system/status` with latency measurement and error diagnostics.
- **Dynamic Axios Synchronization**: `http.ts` listens to `onServerUrlChange` and dynamically rebinds Axios default `baseURL` and interceptor `config.baseURL`.
- **RSA Public Key Cache Invalidation**: `crypto.ts` invalidates `cachedCryptoKey` upon server URL changes to re-negotiate keys with the new instance.
- **UI Access**: Configurable on `LoginMobile.vue` (via `ServerConfigDialog.vue` status bar) before logging in, and on `SettingsMobile.vue` (via dedicated server address card with unsynced offline data safeguards).

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
| `backend/CMakeLists.txt` | C23 standard; FetchContent for csilk (GIT_TAG master, local `deps/csilk` override); optional PostgreSQL |
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
| `frontend/src/stores/category.ts` | Category tree cache with `invalidate()` and `reset()` |
| `frontend/src/stores/chat.ts` | Chat sessions, messages, streaming state; `resetState()` on logout |
| `frontend/src/stores/ledger.ts` | Ledger list; `reset()` on logout |
| `frontend/src/utils/format.ts` | `localToday()`, `localThisMonth()` — local timezone date helpers (never use `toISOString().slice()`)
| `Dockerfile` | Multi-stage: backend-build → frontend-build → nginx runtime |
| `docker-compose.yml` | Two-service compose: minefolio + nginx proxy on :80 |
| `scripts/dev.sh` | Local dev: build backend, start server :8080, start frontend :5173 |
| `scripts/build.sh` | Production build: cmake Release + npm run build |

## Market / FX — 单实例假设

汇率同步（`backend/src/services/market/exchange_rate_service.c`）当前按**单实例部署**设计：`g_rates[]` 是进程内静态内存表，`exchange_rate_refresh_all()` 无跨实例协调机制。多副本部署时不同实例的内存表可能不一致（读路径 `exchange_rate_get_to_cny` 优先查内存表，查不到时才回落到 `exchange_rate_history` 表）。

**多副本部署前需改造或明确标注**，可选方向：
- （a）读路径改为优先查 `exchange_rate_history` 最新一行，消除对内存表的依赖；
- （b）引入类似 `schema_migration_lock` 的行锁机制做单写者仲裁；
- （c）保持现状，并在部署文档中明确标注"汇率同步仅支持单实例部署"。

本次改动仅为文档标注，不做代码改造。

## Runtime / Tooling Preferences

| Layer | Runtime | Package Manager | Notes |
|-------|---------|-----------------|-------|
| Backend | Linux (gcc-14) | CMake 3.16+ | C23 standard; GNU extensions |
| Backend (SQLite) | SQLite 3.35+ | — | 18 个 repository 实现文件依赖 `RETURNING` 语法（该特性于 SQLite 3.35 引入），版本过低时相关写入路径会失败 |
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

### Backend Unit & Integration Tests (50 CTest Suites + 16 Integration Suites)

```bash
cd backend
# 1. Run all 50 CTest unit test suites (sub-second fast feedback)
cd build && ctest --output-on-failure && cd ..

# 2. Run all core integration test suites
./tests/test_link.sh          # 174 assertions across 40 cases: auth, accounts dimension, CRUD, balance联动, PnL, CSV, pagination, ledger rebuild, AI trace/workflow

./tests/test_ledgers.sh       # 16 cases: multi-ledger spaces, member RBAC (Owner/Editor/Viewer)
./tests/test_2fa.sh           # 12 cases: TOTP 2FA secret gen, QR code, enable/disable, login verification
./tests/test_dca_cashflow.sh  # 18 cases: DCA periodic purchase execution, cashflow calendar projection
./tests/test_ai_trace.sh      # 17 cases: AI conversation traces, token usage, tool spans
./tests/test_ai_tool_call.sh  # 15 cases: AI tool-call loop (single + 2 parallel tools) vs mock OpenAI server; regression for tool-message use-after-free
./tests/test_ai_truncate.sh   # 14 cases: edit-resend server history truncation, persisted message-id SSE events, regenerate delete semantics
./tests/test_ai_loop_hardening.sh  # 28 assertions: runtime transient retry, budget wrap-up with duplicate-call guard, client-disconnect cancel, partial-text persistence, stalled-upstream HTTP timeout (error 1004), confirmation enforcement
./tests/test_market_sync.sh   # 18 cases: Multi-source market quotes, caching, bulk sync
./tests/test_fx_oauth.sh      # 20 cases: Exchange rates, FX gain/loss PnL, receipt OCR, OAuth2/OIDC SSO
./tests/test_mcp.sh           # MCP stdio server config + chat tool-call dispatch routing
./tests/test_mcp_server.sh    # External-client view of POST /mcp (initialize/tools/list/tools/call JSON-RPC); unauthenticated requests rejected with JSON-RPC -32001
./tests/test_report_isolation.sh # Expense monthly/report isolation regression (user_id + ledger_id)
```

Tests start real servers with temp SQLite DBs, exercise all API endpoints via curl/JSON, and verify database state directly with `sqlite3`. `test_ai_tool_call.sh`, `test_ai_truncate.sh` and `test_ai_loop_hardening.sh` also start `mock_ai_tool_server.js` (node) as their AI provider. Run before committing any changes.

### Frontend Tests

```bash
cd frontend
npm test    # vitest run --config vite.config.mobile.ts (jsdom environment)
npm run build         # Desktop type-check + build
npm run build:mobile  # Mobile type-check + build
```

Frontend unit tests cover 37 test suites (177 unit tests) including format helpers, crypto, auth store, offline-http sync, chat store race guards, AI thinking/reasoning mode, analysis panel, chat layout decoupling, and AI stream failure handling; the integration test suite (`test_link.sh`) covers the critical paths end-to-end.

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
11. **Offline sync ID mapping**: When creating records offline, the local row ID must exactly match the queue's `record_id`. Never let SQLite auto-assign a different ID; include the `id` column in the INSERT statement. After syncing an offline-created asset, remap dependent local rows (transactions, daily_expenses) and queued payload FKs from the placeholder ID to the server-assigned ID.
12. **Chat auto-scroll on history prepend**: Do NOT watch `chat.messages.length` to trigger auto-scroll — prepending older pages changes the length and forces the view to the bottom. Instead, watch only the **last message ID** to detect genuinely new appended content.
13. **UTC date drift in forms**: `new Date().toISOString().slice(0, 10)` returns UTC, not local. In UTC+ timezones between 00:00–08:00, this yields yesterday's date. Use `localToday()` from `@/utils/format` instead.
14. **Transaction dialog fee field**: Always reset `form.fee` to 0 in the new-transaction branch of `openDialog()`, and map `Number(txn.fee) || 0` in the edit branch. Clear `fee` in `onTransactionTypeChange()` when switching away from buy/sell to prevent stale fees from being sent on non-investment transactions.

## Commit Convention

Follows [Conventional Commits](https://www.conventionalcommits.org/) with a gitmoji prefix. The header is the only required line — this repo commits almost exclusively as subject-only.

```
type(scope): 🎯 subject
```

**Anatomy**

| Part | Rule |
|------|------|
| `type` | Required, lowercase, from the table below |
| `(scope)` | Optional, lowercase, from the scope vocabulary below |
| emoji | Required, one per commit, **after** the colon (`): ✨ …`), matching the type |
| `subject` | Required, lowercase imperative mood, no trailing period, ≤ ~72 chars, describes the change not the ticket |

**Types**

| Type | Emoji | When |
|------|-------|------|
| feat | ✨ | New user-facing functionality |
| fix | 🐛 | Bug fix / defect correction |
| docs | 📝 | Documentation only (code, design, plans) |
| style | 🎨 | Formatting/whitespace; no behavior change |
| refactor | ♻️ | Restructuring; no behavior/API change |
| perf | ⚡️ | Performance improvement |
| test | ✅ | Add or update tests |
| build | 📦 | Build system / dependency changes |
| ci | 👷 | CI configuration & scripts |
| chore | 🧹 | Maintenance; none of the above |
| revert | ⏪️ | Reverts a prior commit (see below) |

> Emoji MUST match the type per this table. Older commits (e.g. the `global-indices` series) used `🎯` as a catch-all for every type — do not propagate that; new commits use the mapped emoji. `test` is canonically `✅`; `🧪` appearing historically is deprecated.

**Scopes** (lowercase; use the area the diff actually touches)

| Scope | Covers |
|-------|--------|
| `core` | Financial/ledger core + shared `common/` (balance, tx_types, position calc) |
| `domain` / `application` / `infrastructure` | DDD-layer changes when a diff spans one layer across domains |
| `market` | Quotes, indices, exchange rates, scheduler, sync |
| `transaction` | Transactions, transfers, fees, PnL write paths |
| `asset` | Assets, positions, categories |
| `ai` | AI Runtime, tools, MCP, policy, chat |
| `auth` | Authentication, JWT, 2FA, RBAC, ledgers/membership |
| `db` | Schema, migrations, repository/SQL plumbing |
| `frontend` | Vue SPA/desktop components & stores |
| `mobile` | Mobile views, offline sql.js, Capacitor |
| `i18n` | Translation dictionaries |

Omit the scope for repo-wide changes (e.g. `chore: sync auto-generated mobile components declarations`). Prefer the narrowest true scope; do not invent scopes ad hoc.

**Body & footer**

The subject-only form is the norm; add a body only when the *why* isn't obvious from the diff. When present:

- Separate header from body with one blank line; wrap body at ~72 cols.
- Explain motivation and contrast with alternatives — the *why*, not the *what* (the diff already shows the what).
- Footer for references: `Closes #123`, `Refs …`.

**Breaking changes**

Append `!` before the colon (`feat(api)!: 🐛 …`) **and** add a `BREAKING CHANGE: <description>` footer. Reserve for wire-format, migration, or public-API changes the frontend must adapt to in lockstep.

**Revert**

`revert: ⏪️ <reverted subject>`, then a body line `This reverts commit <hash>.`.

**Merge commits** (`Merge branch '…'`) are exempt from the convention.

**Examples (from real history)**

```
refactor(core): ♻️ enforce user_id scope in position calculation and clean legacy models
fix(transaction): 🐛 sanitize nullable foreign keys in tx_insert to sql null
feat(market): ✨ implement market-clock aware quote_engine_sync_indices_to_db
docs(market): 📝 add design spec for market minute sync and db-centric cqrs
chore: sync auto-generated mobile components declarations
```

Anti-patterns: capitalized/gerund subjects (`✨ Added feature`), ticket-only or vague subjects (`🐛 fix bug`), emoji before the colon, multiple emoji in one commit, and non-mapped type/emoji pairs.
