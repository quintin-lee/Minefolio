# Changelog

All notable changes to Minefolio will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

_No changes yet._

---

## [1.2.0] - 2026-09-09

### Verified
- **Full CI Green (2026-09-09)**:
  - All 6 CI jobs pass: Backend Build & Test, Frontend Typecheck+Build, iOS Build, Android Build, Frontend Docker Image, Docker Image Package.
  - Integration tests: `test_link.sh` 147/147, `test_2fa.sh` 17/17, `test_ai_trace.sh` 17/17, `test_ai_tool_call.sh` 15/15, `test_ledgers.sh` ALL, `test_fx_oauth.sh` 20/20.
  - Unit tests: 28/28 CTest suites pass.
  - Zero legacy repository files remaining. `backend/src/repositories/` directory deleted.

### Added
- **Legacy Repository Complete Deletion**:
  - Deleted ALL 17 legacy `repositories/*.c/h` files, eliminating the dual-track architecture.
  - All SQL now lives exclusively in `infrastructure/repositories/*_repo_impl.c` (18 files).
  - Created new infrastructure implementations: `ai_trace_repo_impl.h/.c`, `ai_session_repo_impl.h/.c`, `ai_settings_repo_impl.h/.c`, `import_rule_repo_impl.h/.c`.
  - Extracted shared ledger utilities to `common/ledger_utils.h/.c` (tx_get_old, tx_child_fee_rows, tx_delete_fee_children, ledger_get_default, ledger_get_user_role).
  - Updated ~30 source files to use new repository headers.
  - 28/28 unit tests pass, 100% build success.

- **DDD Four-Layer Architecture Migration (Complete)**:
  - Migrated all 16 business domains to Domain-Driven Design (DDD) four-layer architecture:
    - **Domain Layer** (`backend/src/domain/`): Pure business entities, repository contracts, and rules with zero external dependencies.
    - **Application Layer** (`backend/src/application/`): Use case orchestration with command objects and result DTOs.
    - **Infrastructure Layer** (`backend/src/infrastructure/repositories/`): SQL implementations of repository contracts.
    - **Interface Layer** (`backend/src/interfaces/http/controllers/`): Thin HTTP handlers delegating to use cases.
  - Migrated domains: tag, category, ledger, daily_expense, transfer, dca, report, file, import/export, auth, ai, market, asset, transaction, portfolio, cashflow.
  - Created ~135 new DDD files with ~10,000+ lines of code.
  - All 143 integration tests pass with zero regressions.

- **AI Subsystem Repository Abstraction**:
  - Created `domain/ai/repository.h` with unified data access interface for AI tools and workflows.
  - Implemented `infrastructure/repositories/ai_repo_impl.c` wrapping legacy repository functions.
  - Migrated all 6 AI tools (asset, cashflow, expense, portfolio, transaction, transfer) to use new repository interface.
  - Migrated all 4 AI workflows (cashflow_forecast, financial_health, monthly_review, portfolio_analysis) to use new repository interface.

### Fixed
- **Offline sync ID mapping** (`frontend/src/utils/offline-http.ts`, `frontend/src/stores/sync.ts`):
  - Fixed offline-created records using two different IDs (queue `record_id` vs SQLite auto-increment rowid), causing duplicate/wrong records after sync. Local rows now insert with explicit `id` matching the queue's `record_id`.
  - New `remapAssetRefs()` rewrites dependent local rows (transactions, daily_expenses) and queued payloads when an offline-created asset gets its real server ID.
  - Offline edits now update in place instead of inserting duplicate rows.
  - Added 4 regression tests in `offline-http.spec.ts` and `sync.store.spec.ts`.
- **Chat load-more scroll** (`frontend/src/views/Chat.vue`):
  - Replaced `watch(() => chat.messages.length)` (which fired on history prepend and forced scroll to bottom) with a watcher on the last message ID. Only auto-sticks when genuinely new content is appended.
