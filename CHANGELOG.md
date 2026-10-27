# Changelog

All notable changes to Minefolio will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- **Account-Dimension Asset Management (`backend/sql/migrations/{sqlite,postgres}/V017__accounts_table.sql`, `backend/src/domain/account/`, `application/account/`, `infrastructure/repositories/account_repo_impl.c`, `interfaces/http/controllers/account_controller.c`, `frontend/src/`)**:
  - Introduced independent `accounts` table (V017) as pure container grouping assets per (user, ledger) with institution type, account number, currency, icon, note, manual sort order, and soft-archive flag; backfill migration auto-creates a "默认账户" per ledger and re-parents all existing assets.
  - Full DDD backend stack: domain rules (`mf_account_rule_*`), repository with `asset_count`/`total_value` aggregates, use cases with duplicate-name 1004 and delete-protection (assets/transactions present → 1004, guide to archive), lazy seeding of the default account alongside category seeds, and REST endpoints `GET/POST /api/accounts`, `PUT/DELETE /api/accounts/:id`.
  - Asset chain integration: `asset_list`/`asset_get` LEFT JOIN accounts (`account_name`, `institution`), optional `?account_id=` filter, `asset_insert` accepts `account_id` with default-account fallback for backwards compatibility; AI `asset_tool` gains optional `account_id` filter.
  - Desktop Assets page rewritten as account-card groups (header: icon + institution tag + subtotal + edit/archive; child asset rows), with `AccountDialog.vue` shared by desktop and mobile; mobile Assets page uses collapsible account cards (collapsed by default showing subtotal).
  - All 14 asset-picker entry points label options as `{account_name} · {asset name}`; transaction, DCA, cashflow, import, and transfer flows pick up the account dimension automatically.
  - Mobile offline sync: local sql.js schema adds `accounts` table and `assets.account_id`; `sync.ts` pulls/pushes accounts with `remapAccountRefs()` for offline-created account IDs; `offline-http.ts` maps `/api/accounts` paths.
  - Added accounts coverage in `test_link.sh` (174 assertions total, including archived-account list-filter regression).

- **AI Agent Loop Hardening (`backend/src/services/ai/runtime/{loop,limits}.c`, `backend/src/services/ai_service.c`, `frontend/src/{api/ai.ts,stores/chat.ts,views/Chat.vue}`)**:
  - **Client-Disconnect Cooperative Cancel**: any failed SSE write sets a `cancel_token` shared with the runtime; the loop checks it per turn and per stream chunk, aborts further tool execution, and persists exactly the partial text the client had seen — no ghost full-text rows, no wasted provider calls.
  - **Transient Provider Error Retry**: the runtime retries remaining transient errors (e.g. HTTP 500/504) twice with 0.5s/1.5s backoff, only while no body text has streamed to the client; configuration-class errors (400/401/403/404) and errors already retried inside `csilk_ai_chat` (CURL, 429/502/503) are not re-stormed.
  - **Budget-Exhaustion Wrap-Up**: when turns/tokens/timeout budgets run out, a final tool-less (`tool_choice="none"`) call converges the conversation into a real answer persisted with `finish_reason="budget"`, instead of ending on a bare error.
  - **Duplicate Tool-Call Guard**: identical (name+arguments) tool calls within one run are short-circuited with a `duplicate_call` replay notice and no longer consume the tool budget; oversized tool results are clamped to an 8KB JSON preview before entering model context (full results still reach SSE and persistence).
  - **Typed SSE Errors & Decoupled Frontend Failure UI**: `error` events now carry the runtime error code (e.g. 1001 model) and stream failures no longer overwrite already-streamed content — the store records `msg.error` and the UI renders a separate error strip with a retry action; deleting the active session aborts its in-flight stream; retry now resends the nearest preceding user message instead of the session's first one.
  - **Usage Stats on `done`**: the `done` SSE event carries prompt/completion/total tokens, estimated cost, elapsed time and finish reason; the chat UI renders a per-message usage line (tokens · $ · duration) with a `budget` badge.
  - Added integration test suite `test_ai_loop_hardening.sh` (20 assertions: retry, budget wrap-up + duplicate interception, mid-stream provider death) and frontend unit tests `chat-stream-error.spec.ts` (error decoupling, session-delete abort).

