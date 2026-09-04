# Changelog

All notable changes to Minefolio will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

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