- **UTC date defaults across 17 call sites** (`frontend/src/utils/format.ts`, 12 view/component files):
  - Added `localToday()` and `localThisMonth()` helpers returning local-timezone dates.
  - Replaced all `new Date().toISOString().slice(0, 10|7)` calls that produced UTC dates (wrong in UTC+ timezones between 00:00–08:00).
  - Added 3 unit tests in `format.spec.ts` including a fake-timer test at local midnight.
- **Chat state leak on logout** (`frontend/src/stores/auth.ts`, `frontend/src/stores/chat.ts`):
  - `logout()` now calls `chat.resetState()` (sessions, messages, streaming state, model selections) and clears chat drafts from localStorage.
- **Ledger/category state leak on logout** (`frontend/src/stores/ledger.ts`, `frontend/src/stores/category.ts`):
  - Added `reset()` methods to both stores; `logout()` now resets all three account-scoped stores.
- **Chat session-switch race** (`frontend/src/stores/chat.ts`):
  - Added `sessionSwitchSeq` counter to prevent stale `selectSession` / `loadMoreMessages` results from contaminating the current session.
  - Added 2 regression tests in `chat.store.spec.ts`.
- **Asset cascader operator precedence** (`frontend/src/views/Assets.vue`):
  - Fixed `&&` binding tighter than `||` causing lazy-loaded child asset categories to appear at root level.
- **Stale fee in transaction dialog** (`frontend/src/views/Transactions.vue`):
  - `openDialog()` edit branch now maps the row's real fee; reset branch sets `fee: 0`.
  - `onTransactionTypeChange()` clears fee when switching away from buy/sell.
- **Mobile initial sync** (`frontend/src/views-mobile/MobileLayout.vue`):
  - `onMounted` now triggers `syncNow()` once the protected mobile layout mounts.
- **Mobile daily expenses offline reads** (`frontend/src/views-mobile/DailyExpensesMobile.vue`):
  - `loadData()` now falls back to local sql.js when online list or month-summary requests fail.

### Added
- **Unified AI Runtime Architecture (`backend/src/services/ai/runtime/`, `backend/src/services/ai/memory/`)**:
  - Implemented Minefolio's centralized C-native AI Runtime architecture completely decoupling Controllers and Workflows from direct LLM invocations.
  - 8-Subsystem Runtime Architecture:
    - `Session`: Session persistence, auto-title generation, and history retrieval (`runtime/session.h`).
    - `Context`: Unified execution container `ai_runtime_context_t` consolidating user ID, session ID, message history, tool registry, permissions, trace metadata, limits, and runtime statistics (`runtime/context.h`).
    - `Model`: Provider abstraction layer, structured prompt formatting, token usage and cost estimation (`model/model.h`).
    - `Tool`: Schema-based tool definitions and dispatch pipeline with strict parameter validation (`tools/registry.h`).
    - `Workflow`: High-level multi-step financial DAG workflows delegating LLM turns to Runtime (`workflow/executor.h`).
    - `Policy`: Pre-execution security interception enforcing 5-tier financial risk assessments, confirmations, and frequency bounds (`policy/policy.h`).
    - `Trace`: End-to-end tracing capturing turn latencies, token consumption, and tool execution spans (`trace/trace.h`).
    - `Memory`: Sliding-window message context manager (`memory/memory.h`) guaranteeing system prompt retention and token budget compliance.
  - **Autonomous Agent Execution Loop (`runtime/loop.h/.c`)**:
    - Complete agent state machine: Context build $\to$ Memory sliding window $\to$ Pre-turn budget validation $\to$ Model stream invocation $\to$ Response parsing $\to$ Tool budget check $\to$ Policy evaluation $\to$ Tool execution $\to$ Trace recording $\to$ Context append $\to$ Loop continuation $\to$ Stream finish.
    - Unified stream entrypoint `ai_runtime_execute_stream()` and batch execution entrypoint `ai_runtime_execute()`.
  - **Runtime Limits & Budgets Enforcement (`runtime/limits.h/.c`)**:
    - Strict enforcement of `max_iterations`, `timeout_ms`, `token_budget`, `tool_budget`, and `cost_budget`.
    - Cooperative cancellation support via `cancellation_token` pointer for client disconnects or abort signals.
  - **Structured Error Taxonomy (`runtime/error.h/.c`)**:
    - Differentiated error codes: `AI_RUNTIME_ERR_OK`, `AI_RUNTIME_ERR_MODEL`, `AI_RUNTIME_ERR_TOOL`, `AI_RUNTIME_ERR_POLICY`, `AI_RUNTIME_ERR_TIMEOUT`, `AI_RUNTIME_ERR_CONTEXT_OVERFLOW`, `AI_RUNTIME_ERR_VALIDATION`, `AI_RUNTIME_ERR_CANCELLED`.
    - Human-readable error names and structured status objects with detail messages.
  - **Dedicated Unit Test Suite (`backend/tests/unit/test_ai_runtime.c`)**:
    - Added test cases verifying context lifecycle, error formatting, limit checks (iteration limit, token budget, tool budget, timeout), memory sliding window with system message pinning, and loop cancellation handling (CTest total reaches 27 suites).