### Fixed
- **AI Settings Instance-Admin Gating (`backend/src/interfaces/http/controllers/ai_controller.c`, `backend/src/infrastructure/repositories/auth_repo_impl.c`)**:
  - `PUT /api/settings/ai` and the `POST /api/settings/ai/test` / `fetch-models` probe endpoints now require the instance admin (earliest registered account, i.e. the `/system/setup` user) via new `mf_auth_repo_is_instance_admin()`; previously any authenticated user could rewrite the global provider config (base_url/api_key) or use the probes as an SSRF surface. `GET /api/settings/ai` stays open to authenticated users (chat model defaults and receipt OCR depend on it, and the payload carries no secret material) and is pinned by a regression assertion.
  - `test_link.sh` case 35 extended with non-admin `1004` assertions on all three endpoints plus a tamper check.
- **Archived Account List Filter (`backend/src/infrastructure/repositories/account_repo_impl.c`)**:
  - `account_list` passed the `archived=0` conditional filter and `ORDER BY` as extra `snprintf` arguments with no matching format specifier, silently dropping both; default list now hides archived accounts and `include_archived=1` includes them.

### Changed
- **Test Suite Expansion**:
  - Expanded integration coverage with `test_ai_loop_hardening.sh` (20 assertions) and frontend `chat-stream-error.spec.ts` (36 suites / 170 tests total).

## [1.3.2] - 2026-10-24

### Added
- **AI Trace Detail Slide-over Drawer Redesign (`frontend/src/views/AiTraceDetail.vue`)**:
  - Replaced cramped 800px modal dialog with a modern right slide-over drawer (`el-drawer`), featuring width adaptability (75% on desktop, 100% on mobile) and a one-click maximize/restore toggle.
  - Added Bento performance & cost metrics bar displaying latency, TTFT, token breakdown (Prompt vs Completion), tok/s generation rate, cost in USD, and sampling parameters.
  - Implemented a two-column split workspace (`.workspace-split`) separating Prompt context cards from Assistant completion with independent vertical scroll containers, eliminating nested scroll conflicts.
  - Added Markdown vs raw text view mode toggle for model completions, structured JSON payload viewer, and single-click copy actions for messages, inputs, outputs, and JSON payloads.
  - Added unit test suite `ai-trace-detail.spec.ts`.
- **Chat Layout Optimization & Decoupling (`frontend/src/views/Chat.vue`, `frontend/src/views/Layout.vue`, `frontend/src/router/index.ts`)**:
  - **Full-Bleed Route Mode**: Introduced `meta.fullBleed: true` for the `/chat` route and `.is-full-bleed` styling in `Layout.vue`, removing default outer padding (`24px 28px`) and outer scrollbars to achieve seamless edge-to-edge docking for the AI workspace.
  - **Integrated Chat Toolbar (3-in-1 Topbar)**: Eliminated redundant top-level `page-header` (~52px vertical height savings). Consolidated model selector, thinking mode badge, stream status, export markdown, copy, and clear controls into an integrated `.chat-toolbar` inside `.chat-main`. Integrated "New Chat" shortcut directly into the session sidebar header.
  - **Flex Natural Input Docking**: Decoupled `.chat-input-area` from fragile `position: absolute; bottom: 18px` into normal vertical flex document flow (`flex-shrink: 0`), completely preventing input card expansion from obscuring trailing messages.
  - **Messages Container Isolation**: Wrapped `.messages` inside a relative `.messages-container` with normal padding (`20px 20px 24px`), anchoring `.scroll-bottom-btn` inside `.messages-container` at `bottom: 14px`.
  - Added unit test suites `layout-fullbleed.spec.ts`, `chat-toolbar.spec.ts`, and `chat-input-layout.spec.ts`.