### Changed
- `backend/src/services/ai_service.c`: Refactored `ai_chat_handler` and `ai_service_stream_report` to delegate completely to `ai_runtime_execute_stream()`, eliminating over 400 lines of duplicated conversation loops, manual token counting, and tool allocation boilerplate.
- `AGENTS.md`: Updated AI Architecture guidelines, runtime execution rules, and directory map.

### Summary
Complete elimination of legacy dual-track architecture. All 16 business domains now use DDD four-layer architecture exclusively. Zero legacy repository files remain.

### Changed
- Deleted `backend/src/repositories/` directory (17 legacy repo files removed).
- All SQL consolidated in `backend/src/infrastructure/repositories/*_repo_impl.c` (18 files).
- All 20 HTTP controllers in `interfaces/http/controllers/` (legacy controllers directory removed).
- Updated `docs/architecture.md` to reflect zero legacy components.

---

## [1.1.0] - 2026-09-04

### Added
- **DDD 4-Layer Architecture (`src/interfaces/`, `src/application/`, `src/domain/`, `src/infrastructure/`)**:
  - Full domain-driven design architectural decoupling across all core modules:
    - Pure domain entities and invariant business rules (`src/domain/`) with zero external framework dependencies.
    - Application use cases (`src/application/`) managing transaction boundaries, orchestration, and domain repository contracts.
    - Clean domain repository implementations (`src/infrastructure/database/`) with dual SQLite and PostgreSQL support.
    - Thin HTTP interface controllers (`src/interfaces/http/controllers/`) handling parameter extraction and response envelopes.
  - 11 domain rule and repository test suites registered in CTest (`test_domain_transaction`, `test_domain_asset`, `test_domain_auth`, `test_domain_ai`, `test_domain_portfolio`, `test_domain_cashflow`, `test_domain_market`, `test_domain_cost_basis`, `test_domain_pnl`, `test_domain_position`, `test_domain_multi_currency`, `test_database_repository`).

- **Containerization & Production CI/CD Hardening**:
  - Resolved backend container shared library loading failure (`libyyjson.so.0`) by enforcing static dependency linking (`BUILD_SHARED_LIBS=OFF`, `BUILD_STATIC_LIBS=ON`, `CSILK_BUILD_SHARED=OFF`) in both `CMakeLists.txt` and `Dockerfile`.
  - Added double-layer defense-in-depth in Dockerfile to collect build-stage shared objects to `/usr/local/lib/` and register them via `ldconfig`.
  - Aligned frontend distribution paths (`/opt/minefolio/frontend/dist` and `/usr/share/nginx/html`) across `Dockerfile.frontend` and Nginx configurations, eliminating 500 rewrite loops.
  - Implemented dynamic upstream DNS resolution (`resolver 127.0.0.11 valid=10s ipv6=off;` + variable `$backend_upstream`) in `nginx/minefolio.docker.conf`, making Nginx boot resilient against backend container initialization timing.
  - Made Docker image smoke tests mandatory in `.github/workflows/ci.yml` across all branches, tags, and pull requests prior to pushing images to GitHub Container Registry (GHCR).
- Fixed container timezone mismatch by installing `tzdata` in the Dockerfile runtime image and configuring `TZ=Asia/Shanghai` with `/etc/localtime` mount in `docker-compose.yml`, ensuring backend `localtime`/`strftime` outputs match the intended production timezone.

- **Database Migration System (`backend/src/infrastructure/database/migration/`, `backend/sql/migrations/`)**:
  - Replaced legacy 700+ line ad-hoc `col_exists()` and hardcoded `ALTER TABLE` routine in `db.c` with a formal native C migration engine.
  - Flyway-style versioned migration scripts for SQLite and PostgreSQL:
    - `V001__initial_auth_and_system.sql`: Core users, system settings, tokens.
    - `V002__categories_and_assets.sql`: Category tree, assets, price history.
    - `V003__transactions_and_expenses.sql`: Transactions, expenses, tags, parent_tx_id cascading.
    - `V004__ledgers_and_members.sql`: Multi-ledger spaces, member RBAC, and business table `ledger_id` links.
    - `V005__ai_traces_and_settings.sql`: AI chat sessions, messages, traces, and model configurations.
    - `V006__dca_and_cashflow.sql`: DCA recurring plans, execution history, and cashflow calendar schedules.
    - `V007__market_quotes_and_fx.sql`: Exchange rate pairs, FX history snapshots, smart import rules.
  - Tracking table `schema_migrations` storing `version`, `name`, `checksum`, `applied_at`, `execution_time_ms`, `execution_time`.
  - Mutex lock table `schema_migration_lock` with timeout lease preventing multi-instance race conditions.
  - SHA-256 CRLF-normalized hashing (`checksum.c`) ensuring platform-independent tamper detection.
  - Migration engine lifecycle: discovery, version sorting, validation, transactional apply, and status reporting.
  - Non-destructive Auto-Baseline mechanism automatically detecting legacy databases and recording V001~V007 as applied without wiping existing data.
  - Dedicated unit test suite: `test_migration_engine` covering hashing, discovery, mutex concurrency, idempotence, tamper detection, and auto-baseline.