- **AI Thinking / Reasoning Mode (`backend/sql/migrations/{sqlite,postgres}/V015__ai_messages_thinking_content.sql`, `backend/src/services/ai/`, `frontend/src/`)**:
  - **Database Schema Migration (V015)**: Added `thinking_content TEXT` column to `ai_messages` table across SQLite and PostgreSQL migrations.
  - **AI Runtime Streaming Support**: Enhanced SSE event stream protocol to transmit `data: {"type": "thinking", "content": "..."}` chunks, parsing `reasoning_content` (DeepSeek-R1 / OpenAI o-series) and `<think>` blocks.
  - **Session Repository Persistence**: Updated `ai_session_repo_impl.c` to persist `thinking_content` alongside assistant response messages.
  - **Frontend Streaming & Collapsible Card**: Updated `chat.store.ts` to consume thinking chunks, and implemented a collapsible thinking card in `ChatMessageContent.vue` with smooth typewriter animation, duration counter, and expand/collapse transitions.
  - Added unit test suites `chat-thinking.spec.ts` and `chat-message-thinking.spec.ts`.
- **AI Analysis Panel & 3-Column Studio (`frontend/src/views/Chat.vue`, `frontend/src/components/AnalysisPanel.vue`)**:
  - Introduced responsive 3-column workspace layout with draggable resize handle (`drag-handle`) and drawer fallback on mobile screens.
  - Integrated `AnalysisPanel.vue` for rendering comprehensive financial markdown reports, metric callouts, and Mermaid diagrams without crowding the primary conversation stream.
  - Added unit test suites `analysis-panel.spec.ts`, `chat-layout-panel.spec.ts`, `chat-message-content-panel.spec.ts`, and `chat-panel-extractor.spec.ts`.
- **Transfer Explicit Transactions (`backend/sql/migrations/{sqlite,postgres}/V014__transfer_explicit_transactions.sql`)**:
  - Added `from_tx_id` and `to_tx_id` explicit foreign keys referencing `transactions(id)` in `transfers` table.
  - Rescheduled transfer write sequence so transaction records are created and committed first, then linked to the `transfers` row.
  - Made transfer deletion deterministic via `from_tx_id = ? OR to_tx_id = ?`, with legacy `(from_asset_id, to_asset_id, amount)` attribute matching strictly preserved as backwards-compatibility fallback.
- **Global Market Indices & Minute Sync (`backend/sql/migrations/{sqlite,postgres}/V013__market_indices_table.sql`)**:
  - Added `market_indices` schema table and `/api/market/indices` endpoint for tracking major global market indices and intraday data.
  - Added `test_market_indices` unit test suite.
- **Exchange Rate Safe Semantics & Fallback Warning (`backend/src/services/market/exchange_rate_service.c/.h`)**:
  - Added `exchange_rate_get_to_cny_safe()` returning `-1` on unknown currency or invalid parameters.
  - Added `[WARN]` logging to `exchange_rate_get_to_cny()` fallback to prevent silent currency misconfigurations.
  - Added `test_fx_semantics` unit test suite.
- **Trade Performance Alignment & Fixed-Point Replay (`backend/src/application/portfolio/usecases.c`)**:
  - Migrated `/reports/transaction/performance` calculation to Financial Core fixed-point arithmetic (`money_t`, `quantity_t`, `price_t`, `decimal_t`), aligning with `/reports/holdings` domain rules.
  - Replaced static `trackers[256]` float array with dynamically allocated `asset_perf_tracker_t` array.
  - Added `test_portfolio_pnl_alignment` unit test suite.
- **Deterministic DTO Rounding (`backend/src/common/db.h`)**:
  - Added `mf_round_to_decimals(double val, int decimals)` helper to standardize float JSON DTO output, eliminating IEEE 754 tail residuals.
- **PostgreSQL Prepared Statement Engine (`csilk/src/drivers/db/postgres.c`)**:
  - Implemented full native prepared statement lifecycle in PostgreSQL driver: `?` parameter rewriting to `$1`, `$2`, ..., `PQprepare`, `PQexecPrepared`, parameter type binding, binary/text JSON conversion, and `DEALLOCATE` resource cleanup.
  - Resolved multi-instance concurrency lock failures (`UPDATE schema_migration_lock ... WHERE ... ?`) on PostgreSQL during database startup migrations, enabling flawless PostgreSQL deployments.