- **Financial Core Engine (`backend/src/core/financial/`)**:
  - `money.h/.c`: 64-bit signed integer fixed-point money type with fractional units (cents) to completely eliminate floating-point rounding errors in currency operations.
  - `decimal.h/.c`: Arbitrary precision decimal calculations supporting rounding modes (Banker's, Truncate, Half Up).
  - `quantity.h/.c`: Position share precision calculations supporting crypto sub-units and micro-lots.
  - `price.h/.c`: Execution price arithmetic with currency matching.
  - `rate.h/.c`: Exchange rate and dividend yield calculations with bounded precision.
  - `percentage.h/.c`: Percentage and basis-point calculations.
  - `currency.h/.c`: ISO 4217 standard currency registry with code, symbol, display format, and standard decimal places.
  - `pnl.h/.c`: Realized and unrealized PnL computation models with cost basis attribution.
  - 8 financial unit test suites registered in CTest (`test_currency`, `test_decimal`, `test_money`, `test_quantity`, `test_price`, `test_rate`, `test_pnl`, `test_fx`).

- **Ledger Engine (`backend/src/core/ledger/`)**:
  - Centralized Ledger Engine serving as the single source of financial truth:
    $$\text{Transaction} \longrightarrow \text{Ledger Engine} \longrightarrow \text{Position, Balance, Cost Basis, Realized/Unrealized PnL, Portfolio}$$
  - `ledger_engine_apply_transaction`: Atomically calculates and applies positions, cost-basis adjustments, and cash balances from transaction facts.
  - `ledger_engine_reverse_transaction`: Reverses balance deltas and position adjustments safely upon deletion or rollback.
  - `ledger_engine_rebuild_position`: Replays the complete chronological transaction history for an asset from genesis to recalculate quantity, cost basis, and current net value.
  - `ledger_engine_rebuild_portfolio`: Full portfolio recomputation across all user assets.
  - Dedicated rebuild REST endpoints: `POST /api/assets/:id/rebuild` and `POST /api/assets/rebuild`.
  - CTest unit tests: `test_ledger_math` and `test_ledger_engine`.

- **Modular AI Architecture (`backend/src/services/ai/`)**:
  - Decoupled former monolithic `ai_workflow_service.c` into clean single-responsibility architectural subsystems:
    - `runtime/`: Execution loop, thread contexts, conversation session state machine.
    - `model/`: Provider abstraction, structured request building, SSE stream response decoding.
    - `workflow/`: Universal DAG engine, graph validation, node dispatching, state lifecycle execution.
    - `workflows/`: Pre-built finance workflows (`financial_health`, `cashflow_forecast`, `monthly_review`, `portfolio_analysis`).
    - `tools/`: Tool registry, dispatcher, schema validator, context isolation.
    - `policy/`: Authorization, permission management, risk evaluation, double-confirmation token lifecycle.
    - `trace/`: OpenTelemetry-compatible span tracing, latency accounting, token usage exporter.

- **AI Tool Framework (`backend/src/services/ai/tools/`)**:
  - Formal registration and schema definition framework: `ai_tool_t`, JSON Schema parameter validation, type-safe arguments parsing.
  - Decoupled into 7 domain-specific tool modules:
    - `asset_tool.c`: Query assets, balances, and account summaries.
    - `transaction_tool.c`: Query transaction records and propose transaction drafts.
    - `transfer_tool.c`: Propose cross-asset fund transfers.
    - `cashflow_tool.c`: Query upcoming cashflow schedules and projection calendars.
    - `expense_tool.c`: Query and record daily expense/income transactions.
    - `portfolio_tool.c`: Query portfolio weights, performance, and risk metrics.
    - `report_tool.c`: Fetch multi-currency summaries and FX gain/loss attribution.
  - Registered unit test suite: `test_ai_tools`.

- **AI Policy, Risk & Anti-Replay Confirmation Framework (`backend/src/services/ai/policy/`)**:
  - 5-Tier Financial Risk Matrix:
    - `READ_ONLY`: Asset/transaction/report queries (auto-approved).
    - `LOW`: Draft generation and non-financial state mutations.
    - `MEDIUM`: Standard transaction creation and expense recordings.
    - `HIGH`: Real monetary mutations, fund transfers, and ledger state alterations.
    - `CRITICAL`: Large fund transfers and destructive portfolio operations.
  - Bound Confirmation Tokens (`mf_v2.<payload>.<mac>`): Cryptographically binds `user_id`, `session_id`, `tool_name`, canonical SHA-256 arguments hash, `risk_level`, `timestamp`, unique random `nonce`, and `expiration`.
  - Constant-time memory comparison (`ai_confirmation_constant_time_memcmp`) to thwart timing attacks on HMAC signatures.
  - Thread-safe anti-replay nonce cache preventing token re-use and double execution.
  - Structured audit logging (`audit.h/.c`) with automatic redacting of sensitive JWT and API keys.
  - Registered unit test suite: `test_ai_policy`.

- **Unified Secret Provider (`backend/src/config/secret.h/.c`)**:
  - Centralized secret and configuration management eliminating scattered `getenv` calls.
  - Multi-tier provider resolution hierarchy:
    1. In-memory test overrides (`config_secret_set_test_override`) for complete testing isolation.
    2. External Secret Manager plugin (`config_secret_set_manager`) for future HashiCorp Vault / AWS Secrets Manager / K8s integrations.
    3. Environment variables (`MINEFOLIO_<KEY>` and `<KEY>`).
    4. File provider (`<KEY>_FILE`, `/run/secrets/<key_lower>`, `config/secrets/<key_lower>`) supporting Docker Secrets and K8s secret mounts with whitespace trimming.
  - Production Security Gate: Prohibits weak placeholders (`change-me`, `default-secret`, `hard-coded-secret`) and refuses server boot if detected in non-test mode.
  - 4-slot thread-local ring buffer preventing buffer clobbering across consecutive calls in the same expression.
  - Registered unit test suite: `test_secret_provider`.

### Changed
- `docker-compose.yml`: Made `MINEFOLIO_JWT_SECRET` mandatory via `${MINEFOLIO_JWT_SECRET:?MINEFOLIO_JWT_SECRET is required}`, preventing containers from launching with fallback secrets.
- `scripts/dev.sh`: Replaced static development secret fallback with on-the-fly 256-bit cryptographically secure random token generation (`openssl rand -hex 32`).
- `backend/src/main.c`: Enforced secret validation during boot sequence via `config_secret_is_valid("JWT_SECRET")`.
- `backend/src/common/jwt.c` and `backend/src/middlewares/jwt_middleware.c`: Replaced `getenv` calls with `config_secret_get`.
- `backend/src/common/db.c`: Replaced `getenv` calls for `DB_DRIVER` and `DB_DSN` with `config_secret_get` using stack buffers; delegated all database schema migrations to `mf_migration_engine_new()` and `mf_migration_apply()`.
- `backend/src/services/auth_service.c`: Migrated JWT and OAuth credentials resolution to `config_secret_get`.
- `backend/src/services/ai_service.c` and `backend/src/controllers/ai_controller.c`: Migrated configuration paths and API key overrides to `config_env_get` and `config_secret_get`.

---

## [1.0.0] - 2026-09-02

### Added
- **Core Financial Platform**:
  - Full asset lifecycle tracking: Cash, bank accounts, stocks, mutual funds, bonds, crypto, real estate, liabilities (loans, credit cards).
  - Liability sign flipping for accurate automated net-worth calculations.
  - Weighted average cost-basis tracking and PnL reporting.
  - Transaction fee cascade rollback via `parent_tx_id` column.
- **Multi-Currency & FX Engine**:
  - Real-time exchange rate sync via Yahoo Finance.
  - Dual-factor foreign exchange gain/loss attribution report (`reports/fx-pnl`).
- **Multi-Ledger Spaces & RBAC**:
  - Collaborative family, personal, and business ledger isolation.
  - Role-based access control (`Owner`, `Editor`, `Viewer`) and invite codes.
- **DCA & Cashflow Calendar**:
  - Dollar-Cost Averaging scheduled executions with one-click buying.
  - 30/90-day cashflow forecast calendar with dividend/rent confirmations.
- **Receipt OCR & Smart Import**:
  - Multimodal AI vision OCR for bills and receipts with offline heuristic rule fallback.
  - Merchant pattern matching and category auto-classification rules.
- **Security & SSO**:
  - End-to-end RSA-OAEP password encryption in frontend before transmission.
  - TOTP 2FA two-factor authentication with QR code generation.
  - GitHub OAuth2 and generic Enterprise OIDC Single Sign-On.
- **Observability**:
  - Integrated `/csilk-admin` dashboard with live RPS metrics, DAG workflow topology visualization, and 100Hz CPU flamegraph profiler.
- **Testing**:
  - 7 automated integration test suites with 134+ test cases verifying end-to-end HTTP and database state.