- **csilk Framework Feature Downstreaming & Integration (Phases 1-4)**:
  - **JSON Type Extraction (`<csilk/core/json.h>`)**: Adopted `csilk_json_as_int64`, `csilk_json_as_double`, and `csilk_json_as_bool` to safely handle column data types without string-to-number parse gotchas.
  - **Transaction Scope & Savepoint (`<csilk/drivers/db/db.h>`)**: Switched `db_tx_scope_*` to native `csilk_db_tx_scope_*` with full nested savepoint and rollback support.
  - **Database Migration Engine (`<csilk/drivers/db/migration.h>`)**: Downstreamed Flyway-style migration engine (`csilk_migration_*`) supporting discovery, strict version ordering, SHA-256 CRLF normalization, distributed locks, and auto-baseline (`V001`~`V016` at release; `V017` added post-release in [Unreleased]).
  - **CSV Processing (`<csilk/util/csv.h>`)**: Replaced custom CSV utility with RFC 4180 streaming parser and generator (`csilk_csv_*`).
  - **TOTP 2FA Engine (`<csilk/crypto/totp.h>`)**: Replaced internal TOTP implementation with csilk RFC 6238 standard TOTP engine (`csilk_totp_*`).
  - **Middleware Whitelist & Revocation**: Enhanced CSRF and JWT middlewares using csilk native route whitelisting (`csilk_csrf_whitelist_path`) and token revocation callbacks.
- **Database Migration V016 (AI Message Tool Calls)**:
  - Added `backend/sql/migrations/{sqlite,postgres}/V016__ai_messages_tool_calls.sql` introducing `tool_calls TEXT DEFAULT ''` column on `ai_messages` table.
  - Backend `ai_session_repo_impl.c` persists `tool_calls` JSON alongside message content for Agent Loop context reconstruction.
  - Frontend chat store renders persisted tool call state for message rehydration.

### Fixed
- **AI Chat Interaction Correctness (`backend/src/services/ai_service.c`, `backend/src/services/ai/runtime/{loop,loop.h}`, `backend/src/infrastructure/repositories/ai_session_repo_impl.c`, `frontend/src/stores/chat.ts`, `frontend/src/api/ai.ts`)**:
  - **Edit-Resend Server History Truncation**: `POST /api/ai/chat` now accepts `truncate_from_id` and deletes that message plus every later row *before* loading context, eliminating duplicate user rows and ghost replies that previously resurfaced after reload; the SSE stream returns the persisted `user_message_id` / `assistant_message_id` so the frontend addresses messages by real database id (`server_id`).
  - **Regenerate Delete Semantics**: `mf_ai_message_delete_last_assistant` now removes only assistant rows that follow the *last user row*, so a dangling user row from a failed turn can no longer cause the previous turn's answer to be deleted.
  - **Double-Submit Race Guard**: `sendMessage` / `regenerateLastMessage` / `editAndResend` / `runWorkflow` set `isStreaming` synchronously at entry, closing the `await createNewSession()` window in which a double Enter press could dispatch two concurrent turns.
  - **Tool-Call Live Re-render**: chat message `v-memo` now tracks `tool_calls` length and last result, so "executing → done" state flips appear immediately during streaming.
  - Added integration test suite `test_ai_truncate.sh` (14 assertions).
- **Portfolio Trade Events Fee Child Filtering (`backend/src/infrastructure/repositories/portfolio_repo_impl.c`)**:
  - Added `AND (parent_tx_id IS NULL OR parent_tx_id = 0)` in `mf_portfolio_repo_get_trade_events` to filter out child fee rows from holding PnL calculations.
- **Database Error Propagation in Position Updates (`backend/src/common/balance.c`)**:
  - Modified `apply_position_fc` and `rollback_position_fc` to return `-2` on DB update failure instead of silently returning 0, ensuring failure propagates to `ledger_apply_tx` for transaction rollback.
- **Transactional Scopes on Quote Sync & Asset Updates (`backend/src/application/market/usecases.c`, `backend/src/application/asset/usecases.c`)**:
  - Enclosed multi-step writes in `db_tx_scope_begin()` / `db_tx_scope_commit()`, checked `balance_apply_delta` return codes, and triggered rollback on error.
- **Strict HTTP Parameter Validation**:
  - Replaced ~70 instances of `atoll()` with `parse_query_int_strict()` across HTTP controllers (`transaction`, `ledger`, `asset`, `daily_expense`, `dca`, `ai_mcp`, `category`), rejecting non-numeric inputs with HTTP 1002 Bad Request.

### Removed
- **Redundant Glue Code**:
  - Removed internal migration engine `backend/src/infrastructure/database/migration/` (611+ lines).
  - Removed `backend/src/common/csv_utils.c/.h` and `backend/src/common/totp.c/.h` (~600 lines), reducing codebase footprint by ~2000 lines.

### Changed
- **Test Suite Expansion**:
  - Expanded CTest unit test suite matrix from 28 to 35 suites (added `test_market_indices`, `test_fx_semantics`, `test_portfolio_pnl_alignment`), achieving 100% pass rate.
  - Expanded integration test suite `test_link.sh` to 39 test cases (151 assertions).

- **Version Bump**: Bumped version to `1.3.2` across backend (`CMakeLists.txt`, `admin_controller.c`), frontend (`package.json`, `package-lock.json`), Android (`build.gradle`), tests (`http.utils.spec.ts`, `mcp-settings.spec.ts`), and documentation (`README.md`, `docs/architecture.md`, `CHANGELOG.md`).

---

## [1.3.1] - 2026-09-12

### Added
- **Mobile Server Address Configuration & Connectivity Testing (`frontend/src/`)**:
  - **Dynamic Server Address Resolution (`src/utils/server-url.ts`)**: Added comprehensive server URL utility managing format normalization (auto protocol prefix inference `http://` / `https://` and trailing slash trimming), validation (`isValidServerUrl`), and `localStorage` persistence (`minefolio_server_url`) with seamless fallback to `VITE_API_URL` or relative paths.
  - **Live Connectivity & Latency Probing (`testServerConnection`)**: Integrated diagnostic tool querying `/api/system/status` with a 6-second timeout, measuring round-trip latency in milliseconds, detecting Minefolio server version, and providing detailed error diagnostics.
  - **Dynamic Axios Re-binding (`src/utils/http.ts`)**: Wired Axios instance default `baseURL` and request interceptors to automatically react to server address mutations in real time without requiring application restart.
  - **RSA Public Key Cache Invalidation (`src/utils/crypto.ts`)**: Listens to server address change events and invalidates cached RSA-OAEP public keys, guaranteeing that authentication credentials are encrypted using the target server's active key.
  - **Reactive Composable (`src/composables/useServerUrl.ts`)**: Created reactive composable synchronizing server URL state (`serverUrl`, `isCustom`, `displayUrl`) across views and components.
  - **Mobile Server Config Dialog (`src/components/mobile/ServerConfigDialog.vue`)**: Implemented mobile-tailored modal providing server address editing, real-time connectivity testing with status badges, and one-click reset to default.
  - **Mobile Login Screen Integration (`src/views-mobile/LoginMobile.vue`)**: Added server status pill bar on the login view allowing pre-login server configuration and guidance on network connection errors.
  - **Mobile Settings Page Integration (`src/views-mobile/SettingsMobile.vue`)**: Added dedicated server address settings card with live ping test and safeguard confirmation dialog when offline records are pending sync.
  - **Unit Test Suites**: Added unit tests in `http.utils.spec.ts`, `use-server-url.spec.ts`, and `login-mobile.spec.ts`.

### Changed
- **Version Bump**: Bumped version to `1.3.1` across backend (`CMakeLists.txt`, `admin_controller.c`), frontend (`package.json`, `package-lock.json`), Android (`build.gradle`), and documentation (`README.md`, `docs/architecture.md`, `CHANGELOG.md`).

---

## [1.3.0] - 2026-09-11

### Added
- **DCA Plans Pagination & Aggregate Metrics (`backend/src/domain/dca/`, `application/dca/`, `infrastructure/repositories/dca_repo_impl.c`)**:
  - Full end-to-end backend pagination (`page`, `page_size`, `status`) across all DDD architecture layers.
  - New aggregate financial indicators: `active_plan_count`, `total_invested_amount`, `total_current_value`, `total_pnl`, `total_pnl_pct`.
  - Frontend `Plans.vue` integrated with status tab filtering (`all`, `active`, `paused`, `completed`), pagination controls, and reactive summary cards.
- **Bento Grid Studio & Comprehensive UI Modernization (`frontend/src/views/`)**:
  - **Dashboard**: Modern Bento Grid layout featuring a Hero Net-Worth card with an integrated multi-currency strip, Bloomberg-style asset breakdown donut with centered total metrics, modern area gradient net worth trend charts, and rounded gradient yearly income/expense bars.
  - **Ledger & Transactions**: Upgraded with stream tables, interactive hero amount inputs with type toggles, active row indicator accents, category pills, and streamlined filter bars.
  - **Holdings**: Bloomberg-style symbol display cells, visual weight allocation progress bars, and PnL status badges.
  - **SummaryCard Component**: Enhanced with glowing accent bars, trend capsules, and hover lift micro-interactions.
  - **Navigation & Layout**: Added active menu indicator bars, glowing quick-add CTA buttons, and smooth page fade transitions.
- **AI Chat Studio Modernization (`frontend/src/views/Chat.vue`)**:
  - Centered conversation stream layout with refined AI/User avatars and streaming micro-interactions.
  - Floating island input capsule with integrated financial workflows and action buttons.
  - Streamlined header bar and polished collapsible session sidebar.
  - Enhanced financial markdown tables, callout blocks, action card typography, and code block copy interactions.
  - Bento prompt starter cards with subtle hover lift for empty conversation states.
- **Brand Identity & AppLogo Redesign (`frontend/src/components/AppLogo.vue`, `frontend/public/`, `frontend/android/`)**:
  - Brand identity upgrade featuring a 3D faceted 'M' monogram with an upward momentum surge trendline.
  - Reusable `AppLogo.vue` component with gradient facets and glow effects.
  - Regenerated web favicon (`favicon.svg`, `favicon.ico`, multi-res PNGs, and PWA icons).
  - Unified Phosphor Duotone navigation iconography across desktop and mobile layouts.
  - Regenerated Android launcher icons, adaptive icons, and splash screens.
- **Database Migration V009 (Savings Goals)**:
  - Added schema migration `V009__savings_goals.sql` for SQLite and PostgreSQL.

### Changed
- **Full-Height Flex Table Layout Alignment**:
  - Aligned **Assets Table** (`Assets.vue`) and **DCA Plans Table** (`Plans.vue`) with **Transactions Table** (`Transactions.vue`).
  - Implemented vertical flex layout (`height: 100%; overflow: hidden; display: flex; flex-direction: column;`), extending tables to fill available page height down to the bottom with internal scrolling, and pinning pagination bars at the bottom right.
- **Version Bump**:
  - Bumped version to `1.3.0` across backend (`CMakeLists.txt`, `admin_controller.c`), frontend (`package.json`, `package-lock.json`), Android (`build.gradle`), and documentation (`README.md`, `AGENTS.md`, `docs/architecture.md`).

### Fixed
- **Multi-Tenant & Multi-Ledger Data Isolation**:
  - Strictly enforced `user_id` and `ledger_id` isolation in AI monthly expense reports and financial reports (`reports.c`, `ai_repo_impl.c`).
- **Receipt OCR Multipart Thread Safety**:
  - Made receipt multipart parser state thread-local (`receipt_ocr_service.c`), eliminating cross-thread heap corruption during concurrent receipt uploads.
- **Theme & Display Consistency**:
  - Fixed contrast and background color issues under light theme across Dashboard, Summary Cards, and Modals.
  - Resolved button tooltip display anomalies under dark and light themes.
  - Fixed login card displacement caused by unpositioned background particle canvas.
- **Test Suite Determinism**:
  - Added offline market mock driver for deterministic market test suite execution.
  - Fixed ledger_id propagation in category tree unit tests.

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
