# Minefolio Fullstack Audit Remediation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remediate all verified Critical, Major, and Minor defects across Minefolio's C23 backend, Vue 3 desktop/mobile frontend, PostgreSQL/SQLite migrations, IM gateway, and Docker/Nginx/CI infrastructure in 31 atomic, independently tested commits.

**Architecture:** Fixes are ordered into 5 waves: Wave 1 (C backend security, memory safety, signal safety, and deadlock elimination), Wave 2 (C backend financial/ledger precision, RBAC, and auth hardening), Wave 3 (cross-stack CSRF/JSON/SSE/Blob contracts, dual-engine migrations, gateway, and deployment/CI), Wave 4 (Vue 3 async epoch guards, lifecycle cleanup, and mobile offline sync), and Wave 5 (C backend DDD boundary cleanup).

**Tech Stack:** C23 (csilk, libcurl, OpenSSL, SQLite3, libpq, ASan/UBSan/LSan), Vue 3 + TypeScript + Pinia + Vitest + sql.js WASM, Node.js 22 (IM gateway), Docker / Nginx / GitHub Actions.

**Spec:** Fullstack code audit conducted in conversation on 2026-11-27.

## Global Constraints

- **Isolated Build & Test (MANDATORY):** Never run bare `cmake --build --parallel`, bare `ctest`, or uncapped `vitest`. Every build and test command MUST run through `./backend/tests/run_isolated.sh` with explicit concurrency and memory caps:
  - CMake configure (if needed): `./backend/tests/run_isolated.sh -m 4G -j 2 -- cmake -S backend -B backend/build-asan -G "Unix Makefiles" -DMINEFOLIO_SANITIZE=ON`
  - C build (ASan): `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan`
  - C targeted test: `./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/<test_target>`
  - C full unit suite: `./backend/tests/run_isolated.sh -m 4G -j 2 --rss 1536 unit backend/build-asan`
  - C integration test: `MINEFOLIO_BUILD_DIR="$PWD/backend/build-asan" ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- bash backend/tests/<script>.sh`
  - Frontend targeted vitest: `NODE_OPTIONS="--max-old-space-size=2048" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend exec -- vitest run --config vite.config.mobile.ts --pool=forks --poolOptions.forks.maxForks=2 tests/<spec>.spec.ts`
  - Frontend full vitest: `NODE_OPTIONS="--max-old-space-size=3072" ./backend/tests/run_isolated.sh -m 8G -- npm --prefix frontend exec -- vitest run --config vite.config.mobile.ts --pool=forks --poolOptions.forks.maxForks=2`
  - Frontend build / mobile build: `NODE_OPTIONS="--max-old-space-size=3072" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend run build` (or `build:mobile`)
  - Gateway test: `NODE_OPTIONS="--max-old-space-size=1024" ./backend/tests/run_isolated.sh -m 2G -- npm --prefix gateway test`
- **C DDD Signature Convention:** Preserve `void* pool` in `application/*/usecases.h` (intentional decoupling); cast `(csilk_db_pool_t*)pool` inside `.c`.
- **csilk DB Stringification Contract:** `csilk_db_query_param_json` serializes all DB columns as string scalars. Always use `db_get_int`, `db_get_num`, `db_get_bool`, `db_get_decimal` from `common/db.h`; never call `csilk_json_get_number()` or `csilk_json_get_bool()` on DB rows.
- **Borrowed View Ownership:** `csilk_json_get` and `csilk_json_array_get` return borrowed views; always `csilk_json_copy()` before passing to `respond_ok()` or attaching to another document.
- **Migration Immutability:** Never modify existing `V001`..`V039` migration files (would break SHA-256 checksums); new schema parity changes go in `V040`.
- **Commit Hygiene:** One commit per task, English Conventional Commits + gitmoji matching `AGENTS.md`, explicit `git add <files>`, never stage incidental `components.d.ts` / `components.mobile.d.ts` churn, never push.

## Review Focus

1. PostgreSQL vs SQLite boolean string scalars (`"t"`/`"f"` vs `"1"`/`"0"`) parsed via `db_get_bool` in C and normalized before reaching JS/Gateway (`Task 9`, `Task 17`).
2. Cross-currency transfers (e.g., USD -> CNY) created and deleted via `DELETE /api/transactions/:id`, plus backdated AI entries invalidating `ledger_snapshots` from entry date (`Task 8`, `Task 11`).
3. In-flight async fetches resolving after logout or ledger switch in Pinia stores, `useFinancialEntities`, and `SyncEngine` without contaminating new session/ledger state (`Task 25`, `Task 27`).
4. Mobile degraded mode (`localDb === null`) and nested multi-level category trees during `SyncEngine.pullRemote()` (`Task 23`, `Task 26`).
5. `ENABLE_CSRF=1` + Nginx reverse proxy combination for OAuth login, `/mcp`, `/api/ai/workflows/run` SSE, 25MB uploads, and per-IP rate limiting (`Task 16`, `Task 21`).

---

## Wave 1: C Backend High-Risk Security, Memory & Concurrency

### Task 1: Restrict MCP `secret_ref`, block `config_secret_get` path traversal, and prevent CRLF header injection

**Files:**
- Modify: `backend/src/config/secret.c:236-256`
- Modify: `backend/src/services/ai/tools/mcp/mcp_client.c:133-198`
- Modify: `backend/src/application/mcp/usecases.c:180-245`
- Modify: `backend/src/interfaces/http/controllers/ai_mcp_controller.c:56-149`
- Test: `backend/tests/unit/test_security_audit_regressions.c`

**Interfaces:**
- Produces: `bool mf_mcp_secret_ref_valid(const char* ref)` (declared in `application/mcp/usecases.h` or `domain/mcp/rules.h`) allowing only `MCP_SECRET_[A-Za-z0-9_]+`.

- [ ] **Step 1: Write failing security regression tests in `backend/tests/unit/test_security_audit_regressions.c`**
  - Test `config_secret_get("../../etc/passwd", ...)` and `config_secret_get("foo/bar", ...)` return `NULL`.
  - Test `mf_mcp_secret_ref_valid("JWT_SECRET")` -> `false`, `mf_mcp_secret_ref_valid("MCP_SECRET_../x")` -> `false`, `mf_mcp_secret_ref_valid("MCP_SECRET_GITHUB_TOKEN")` -> `true`.
  - Test `mf_mcp_usecase_create` rejects non-admin setting `secret_ref` and rejects non-`MCP_SECRET_` `secret_ref` even for admin.
  - Test header value containing `\r` or `\n` is rejected in `mcp_client` header assembly.
- [ ] **Step 2: Run targeted test to confirm RED**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_security_audit_regressions`
- [ ] **Step 3: Implement validation & cleansing**
  - In `backend/src/config/secret.c:236-256`, validate every character of `pure_name` matches `[A-Za-z0-9_-]`; otherwise return `NULL`.
  - Add `mf_mcp_secret_ref_valid` requiring prefix `MCP_SECRET_` followed by >= 1 chars in `[A-Za-z0-9_]`.
  - In `application/mcp/usecases.c` (`mf_mcp_usecase_create` / `update`) and `ai_mcp_controller.c`: reject non-admin users supplying non-empty `secret_ref` (1004) and reject invalid `secret_ref` (1002).
  - In `mcp_client.c:http_call`, re-check `mf_mcp_secret_ref_valid(s->secret_ref)`, check `!strpbrk(t, "\r\n")`, widen `hdr` buffer to 600 bytes, and call `OPENSSL_cleanse(tok, sizeof(tok))`. In `append_header_json`, skip/reject any key or value containing `\r` or `\n`.
- [ ] **Step 4: Run targeted test to confirm GREEN**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_security_audit_regressions`
- [ ] **Step 5: Commit**
  - `git add backend/src/config/secret.c backend/src/services/ai/tools/mcp/mcp_client.c backend/src/application/mcp/usecases.c backend/src/interfaces/http/controllers/ai_mcp_controller.c backend/src/domain/mcp/rules.h backend/src/domain/mcp/rules.c backend/tests/unit/test_security_audit_regressions.c`
  - `git commit -m "fix(security): 🐛 restrict mcp secret_ref allowlist and block secret path traversal"`

### Task 2: Fix deterministic UAF in `mcp_bridge_dispatch` and Double-Free in `api_ai_news_get_date`

**Files:**
- Modify: `backend/src/services/ai/tools/mcp/mcp_bridge.c:581-596`
- Modify: `backend/src/interfaces/http/controllers/ai_news_controller.c:82-92`
- Test: `backend/tests/unit/test_policy_mcp_risk.c`, `backend/tests/unit/test_ai_news_privacy.c`

- [ ] **Step 1: Add regression assertions to `test_policy_mcp_risk.c` and `test_ai_news_privacy.c`**
  - Trigger policy-denied `mcp_bridge_dispatch` call under ASan and verify JSON `"error":"policy_denied"` + non-empty `"reason"`.
  - Exercise `ai_news_get_by_date_ledger` + `csilk_json_copy(csilk_json_array_get(result, 0))` / `api_ai_news_get_date` path under ASan.
- [ ] **Step 2: Run targeted tests to observe failure under ASan**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan`
  - `./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_policy_mcp_risk`
- [ ] **Step 3: Implement fixes**
  - In `mcp_bridge.c:581-596`, copy `decision->reason` into `char reason_buf[256]` before calling `ai_policy_decision_free(decision)`, then pass `reason_buf` to `csilk_json_add_string`.
  - In `ai_news_controller.c:89-91`, deep-copy `csilk_json_t* first = csilk_json_copy(csilk_json_array_get(result, 0));`, call `csilk_json_free(result);`, then `respond_ok(c, first);`.
- [ ] **Step 4: Run targeted tests under ASan to confirm GREEN**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_policy_mcp_risk && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_ai_news_privacy`
- [ ] **Step 5: Commit**
  - `git add backend/src/services/ai/tools/mcp/mcp_bridge.c backend/src/interfaces/http/controllers/ai_news_controller.c backend/tests/unit/test_policy_mcp_risk.c backend/tests/unit/test_ai_news_privacy.c`
  - `git commit -m "fix(ai): 🐛 fix heap use-after-free in mcp_bridge and double-free in ai_news"`

### Task 3: Fix OOM Double-Free in `loop_dispatch_parallel_tools`, `/mcp` heap leak, and `ai_chat_handler` refcount leak

**Files:**
- Modify: `backend/src/services/ai/runtime/loop_dispatch.c:282-311`
- Modify: `backend/src/interfaces/http/controllers/mcp_server_controller.c:50-58`
- Modify: `backend/src/services/ai_service.c:576-589`
- Test: `backend/tests/unit/test_ai_parallel_tools.c`, `backend/tests/unit/test_mcp_unauth_protocol.c`

- [ ] **Step 1: Inspect and add regression test for `mcp_send_response` with numeric and null `id` under LSan**
  - Verify `test_mcp_unauth_protocol.c` exercises JSON-RPC response formatting with non-null `id` and `null` `id` with `detect_leaks=1`.
- [ ] **Step 2: Apply fixes**
  - In `loop_dispatch.c:286-287`, set `threads = NULL; spawned = NULL;` immediately after `free(threads); free(spawned);` before `goto parallel_done;`.
  - In `mcp_server_controller.c:52`, change `if (id && id != csilk_json_null())` to `if (id && !csilk_json_is_null(id))`.
  - In `ai_service.c:577-588`, add `ai_config_release(cfg);` before both early `return;` statements (`sid <= 0` and `!sess`).
- [ ] **Step 3: Build & run unit + MCP integration tests under ASan/LSan**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan`
  - `./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_ai_parallel_tools`
  - `./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_mcp_unauth_protocol`
  - `MINEFOLIO_BUILD_DIR="$PWD/backend/build-asan" ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- bash backend/tests/test_mcp_server.sh`
- [ ] **Step 4: Commit**
  - `git add backend/src/services/ai/runtime/loop_dispatch.c backend/src/interfaces/http/controllers/mcp_server_controller.c backend/src/services/ai_service.c backend/tests/unit/test_mcp_unauth_protocol.c`
  - `git commit -m "fix(ai): 🐛 fix double-free in parallel dispatch and leaks in mcp/chat handlers"`

### Task 4: Eliminate AB-BA deadlock between `s_category_seed_mutex` / `s_account_seed_mutex` and `g_tx_mutex`

**Files:**
- Modify: `backend/src/application/category/usecases.c:111-268`
- Modify: `backend/src/application/account/usecases.c:585-620`
- Test: `backend/tests/unit/test_category_usecases.c`, `backend/tests/unit/test_account_usecases.c`

- [ ] **Step 1: Write concurrent lock-order regression test in `test_category_usecases.c` and `test_account_usecases.c`**
  - Spawn two threads for 50 iterations: Thread A calls `category_usecase_seed_defaults` directly; Thread B acquires `db_tx_scope_begin` first and calls `category_usecase_seed_defaults` + `account_usecase_seed_default` inside the tx scope. Assert completion without deadlock (using a 5s watchdog / timeout join).
- [ ] **Step 2: Run tests to verify deadlock reproduction (or inspect baseline)**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan`
  - `./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_category_usecases`
- [ ] **Step 3: Refactor mutex scope in `category/usecases.c` and `account/usecases.c`**
  - Lock `s_category_seed_mutex` ONLY around in-memory `cat_seed_cache_contains()` and `cat_seed_cache_add()`.
  - Release `s_category_seed_mutex` BEFORE calling `mf_category_repo_is_seeded` or `db_tx_scope_begin`.
  - Inside `db_tx_scope_begin`, re-check `mf_category_repo_is_seeded` so concurrent callers serialize cleanly on `g_tx_mutex` / DB transaction without AB-BA inversion.
  - Apply the same cache-only locking pattern to `s_account_seed_mutex` in `account_usecase_seed_default`.
- [ ] **Step 4: Run tests to confirm GREEN**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_category_usecases && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_account_usecases`
- [ ] **Step 5: Commit**
  - `git add backend/src/application/category/usecases.c backend/src/application/account/usecases.c backend/tests/unit/test_category_usecases.c backend/tests/unit/test_account_usecases.c`
  - `git commit -m "fix(core): 🐛 eliminate ab-ba lock inversion deadlock between seed mutexes and db tx scope"`

### Task 5: Make `main.c` signal shutdown async-signal-safe and fix `scheduler_engine` stop ordering

**Files:**
- Modify: `backend/src/main.c:57-69,244-268`
- Modify: `backend/src/core/scheduler/scheduler_engine.c:868-879,1099-1127`
- Test: `backend/tests/unit/test_scheduler_engine.c`

- [ ] **Step 1: Add shutdown-race test in `test_scheduler_engine.c`**
  - Call `on_market_event_received` / `scheduler_event_publish_*` concurrently while calling `scheduler_engine_stop()`. Verify zero ASan/NULL-deref failures.
- [ ] **Step 2: Implement async-signal-safe self-pipe and safe shutdown order**
  - In `backend/src/main.c`: create a non-blocking `g_sig_pipe[2]`; `handle_shutdown_signal(int sig)` only writes 1 byte (`write(g_sig_pipe[1], &b, 1)`). Spawn a lightweight watcher thread that `poll()`s `g_sig_pipe[0]` and calls `csilk_server_stop(server)` outside signal context. Before `csilk_app_free(app)`, reset `signal(SIGTERM, SIG_DFL); signal(SIGINT, SIG_DFL);`, wake & join the watcher thread, close pipe FDs, and clear `g_app = NULL`.
  - In `main.c:255-256`: stop `market_scheduler_stop()` BEFORE `scheduler_engine_stop()`.
  - In `scheduler_engine.c:scheduler_engine_stop`: call `scheduler_event_reset()` FIRST before destroying `s_worker_pool` or nulling `s_db_pool`. In `on_market_event_received`, acquire `s_engine_lock` to snapshot `s_db_pool` and `s_worker_pool` safely (and unlock before DB query).
- [ ] **Step 3: Build & test under ASan**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_scheduler_engine`
  - Smoke-test `./backend/build-asan/minefolio` startup + double `SIGTERM` shutdown returning exit code 0 cleanly.
- [ ] **Step 4: Commit**
  - `git add backend/src/main.c backend/src/core/scheduler/scheduler_engine.c backend/tests/unit/test_scheduler_engine.c`
  - `git commit -m "fix(core): 🐛 make signal shutdown async-signal-safe and fix scheduler stop order"`

### Task 6: Release MCP stdio pool lock before process teardown and enforce monotonic stdio deadlines

**Files:**
- Modify: `backend/src/services/ai/tools/mcp/mcp_stdio_pool.c:135-148,255-295`
- Modify: `backend/src/services/ai/tools/mcp/mcp_client.c:790-985,1080-1105`
- Test: `backend/tests/unit/test_mcp_pool.c`, `backend/tests/unit/test_mcp_client_io.c`

- [ ] **Step 1: Add deadline & shutdown regression tests in `test_mcp_pool.c` and `test_mcp_client_io.c`**
  - Test `stdio_call` against a mock child that writes 1 stderr byte every 50ms with `timeout_ms = 200`; assert `stdio_call` times out near 200ms rather than resetting forever.
  - Test `mf_mcp_stdio_pool_shutdown()` while one client is `in_use`, followed by `mf_mcp_stdio_pool_release()`; assert zero UAF / double-free under ASan.
- [ ] **Step 2: Run tests to confirm RED**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_mcp_pool && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_mcp_client_io`
- [ ] **Step 3: Implement fixes**
  - In `mcp_stdio_pool.c`: detach dead/evicted `mf_mcp_client_t*` pointers into a local array while holding `s_pool_lock`, unlock `s_pool_lock`, then call `mf_mcp_client_free()`. In `shutdown`, set `s_shutting_down = true`, detach only `!in_use` slots, and in `release()` destroy any returned client when `s_shutting_down` is true.
  - In `mcp_client.c:stdio_call` & `mf_mcp_client_initialize`: compute `CLOCK_MONOTONIC` deadline (`deadline_ms`), write via `poll(POLLOUT)` + non-blocking/bounded loop using precomputed `frame_len`, and pass `remaining_ms` to `poll()`. In `mf_mcp_client_free`, break on `waitpid(...) == -1 && errno == ECHILD` and retry `waitpid(pid, &st, 0)` on `EINTR`.
- [ ] **Step 4: Run tests to confirm GREEN**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_mcp_pool && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_mcp_client_io`
- [ ] **Step 5: Commit**
  - `git add backend/src/services/ai/tools/mcp/mcp_stdio_pool.c backend/src/services/ai/tools/mcp/mcp_client.c backend/tests/unit/test_mcp_pool.c backend/tests/unit/test_mcp_client_io.c`
  - `git commit -m "fix(ai): 🐛 release mcp pool lock before teardown and enforce monotonic stdio deadlines"`

### Task 7: Harden `file_parser.c` child execution and move DB queries off `market_scheduler` lock

**Files:**
- Modify: `backend/src/services/file_parser.c:60-168`
- Modify: `backend/src/services/market/market_scheduler.c:209-295,347-360`
- Test: `backend/tests/unit/test_file_parser_mkstemps.c`

- [ ] **Step 1: Add test cases in `test_file_parser_mkstemps.c`**
  - Test `file_parser_write_temp` handles full buffer writes and `run_tool_capture` (via a helper or slow/stalled command test) respects timeout and cleans up child process without zombie leak.
- [ ] **Step 2: Implement fixes**
  - In `file_parser.c`: loop `write()` in `file_parser_write_temp` on partial writes and `EINTR`; in `run_tool_capture` child (`pid == 0`), restore `signal(SIGPIPE, SIG_DFL)` and close inherited FDs `>= 3` (via `/proc/self/fd` or `sysconf(_SC_OPEN_MAX)` fallback); in parent, read pipe using `poll` + `CLOCK_MONOTONIC` 15s deadline, `kill(pid, SIGKILL)` on timeout, and retry `waitpid(pid, &st, 0)` on `EINTR`.
  - In `market_scheduler.c`: snapshot `g_pool` under `g_sched_mutex`, unlock `g_sched_mutex` before calling `mf_scheduler_repo_is_builtin_enabled` and `exchange_rate_preload_from_db`, then re-lock and re-check `g_sched_running`.
- [ ] **Step 3: Build & test under ASan**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_file_parser_mkstemps`
- [ ] **Step 4: Commit**
  - `git add backend/src/services/file_parser.c backend/src/services/market/market_scheduler.c backend/tests/unit/test_file_parser_mkstemps.c`
  - `git commit -m "fix(core): 🐛 harden file_parser child execution and move db queries off market_scheduler lock"`

---

## Wave 2: C Backend Financial Integrity, RBAC & Auth Hardening

### Task 8: Persist `fx_rate_to_cny` on cross-currency transfer rows and harden transfer reversal

**Files:**
- Modify: `backend/src/infrastructure/repositories/transfer_repo_impl.h`
- Modify: `backend/src/infrastructure/repositories/transfer_repo_impl.c:95-155`
- Modify: `backend/src/application/transfer/usecases.c`
- Modify: `backend/src/core/ledger/ledger_engine.c:845-860`
- Test: `backend/tests/unit/test_fx_semantics.c`

- [ ] **Step 1: Write failing regression test in `test_fx_semantics.c`**
  - Seed USD asset (balance 1000) and CNY asset (balance 0), set USD->CNY rate = `7.20`. Call `transfer_usecase_create` for `100 USD` from USD asset to CNY asset, assert CNY balance becomes `720.00`, then delete the `transfer_out` transaction via `tx_usecase_delete`, and assert USD balance is `1000.00` and CNY balance returns to `0.00` (not `620.00`).
- [ ] **Step 2: Run test to verify RED**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_fx_semantics`
- [ ] **Step 3: Implement `fx_rate_to_cny` persistence & legacy fallback**
  - Update `mf_transfer_repo_insert_out_transaction` and `mf_transfer_repo_insert_in_transaction` (or add `_fx` parameter) to persist `fx_rate_to_cny` in `transactions`.
  - In `transfer_usecase_create`, compute `fx_rate_to_cny` (`1.0` for CNY, strict date-aware rate via `exchange_rate_get_to_cny_at` for non-CNY) and pass it into both `transfer_out` and `transfer_in` inserts.
  - In `ledger_engine.c:ledger_reverse_tx`, if `strcasecmp(currency, "CNY") != 0 && fabs(fx_rate - 1.0) < 1e-9`, treat `fx_rate = 0.0` so `balance_convert_money_fx_checked` resolves the historical rate for `tx_date` instead of applying 1:1.
- [ ] **Step 4: Run tests to confirm GREEN**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_fx_semantics`
  - `MINEFOLIO_BUILD_DIR="$PWD/backend/build-asan" ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- bash backend/tests/test_link.sh`
- [ ] **Step 5: Commit**
  - `git add backend/src/infrastructure/repositories/transfer_repo_impl.h backend/src/infrastructure/repositories/transfer_repo_impl.c backend/src/application/transfer/usecases.c backend/src/core/ledger/ledger_engine.c backend/tests/unit/test_fx_semantics.c`
  - `git commit -m "fix(transaction): 🐛 persist fx_rate_to_cny on cross-currency transfer rows"`

### Task 9: Fix smart import rule `db_get_bool` parsing and validate CSV import amounts

**Files:**
- Modify: `backend/src/services/import_service.c:60-72,512-540,775-828`
- Modify: `backend/src/application/import_export/receipt_usecases.c:355-365`
- Test: `backend/tests/unit/test_domain_repositories.c`

- [ ] **Step 1: Write failing test in `test_domain_repositories.c`**
  - Insert an `import_rules` row with `is_active = 1` (string `"1"` / `"t"` from DB query), verify `db_get_bool` returns `1` whereas `"0"` / `"f"` returns `0`, and verify CSV import rejects negative, zero, `NaN`, and non-numeric `amount` rows.
- [ ] **Step 2: Run test to verify RED**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_domain_repositories`
- [ ] **Step 3: Implement fixes**
  - Replace `csilk_json_get_bool(r, "is_active")` with `db_get_bool(r, "is_active")` in `services/import_service.c:65` and `application/import_export/receipt_usecases.c:358`.
  - In `import_transactions_csv_execute` and `import_daily_expenses_csv_execute`, parse `amount_s` with `strtod(amount_s, &endptr)` checking `endptr != amount_s && *endptr == '\0' && isfinite(amount) && amount > 0.0`, skipping invalid rows with error count instead of inserting.
- [ ] **Step 4: Run tests to confirm GREEN**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_domain_repositories`
- [ ] **Step 5: Commit**
  - `git add backend/src/services/import_service.c backend/src/application/import_export/receipt_usecases.c backend/tests/unit/test_domain_repositories.c`
  - `git commit -m "fix(transaction): 🐛 use db_get_bool for smart import rules and validate csv amounts"`

### Task 10: Fix missing `=` padding in `receipt_controller.c:base64_encode`

**Files:**
- Modify: `backend/src/interfaces/http/controllers/receipt_controller.c:80-95`
- Test: `backend/tests/unit/test_security_audit_regressions.c`

- [ ] **Step 1: Add RFC 4648 base64 padding test vectors (`""`, `"f"`, `"fo"`, `"foo"`, `"foob"`, `"fooba"`, `"foobar"`) to `test_security_audit_regressions.c`**
  - Export/declare `char* receipt_base64_encode(const unsigned char* data, size_t input_length, size_t* output_length)` or test the helper directly.
- [ ] **Step 2: Run test to verify RED**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_security_audit_regressions`
- [ ] **Step 3: Fix `base64_encode` in `receipt_controller.c`**
  - Track `size_t rem = input_length - i;` per 3-byte group and emit `'='` when `rem <= 1` (3rd char) or `rem <= 2` (4th char).
- [ ] **Step 4: Run test to confirm GREEN**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_security_audit_regressions`
- [ ] **Step 5: Commit**
  - `git add backend/src/interfaces/http/controllers/receipt_controller.c backend/tests/unit/test_security_audit_regressions.c`
  - `git commit -m "fix(ai): 🐛 fix missing padding in receipt multipart base64 encoder"`

### Task 11: Enforce account RBAC, V014 transfer pairing, and dated snapshot invalidation in AI action confirmations

**Files:**
- Modify: `backend/src/interfaces/http/controllers/ai_controller.c:356-399`
- Modify: `backend/src/services/ai/tools/expense_tool.c:280-328`
- Modify: `backend/src/services/ai/tools/transfer_tool.c:178-215`
- Modify: `backend/src/services/ai/tools/batch_tool.c:714-830`
- Test: `backend/tests/unit/test_ai_action_confirm.c`, `backend/tests/unit/test_ai_batch_confirmation.c`, `backend/tests/unit/test_account_rbac.c`

- [ ] **Step 1: Add failing RBAC + V014 pairing + backdated snapshot tests**
  - Test shared-ledger `editor` with `permission_level='read'` on target account is rejected (`1004`) by AI expense/transfer/batch confirm.
  - Test AI transfer confirm creates paired `transfer_out` + `transfer_in` rows in `transactions` linked to `transfers.from_tx_id` / `to_tx_id`.
  - Test backdated AI expense confirm invalidates `ledger_snapshots` from `date` (not just today).
- [ ] **Step 2: Run tests to verify RED**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan`
  - `./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_ai_action_confirm`
  - `./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_ai_batch_confirmation`
- [ ] **Step 3: Implement fixes**
  - In `ai_controller.c:api_ai_action_confirm_batch_handler`, call `ctx_ledger_id(c, user_id, "editor")` (returning on `< 0`).
  - In `expense_tool.c`, `transfer_tool.c`, and `batch_tool.c`: check `account_usecase_check_permission(pool, user_id, ledger_id, asset_id, MF_ACCOUNT_ACTION_WRITE_TX)` for every affected asset before mutating DB; switch `ledger_apply_expense` / `ledger_apply_transfer` calls to `ledger_apply_expense_dated(..., date)` / `ledger_apply_transfer_dated(..., date)`.
  - Route AI transfer confirmation through `transfer_usecase_create` (or shared helper) so `transfers` + paired `transfer_out`/`transfer_in` + `from_tx_id`/`to_tx_id` are created atomically with `fx_rate_to_cny`.
- [ ] **Step 4: Run tests to confirm GREEN**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_ai_action_confirm && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_ai_batch_confirmation && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_account_rbac`
- [ ] **Step 5: Commit**
  - `git add backend/src/interfaces/http/controllers/ai_controller.c backend/src/services/ai/tools/expense_tool.c backend/src/services/ai/tools/transfer_tool.c backend/src/services/ai/tools/batch_tool.c backend/tests/unit/test_ai_action_confirm.c backend/tests/unit/test_ai_batch_confirmation.c backend/tests/unit/test_account_rbac.c`
  - `git commit -m "fix(ai): 🐛 enforce account rbac, v014 transfer pairing, and dated snapshots in ai confirm"`

### Task 12: Enforce 2FA & registration policy on OAuth callback, disabled-user check on IM bind-auth, and atomic setup

**Files:**
- Modify: `backend/src/application/auth/usecases.c:293-315,1230-1277,1340-1398`
- Modify: `backend/src/application/ai/im_bot_usecases.c:383-431`
- Test: `backend/tests/unit/test_oauth_security.c`, `backend/tests/unit/test_auth_2fa_lifecycle.c`

- [ ] **Step 1: Add failing auth security tests in `test_oauth_security.c` and `test_auth_2fa_lifecycle.c`**
  - OAuth callback for new user when `ENABLE_REGISTRATION=0` must fail with 1004.
  - OAuth callback for existing user with `totp_enabled=1` must return `requires_2fa=true` + `temp_token` instead of full 7-day JWT.
  - `im_bot_usecase_bind_auth` for `user.disabled=1` must fail with 1004.
- [ ] **Step 2: Run tests to verify RED**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_oauth_security && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_auth_2fa_lifecycle`
- [ ] **Step 3: Implement fixes**
  - In `auth/usecases.c:auth_usecase_oauth_callback`: check `ENABLE_REGISTRATION` before creating a new OAuth user; for existing OAuth user with `totp_enabled`, issue `2fa_pending` temp token matching `auth_usecase_login`.
  - In `im_bot_usecases.c:im_bot_usecase_bind_auth`: reject if `user.disabled != 0` with `1004`.
  - In `auth_usecase_setup_first_admin` & `auth_usecase_register`: move `mf_auth_repo_count` check inside `db_tx_scope_begin` so concurrent first-admin / registration checks serialize inside the transaction.
- [ ] **Step 4: Run unit & integration tests to confirm GREEN**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_oauth_security && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_auth_2fa_lifecycle`
  - `MINEFOLIO_BUILD_DIR="$PWD/backend/build-asan" ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- bash backend/tests/test_im_bind_auth.sh`
- [ ] **Step 5: Commit**
  - `git add backend/src/application/auth/usecases.c backend/src/application/ai/im_bot_usecases.c backend/tests/unit/test_oauth_security.c backend/tests/unit/test_auth_2fa_lifecycle.c`
  - `git commit -m "fix(auth): 🐛 enforce 2fa/registration on oauth, disabled check on im bind, and atomic setup"`

### Task 13: Fix `daily_pnl` multi-currency conversion, quote sync currency alignment, and indices SWR cache

**Files:**
- Modify: `backend/src/services/report/report_asset_position.c:370-411`
- Modify: `backend/src/application/market/usecases.c:125-177,306-380`
- Modify: `backend/src/services/market/quote_engine.c:491-553,717-725`
- Test: `backend/tests/unit/test_report_services.c`, `backend/tests/unit/test_market_indices.c`, `backend/tests/unit/test_portfolio_pnl_alignment.c`

- [ ] **Step 1: Add failing tests for multi-currency `daily_pnl` and quote sync currency alignment**
  - In `test_report_services.c`: add USD asset balance log delta `100` (`USD->CNY = 7.2`) + CNY delta `50`; assert `daily_pnl == 770.00` (not `150.00`).
  - In `test_portfolio_pnl_alignment.c` / `test_market_indices.c`: verify `quote_engine` preserves stale cache on failed/in-progress refresh and `market_usecase` converts quote price when `q.currency != asset.currency`.
- [ ] **Step 2: Run tests to verify RED**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_report_services`
- [ ] **Step 3: Implement fixes**
  - In `report_asset_position.c:377-409`: group query by `c.asset_type, a.currency`, convert each group's delta via `exchange_rate_get_to_cny(cur)` and accumulate in `decimal_t`.
  - In `application/market/usecases.c`: after HTTP fetch, enter `db_tx_scope`, re-read asset `currency`/`net_value`/`quantity`, convert `q.current_price` from `q.currency` to `asset.currency` if different, update quote & delta atomically, and call `ledger_snapshot_invalidate_from(..., today)`.
  - In `quote_engine.c`: fetch all 15 indices into a local buffer before acquiring `g_indices_cache.lock` to swap `items`/`count`/`last_updated` atomically; re-check freshness immediately after acquiring `refresh_lock` (double-checked locking).
- [ ] **Step 4: Run tests to confirm GREEN**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_report_services && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_market_indices && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_portfolio_pnl_alignment`
- [ ] **Step 5: Commit**
  - `git add backend/src/services/report/report_asset_position.c backend/src/application/market/usecases.c backend/src/services/market/quote_engine.c backend/tests/unit/test_report_services.c backend/tests/unit/test_market_indices.c backend/tests/unit/test_portfolio_pnl_alignment.c`
  - `git commit -m "fix(market): 🐛 align quote sync currency, convert daily_pnl fx, and fix indices swr cache"`

### Task 14: Harden price history RBAC, redirect SSRF, strict query parsing, `config_get_str` OOB, and `pthread_create` checks

**Files:**
- Modify: `backend/src/infrastructure/repositories/market_repo_impl.c:106-128`
- Modify: `backend/src/common/http_safety.c:320-351`
- Modify: `backend/src/services/market/quote_engine.c:95-130`
- Modify: `backend/src/interfaces/http/controllers/report_controller.c:49-53`
- Modify: `backend/src/interfaces/http/controllers/ai_news_controller.c:32-48`
- Modify: `backend/src/common/config.c:52-55`
- Modify: `backend/src/services/ai/policy/policy.c:171-219`
- Modify: `backend/src/core/scheduler/scheduler_worker_pool.c:81-84`
- Modify: `backend/src/core/scheduler/scheduler_engine.c:1090-1097`
- Modify: `backend/src/services/ai/memory/summary.c:310-320`
- Modify: `backend/src/services/ai/runtime/loop_dispatch.c:140-150`
- Test: `backend/tests/unit/test_account_rbac.c`, `backend/tests/unit/test_ai_policy.c`, `backend/tests/unit/test_security_audit_regressions.c`

- [ ] **Step 1: Add regression tests**
  - Test `mf_market_repo_price_history_range` hides assets under `permission_level='none'` accounts.
  - Test `config_get_str` on a config file ending right after a bare key without quotes does not read OOB under ASan.
- [ ] **Step 2: Implement all boundary/safety fixes**
  - Add `AND ` `MF_RBAC_ASSET_VISIBLE_SQL("a")` in `mf_market_repo_price_history_range`.
  - Replace `atoi()` with `parse_query_int_strict()` returning 1002 on malformed input in `report_controller.c` & `ai_news_controller.c`.
  - In `common/config.c:52`, check `if (*search == '\0') break;` before `search++`.
  - In `policy.c:171-219`, snapshot `s_rules.enforce_confirmation` inside `s_policy_lock`.
  - Check `pthread_create` return values in `scheduler_worker_pool.c`, `scheduler_engine.c`, `summary.c`, `loop_dispatch.c`, tracking `bool started` so failed slots are never passed to `pthread_join` (and unqueued/failed task args are freed cleanly).
  - In `http_safety.c`: disable automatic `CURLOPT_FOLLOWLOCATION` when `allow_redirects=true` (or provide bounded manual redirect loop in `quote_engine.c` that re-calls `mf_http_validate_and_resolve_url` on every `Location` target up to 3 hops).
- [ ] **Step 3: Build & run full CTest suite in isolated runner**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 4G -j 2 --rss 1536 unit backend/build-asan`
- [ ] **Step 4: Commit**
  - `git add backend/src/infrastructure/repositories/market_repo_impl.c backend/src/common/http_safety.c backend/src/services/market/quote_engine.c backend/src/interfaces/http/controllers/report_controller.c backend/src/interfaces/http/controllers/ai_news_controller.c backend/src/common/config.c backend/src/services/ai/policy/policy.c backend/src/core/scheduler/scheduler_worker_pool.c backend/src/core/scheduler/scheduler_engine.c backend/src/services/ai/memory/summary.c backend/src/services/ai/runtime/loop_dispatch.c backend/tests/unit/test_account_rbac.c backend/tests/unit/test_security_audit_regressions.c`
  - `git commit -m "fix(core): 🐛 harden rbac price history, redirect ssrf, strict int parsing, and thread creation"`

### Task 15: Enforce transaction atomicity in `asset_usecase_update`, transfer immutability, rebuild error checks, and decimal trend precision

**Files:**
- Modify: `backend/src/application/asset/usecases.c:250-441`
- Modify: `backend/src/application/transaction/usecases.c:128-163,354-366`
- Modify: `backend/src/core/ledger/ledger_rebuild.c:349-386`
- Modify: `backend/src/application/ai/usecases.c:239-249`
- Modify: `backend/src/application/portfolio/usecases.c:78-103`
- Test: `backend/tests/unit/test_ledger_rebuild_checkpoint.c`, `backend/tests/unit/test_financial_precision_edge.c`

- [ ] **Step 1: Add regression tests**
  - Test updating non-investment asset `current_value` appends `asset_balance_logs`, so subsequent `ledger_rebuild_account` preserves the updated balance.
  - Test `tx_usecase_update` rejects modifying `transfer_out`/`transfer_in` rows (1002).
  - Test `portfolio_usecase_get_asset_trend` avoids float accumulation drift on high-precision decimal values.
- [ ] **Step 2: Implement fixes**
  - In `asset_usecase_update`: move `asset_update_cost_method`, non-investment `asset_update_basic`, `asset_update_account`, and `asset_balance_logs` adjustment insert inside `db_tx_scope`; invalidate `ledger_snapshots` on investment value updates.
  - In `tx_usecase_update`: reject `transfer_out`/`transfer_in` updates with 1002 ("转账记录不支持局部编辑，请删除后重新创建"); in `tx_usecase_create`, verify `asset_id` and `linked_asset_id` both belong to `cmd->ledger_id`.
  - In `ledger_rebuild_account`: check return values of `money_add`/`money_sub` and `upd_res != NULL`, rolling back and returning `-1` on error.
  - In `ai_usecase_sessions_delete`: check `db_tx_scope_begin` return code and abort/rollback on failure.
  - In `portfolio_usecase_get_asset_trend`: accumulate `day_assets`/`day_liabs` and log rollbacks in `decimal_t` before rounding at DTO output.
- [ ] **Step 3: Build & run unit + core integration tests**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 4G -j 2 --rss 1536 unit backend/build-asan`
  - `MINEFOLIO_BUILD_DIR="$PWD/backend/build-asan" ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- bash backend/tests/test_link.sh`
- [ ] **Step 4: Commit**
  - `git add backend/src/application/asset/usecases.c backend/src/application/transaction/usecases.c backend/src/core/ledger/ledger_rebuild.c backend/src/application/ai/usecases.c backend/src/application/portfolio/usecases.c backend/tests/unit/test_ledger_rebuild_checkpoint.c backend/tests/unit/test_financial_precision_edge.c`
  - `git commit -m "fix(core): 🐛 enforce tx atomicity, transfer immutability, and decimal accumulation"`

---

## Wave 3: Contracts, Dual-Engine Migrations, Gateway & Deployment/CI

### Task 16: Exclude OAuth callback from CSRF and prevent duplicate conflicting `Set-Cookie: csrf_token`

**Files:**
- Modify: `backend/src/middlewares/csrf_middleware.c:16-23,124-150`
- Test: `backend/tests/unit/test_security_audit_regressions.c`

- [ ] **Step 1: Add CSRF middleware regression tests in `test_security_audit_regressions.c`**
  - Verify `/api/auth/oauth/callback` and `/api/im/slack/bind-auth` pass exclusion check.
  - Verify `csrf_skip_if_bearer_or_seed` returns `true` after seeding `csrf_token` on safe unauthenticated GET requests (so `csilk_csrf_middleware_ext` does not emit a second `Set-Cookie` header).
- [ ] **Step 2: Implement fix in `csrf_middleware.c`**
  - Add `"/api/auth/oauth/callback"` to `s_csrf_exclude_paths`.
  - In `is_csrf_excluded_path` (or `csrf_skip_if_bearer_or_seed`), also treat `is_im_bind_auth_path(path)` as excluded.
  - When `csrf_skip_if_bearer_or_seed` seeds `csrf_token` on a safe method or excluded path, `return true;` (no further validation needed and prevents `csilk_csrf_middleware_ext` from writing a duplicate `Secure=1` cookie).
- [ ] **Step 3: Build & test under ASan**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_security_audit_regressions`
- [ ] **Step 4: Commit**
  - `git add backend/src/middlewares/csrf_middleware.c backend/tests/unit/test_security_audit_regressions.c`
  - `git commit -m "fix(auth): 🐛 exclude oauth callback from csrf and prevent duplicate csrf cookies"`

### Task 17: Normalize `import_rule` and `im_bot_instance` boolean/int fields across SQLite and PostgreSQL

**Files:**
- Modify: `backend/src/interfaces/http/controllers/import_rule_controller.c:8-30`
- Modify: `backend/src/application/ai/im_bot_instance_usecases.c:260-305`
- Modify: `gateway/ilink/supervisor.js:120-128`
- Modify: `gateway/ilink/runner.js:194-202`
- Test: `gateway/tests/ilink-bool-parse.test.js`

- [ ] **Step 1: Write failing gateway unit test for `"0"`/`"f"`/`"false"` boolean flags**
- [ ] **Step 2: Implement backend normalization & defensive gateway parsing**
  - In `import_rule_controller.c` & `im_bot_instance_usecases.c`: construct/normalize JSON output using `db_get_int()` and `db_get_bool()` so `id`, `priority`, `login_epoch`, `is_active`, and `enabled` emit real JSON numbers/booleans instead of `"0"`/`"f"`/`"t"` strings.
  - In `gateway/ilink/{supervisor,runner}.js`: use helper `parseBoolFlag(v)` returning `false` for `0`, `'0'`, `'false'`, `'f'`, `''`, `null`, `undefined`.
- [ ] **Step 3: Run backend build + integration + gateway tests**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan`
  - `MINEFOLIO_BUILD_DIR="$PWD/backend/build-asan" ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- bash backend/tests/test_im_bot_instance.sh`
  - `NODE_OPTIONS="--max-old-space-size=1024" ./backend/tests/run_isolated.sh -m 2G -- npm --prefix gateway test`
- [ ] **Step 4: Commit**
  - `git add backend/src/interfaces/http/controllers/import_rule_controller.c backend/src/application/ai/im_bot_instance_usecases.c gateway/ilink/supervisor.js gateway/ilink/runner.js gateway/tests/ilink-bool-parse.test.js`
  - `git commit -m "fix(db): 🐛 normalize boolean/int fields for import rules and im instances across engines"`

### Task 18: Fix frontend `Blob` error interception, Workflow SSE `message_id`, `ai-news` ledger header, and API return types

**Files:**
- Modify: `frontend/src/utils/http.ts:211-225`
- Modify: `frontend/src/api/ai.ts:163-188,278-305,542-635`
- Modify: `frontend/src/stores/chat.ts:903-955`
- Modify: `frontend/src/api/ai-news.ts:9-42`
- Modify: `frontend/src/api/im.ts:50-53`
- Test: `frontend/tests/http-blob-error.spec.ts`, `frontend/tests/ai-workflow-stream-ids.spec.ts`

- [ ] **Step 1: Write failing vitest specs `http-blob-error.spec.ts` and `ai-workflow-stream-ids.spec.ts`**
  - Test `http` rejects `Blob` with `type: 'application/json'` and `{ code: 1001, message: 'Unauthorized' }` and calls `handleAuthError()`.
  - Test `runWorkflowStream` preserves `id` on `user_message_id` and `assistant_message_id` events and `assertStreamResponseOk` throws `body.error` on HTTP 403 JSON without consuming valid SSE body.
- [ ] **Step 2: Run targeted vitest to confirm RED**
  - `NODE_OPTIONS="--max-old-space-size=2048" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend exec -- vitest run --config vite.config.mobile.ts --pool=forks --poolOptions.forks.maxForks=2 tests/http-blob-error.spec.ts tests/ai-workflow-stream-ids.spec.ts`
- [ ] **Step 3: Implement fixes**
  - In `http.ts`: make response error check `async` for `res.data instanceof Blob && res.data.type.includes('application/json')`, parse JSON, handle `1001` & error toast, and reject.
  - In `api/ai.ts`: update `assertStreamResponseOk` to handle `{ error?: string }` alongside `code` and throw if `!resp.ok` before reading body when `resp.ok` is false (or parse JSON only when `!resp.ok` or `contentType.includes('application/json')` and throw clearly); add `id?: number | string` to `WorkflowStreamChunk` and extract `parsed.id` in `runWorkflowStream`; fix `createSession` (`Promise<{ id: number } | AiSession>`) and `updateSession` (`Promise<void>`).
  - In `stores/chat.ts`: handle `user_message_id` / `assistant_message_id` in workflow stream handler to set `server_id`.
  - In `api/ai-news.ts`: pass `{ headers: { 'X-Ledger-Id': String(ledgerId) } }` whenever `ledgerId` is given.
  - In `api/im.ts`: change `unbind` return type to `Promise<void>`.
- [ ] **Step 4: Run targeted vitest + `npm run build`**
  - `NODE_OPTIONS="--max-old-space-size=2048" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend exec -- vitest run --config vite.config.mobile.ts --pool=forks --poolOptions.forks.maxForks=2 tests/http-blob-error.spec.ts tests/ai-workflow-stream-ids.spec.ts`
  - `NODE_OPTIONS="--max-old-space-size=3072" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend run build`
- [ ] **Step 5: Commit**
  - `git add frontend/src/utils/http.ts frontend/src/api/ai.ts frontend/src/stores/chat.ts frontend/src/api/ai-news.ts frontend/src/api/im.ts frontend/tests/http-blob-error.spec.ts frontend/tests/ai-workflow-stream-ids.spec.ts`
  - `git commit -m "fix(frontend): 🐛 handle blob error envelopes, workflow message ids, and ai-news ledger header"`

### Task 19: Synchronize `migration.sql` / `migration_postgres.sql` and add `V040` PostgreSQL transfer index parity

**Files:**
- Modify: `backend/sql/migration.sql`
- Modify: `backend/sql/migration_postgres.sql`
- Create: `backend/sql/migrations/sqlite/V040__parity_indexes_and_constraints.sql`
- Create: `backend/sql/migrations/postgres/V040__parity_indexes_and_constraints.sql`
- Test: `backend/tests/unit/test_migration_engine.c`

- [ ] **Step 1: Sync baseline schema files without altering `V001`..`V039`**
  - In `migration.sql`: add missing tables (`savings_goals`, `ai_session_summaries`, `mcp_server`, `mcp_server_tool`), add `ledger_id` to `category_seed_state`, and move `capability` column into `CREATE TABLE IF NOT EXISTS im_bot_instances` removing the bare non-idempotent `ALTER TABLE` at L754.
  - In `migration_postgres.sql`: add missing tables (`savings_goals`, `ai_session_summaries`, `mcp_server`, `mcp_server_tool`, `im_pending_actions`) and missing columns (`users.oauth_provider`, `users.oauth_id`, `im_bot_instances.capability`, `category_seed_state.ledger_id`).
  - Create `V040__parity_indexes_and_constraints.sql` for Postgres (`CREATE INDEX IF NOT EXISTS idx_transfers_user`, `idx_transfers_from_asset`, `idx_transfers_to_asset`) and matching idempotent `V040` for SQLite.
- [ ] **Step 2: Verify migration engine tests & `migration.sql` double-run idempotency**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- ./backend/build-asan/test_migration_engine`
  - Test double-execution idempotency: `sqlite3 /tmp/test_mig_idem.db < backend/sql/migration.sql && sqlite3 /tmp/test_mig_idem.db < backend/sql/migration.sql && rm -f /tmp/test_mig_idem.db`.
- [ ] **Step 3: Commit**
  - `git add backend/sql/migration.sql backend/sql/migration_postgres.sql backend/sql/migrations/sqlite/V040__parity_indexes_and_constraints.sql backend/sql/migrations/postgres/V040__parity_indexes_and_constraints.sql`
  - `git commit -m "fix(db): 🐛 align full schema scripts and add v040 postgres transfer index parity"`

### Task 20: Fix `docker-compose.im.yml` runtime/volume and harden OneBot + iLink gateway adapters

**Files:**
- Modify: `docker-compose.im.yml:40-58`
- Modify: `gateway/adapters/onebot-server.js:45-118`
- Modify: `gateway/ilink/api.js:39-54`
- Modify: `gateway/ilink/supervisor.js:173-185`
- Modify: `gateway/ilink/login.js:9-15,94-150`
- Test: `gateway/tests/gateway-hardening.test.js`

- [ ] **Step 1: Add failing gateway tests for OneBot WS auth/timeout, iLink `res.ok`, redirect allowlist, and shutdown grace**
- [ ] **Step 2: Implement fixes**
  - `docker-compose.im.yml`: change `im-gateway` image to `node:22-alpine`, add writable volume `im_gateway_data:/data` with `CACHE_FILE: /data/im_bindings.json`, and bind NapCat ports to `127.0.0.1`.
  - `onebot-server.js`: pass `ONEBOT_ACCESS_TOKEN` on WebSocket connection (query/header), add `AbortSignal.timeout(15000)` to `fetch`, close any previous socket before reconnect, add exponential backoff with jitter + WS ping heartbeat + `SIGTERM`/`SIGINT` cleanup.
  - `ilink/api.js`: check `if (!res.ok)` and throw error with status/`body.error`.
  - `ilink/supervisor.js`: increase `SIGKILL` shutdown wait from `100` ms to `3500` ms (or wait on child exit promises up to 3500ms).
  - `ilink/login.js`: restrict `buildRedirectBaseUrl` and `statusRes.baseurl` to `https://` hosts ending with `.weixin.qq.com` (or `weixin.qq.com`).
- [ ] **Step 3: Run gateway tests & YAML check**
  - `NODE_OPTIONS="--max-old-space-size=1024" ./backend/tests/run_isolated.sh -m 2G -- npm --prefix gateway test`
  - `python3 -c "import yaml; yaml.safe_load(open('docker-compose.im.yml'))"`
- [ ] **Step 4: Commit**
  - `git add docker-compose.im.yml gateway/adapters/onebot-server.js gateway/ilink/api.js gateway/ilink/supervisor.js gateway/ilink/login.js gateway/tests/gateway-hardening.test.js`
  - `git commit -m "fix(gateway): 🐛 fix im-gateway node runtime/cache volume and harden onebot/ilink adapters"`

### Task 21: Fix Nginx reverse proxy rules and `docker-compose.yml` `TRUST_PROXY` / credential exposure

**Files:**
- Modify: `nginx/minefolio.docker.conf`
- Modify: `nginx/minefolio.conf`
- Modify: `docker-compose.yml:24-125`

- [ ] **Step 1: Update Nginx configurations**
  - Add `client_max_body_size 25m;` to support 10MB receipts & 20MB file uploads.
  - Replace/extend SSE location to match both `/api/ai/chat` and `/api/ai/workflows/run` (`proxy_buffering off; gzip off; proxy_read_timeout 3600s;`).
  - Add `location = /mcp` proxying to backend with host & forwarding headers.
  - Fix `location /admin/` in `minefolio.docker.conf` using `rewrite ^/admin/(.*)$ /csilk-admin/$1 break; proxy_pass $backend_upstream;` so subpaths are preserved.
  - Add baseline security headers (`X-Content-Type-Options`, `X-Frame-Options`, `Referrer-Policy`) on static `location /`.
- [ ] **Step 2: Update `docker-compose.yml`**
  - Add `TRUST_PROXY: "1"` to `minefolio` environment so Nginx `X-Forwarded-For`/`X-Real-IP` do not collapse all users into `"untrusted-fwd"`.
  - Replace weak default `POSTGRES_PASSWORD:-minefolio` and `NAPCAT_WEBUI_TOKEN:-minefolio123` with `${VAR:?required}` (or blank-safe non-weak config), bind NapCat/OneBot ports (`3001`, `3002`, `6099`) to `127.0.0.1:...`, and use official `node:22-alpine` instead of `docker.1ms.run/library/node:22-alpine`.
- [ ] **Step 3: Validate YAML & Nginx config syntax**
  - `python3 -c "import yaml; yaml.safe_load(open('docker-compose.yml'))"`
- [ ] **Step 4: Commit**
  - `git add nginx/minefolio.docker.conf nginx/minefolio.conf docker-compose.yml`
  - `git commit -m "fix(deploy): 🐛 fix nginx sse/mcp/upload proxy rules and enable trust_proxy in compose"`

### Task 22: Harden `Dockerfile` / `Dockerfile.frontend` supply-chain security and CI release gates

**Files:**
- Modify: `Dockerfile:98-100,168-180,210,221,283-287`
- Modify: `Dockerfile.frontend:21,32-36`
- Modify: `.github/workflows/ci.yml:259,400-405,601-606`
- Modify: `.github/workflows/sanitizers.yml:88-107,165-183`

- [ ] **Step 1: Harden Dockerfiles**
  - Remove `git config --global http.sslVerify false` from `Dockerfile`.
  - Parameterize Node download architecture via `TARGETARCH` (`x64`/`arm64`) and verify SHA-256 checksums for Node tarball & `swagger-ui-dist`.
  - Use `npm ci` in both `Dockerfile` and `Dockerfile.frontend`; drop duplicate `dist` copy in `Dockerfile.frontend`.
- [ ] **Step 2: Update CI workflows**
  - In `.github/workflows/ci.yml`: add `backend-postgres`, `backend-asan`, `gateway-test` to `docker-build.needs` and add `docker-build-frontend` to `release.needs`; bump `gateway-test` to Node 22.
  - In `.github/workflows/sanitizers.yml`: add `test_ilink_scheduler_push.sh` and `test_im_bot_instance.sh`.
- [ ] **Step 3: Validate YAML syntax**
  - `python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml')); yaml.safe_load(open('.github/workflows/sanitizers.yml'))"`
- [ ] **Step 4: Commit**
  - `git add Dockerfile Dockerfile.frontend .github/workflows/ci.yml .github/workflows/sanitizers.yml`
  - `git commit -m "ci: 👷 harden dockerfile tls/checksums and gate releases on postgres, asan, and gateway tests"`

---

## Wave 4: Vue 3 Frontend & Mobile Offline Sync

### Task 23: Flatten category trees and include archived accounts in `SyncEngine.pullRemote()`

**Files:**
- Modify: `frontend/src/sync/SyncEngine.ts:475-545`
- Test: `frontend/tests/sync-engine-categories-tree.spec.ts`

- [ ] **Step 1: Write failing vitest `sync-engine-categories-tree.spec.ts`**
  - Mock `categoriesApi.list` returning nested `{ id: 1, children: [{ id: 10, parent_id: 1, children: [{ id: 100, parent_id: 10 }] }] }`. Pre-populate local SQLite with child id 10, run `pullRemote`, and assert ids `1`, `10`, and `100` all exist in local `categories` (`__deleted = 0`).
  - Verify `accountsApi.list` is called with `include_archived: 1`.
- [ ] **Step 2: Run test to confirm RED**
  - `NODE_OPTIONS="--max-old-space-size=2048" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend exec -- vitest run --config vite.config.mobile.ts --pool=forks --poolOptions.forks.maxForks=2 tests/sync-engine-categories-tree.spec.ts`
- [ ] **Step 3: Implement `flattenCategoryTree` & `include_archived` in `SyncEngine.ts`**
  - Flatten `children` recursively when `table === 'categories'`.
  - Include `include_archived: 1` in params when pulling `accounts`.
- [ ] **Step 4: Run test to confirm GREEN**
  - `NODE_OPTIONS="--max-old-space-size=2048" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend exec -- vitest run --config vite.config.mobile.ts --pool=forks --poolOptions.forks.maxForks=2 tests/sync-engine-categories-tree.spec.ts`
- [ ] **Step 5: Commit**
  - `git add frontend/src/sync/SyncEngine.ts frontend/tests/sync-engine-categories-tree.spec.ts`
  - `git commit -m "fix(mobile): 🐛 flatten category trees and include archived accounts in sync pull"`

### Task 24: Forward `X-Ledger-Id` on category/tag sync, add `categories.ledger_id`, and fix conflict discard rollback

**Files:**
- Modify: `frontend/src/api/categories.ts:42-60`
- Modify: `frontend/src/api/tags.ts:29-50`
- Modify: `frontend/src/db/schema.ts:13-26,170`
- Modify: `frontend/src/sync/SyncEngine.ts:458-468,520,586-597`
- Test: `frontend/tests/sync-engine-conflict-and-ledger.spec.ts`

- [ ] **Step 1: Write failing vitest `sync-engine-conflict-and-ledger.spec.ts`**
  - Verify `categoriesApi.create(payload, { headers: { 'X-Ledger-Id': '42' } })` forwards headers.
  - Verify `resolveConflict(id, 'discard')` on an `update`/`delete` conflict restores `remote_payload` and resets `__deleted = 0`, and on a `create` conflict removes the local placeholder row.
  - Verify `pushLocal` dropping a `create` item on `1002` deletes its local placeholder row.
- [ ] **Step 2: Run test to confirm RED**
  - `NODE_OPTIONS="--max-old-space-size=2048" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend exec -- vitest run --config vite.config.mobile.ts --pool=forks --poolOptions.forks.maxForks=2 tests/sync-engine-conflict-and-ledger.spec.ts`
- [ ] **Step 3: Implement fixes**
  - Add optional `config?: AxiosRequestConfig` parameter to `categoriesApi` & `tagsApi` `create`/`update`/`delete`.
  - Add `ledger_id INTEGER DEFAULT 0` to `categories` in `schema.ts` + `TABLE_COLUMNS.categories` + `SyncEngine.ts:hasLedgerId` (and `ALTER TABLE categories ADD COLUMN ledger_id` migration helper for existing local DBs).
  - Fix `resolveConflict(queueId, 'discard')` and validation-drop `create` cleanup in `SyncEngine.ts`.
- [ ] **Step 4: Run test to confirm GREEN**
  - `NODE_OPTIONS="--max-old-space-size=2048" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend exec -- vitest run --config vite.config.mobile.ts --pool=forks --poolOptions.forks.maxForks=2 tests/sync-engine-conflict-and-ledger.spec.ts`
- [ ] **Step 5: Commit**
  - `git add frontend/src/api/categories.ts frontend/src/api/tags.ts frontend/src/db/schema.ts frontend/src/sync/SyncEngine.ts frontend/tests/sync-engine-conflict-and-ledger.spec.ts`
  - `git commit -m "fix(mobile): 🐛 forward ledger header on category/tag sync and restore remote state on discard"`

### Task 25: Add `SyncEngine` epoch guard, preserve `last_sync_at` on pull errors, and deduplicate `1001` handler

**Files:**
- Modify: `frontend/src/sync/SyncEngine.ts:236-245,445-455,540-585`
- Modify: `frontend/src/main-mobile.ts:45-55`
- Test: `frontend/tests/sync-engine-epoch-guard.spec.ts`

- [ ] **Step 1: Write failing vitest `sync-engine-epoch-guard.spec.ts`**
  - Call `sync.reset()` while `pullRemote()` is awaiting an API promise; resolve the old promise and assert zero rows are written to local SQLite.
  - Simulate `pullRemote` failing on one table; assert `last_sync_at` is NOT updated.
  - Simulate `1001` auth fail during `pushLocal`; assert `authFailHandler` is called exactly once.
- [ ] **Step 2: Run test to confirm RED**
  - `NODE_OPTIONS="--max-old-space-size=2048" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend exec -- vitest run --config vite.config.mobile.ts --pool=forks --poolOptions.forks.maxForks=2 tests/sync-engine-epoch-guard.spec.ts`
- [ ] **Step 3: Implement fixes**
  - Add `private syncEpoch = 0`, increment in `reset()`, check `epoch !== this.syncEpoch` after awaits in `pushLocal` & `pullRemote`.
  - Track `hadPullFailure` in `pullRemote`; rethrow `1001` immediately and skip `setLastSync()` when `hadPullFailure` is true.
  - Trigger `authFailHandler` solely in `syncNow()` catch block (remove duplicate call in `pushLocal` catch).
  - Guard `registerNetworkListeners` callback in `main-mobile.ts` on `Boolean(useAuthStore().token)`.
- [ ] **Step 4: Run test to confirm GREEN**
  - `NODE_OPTIONS="--max-old-space-size=2048" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend exec -- vitest run --config vite.config.mobile.ts --pool=forks --poolOptions.forks.maxForks=2 tests/sync-engine-epoch-guard.spec.ts`
- [ ] **Step 5: Commit**
  - `git add frontend/src/sync/SyncEngine.ts frontend/src/main-mobile.ts frontend/tests/sync-engine-epoch-guard.spec.ts`
  - `git commit -m "fix(mobile): 🐛 add sync epoch guard, preserve last_sync_at on pull failure, and dedupe auth fail"`

### Task 26: Harden mobile views for degraded `localDb`, online category mutations, and ledger-scoped `view_cache`

**Files:**
- Modify: `frontend/src/views-mobile/CategoryManageMobile.vue:54-92`
- Modify: `frontend/src/views-mobile/ChatMobile.vue:175-180`
- Modify: `frontend/src/utils/chat-draft.ts:19-26`
- Modify: `frontend/src/views-mobile/HoldingsMobile.vue:100-140`
- Modify: `frontend/src/views-mobile/ReportsMobile.vue:46-65`
- Modify: `frontend/src/views-mobile/PlansMobile.vue:233-268`
- Modify: `frontend/src/composables/mobile/useViewCache.ts:27-34`
- Test: `frontend/tests/mobile-degraded-localdb.spec.ts`

- [ ] **Step 1: Write failing vitest `mobile-degraded-localdb.spec.ts`**
  - Verify `saveDraft` and `saveViewCache` do not throw when `localDb` is uninitialized.
  - Verify `CategoryManageMobile` online delete/load reflects online API and calls `useCategoryStore().invalidate()`.
- [ ] **Step 2: Implement fixes**
  - `CategoryManageMobile.vue`: fetch via `categoriesApi.list()` / `tagsApi.list()` first with local SQLite fallback inside `try/catch`, and invalidate `useCategoryStore()` on mutations.
  - `ChatMobile.vue` & `chat-draft.ts`: wrap `flushDraft()`/`saveDraft()` in `try/catch` so `chat.abortCurrentStream()` always executes in `onBeforeUnmount`.
  - `HoldingsMobile.vue` & `ReportsMobile.vue`: move `deriveLocalHoldings()` / `aggregateMonthlyReport()` inside `try/catch` so degraded `localDb === null` never aborts online fetch.
  - `useViewCache.ts`: guard `saveViewCache()` with `try/catch`; `PlansMobile.vue`: include `ledgerId` in `view_cache` keys.
- [ ] **Step 3: Run vitest + mobile build**
  - `NODE_OPTIONS="--max-old-space-size=2048" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend exec -- vitest run --config vite.config.mobile.ts --pool=forks --poolOptions.forks.maxForks=2 tests/mobile-degraded-localdb.spec.ts`
  - `NODE_OPTIONS="--max-old-space-size=3072" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend run build:mobile`
- [ ] **Step 4: Commit**
  - `git add frontend/src/views-mobile/CategoryManageMobile.vue frontend/src/views-mobile/ChatMobile.vue frontend/src/utils/chat-draft.ts frontend/src/views-mobile/HoldingsMobile.vue frontend/src/views-mobile/ReportsMobile.vue frontend/src/views-mobile/PlansMobile.vue frontend/src/composables/mobile/useViewCache.ts frontend/tests/mobile-degraded-localdb.spec.ts`
  - `git commit -m "fix(mobile): 🐛 harden localDb degradation, online category refresh, and view cache isolation"`

### Task 27: Add async epoch guards to Pinia stores and `useFinancialEntities` to prevent cross-account cache poisoning

**Files:**
- Modify: `frontend/src/stores/category.ts:89-145,256-274`
- Modify: `frontend/src/stores/chat.ts:199-267,435-453,718-760`
- Modify: `frontend/src/stores/ledger.ts:53-98`
- Modify: `frontend/src/stores/market.ts:62-90,325-344`
- Modify: `frontend/src/composables/useFinancialEntities.ts:190-306`
- Test: `frontend/tests/store-logout-race-isolation.spec.ts`

- [ ] **Step 1: Write failing vitest `store-logout-race-isolation.spec.ts`**
  - Start `category.loadCategories()`, `chat.fetchSessions()`, `ledger.fetchLedgers()`, `market.fetchRates()`, and `useFinancialEntities().loadEntities()`, invoke `reset()` / `resetState()` / `resetFinancialEntities()` mid-flight, then resolve the old requests; assert none of the cleared stores or `localStorage` keys are repopulated, and `market.reset()` clears `prevPrices`/`priceDirections`/`rates`/`baseCurrency`.
- [ ] **Step 2: Run test to confirm RED**
  - `NODE_OPTIONS="--max-old-space-size=2048" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend exec -- vitest run --config vite.config.mobile.ts --pool=forks --poolOptions.forks.maxForks=2 tests/store-logout-race-isolation.spec.ts`
- [ ] **Step 3: Implement epoch guards across all 5 modules**
  - `category.ts`: `loadEpoch++` & `inflightLoad = null` in `reset()`/`invalidate()`, discard stale responses before setting `allNodes` or SWR cache.
  - `chat.ts`: increment `sessionSwitchSeq` and `sessionListSeq` in `resetState()`; guard `fetchSessions()` with `sessionListSeq`; in `doSendMessage`, `doRunWorkflow`, `stageWorkflow`, verify sequence didn't advance across `await createNewSession()`.
  - `ledger.ts`: add `fetchSeq`, increment in `reset()`, check before updating `ledgers`/`currentLedgerId`/`localStorage`.
  - `market.ts`: add `ratesReqSeq` for `fetchRates`/`syncExchangeRates`; reset `prevPrices`, `priceDirections`, `rates`, and `baseCurrency` in `reset()`.
  - `useFinancialEntities.ts`: add `entitiesEpoch`, increment in `resetFinancialEntities()`, allow a new epoch's `loadEntities()` to proceed even if an older epoch was loading, and discard stale results on completion.
- [ ] **Step 4: Run test to confirm GREEN**
  - `NODE_OPTIONS="--max-old-space-size=2048" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend exec -- vitest run --config vite.config.mobile.ts --pool=forks --poolOptions.forks.maxForks=2 tests/store-logout-race-isolation.spec.ts`
- [ ] **Step 5: Commit**
  - `git add frontend/src/stores/category.ts frontend/src/stores/chat.ts frontend/src/stores/ledger.ts frontend/src/stores/market.ts frontend/src/composables/useFinancialEntities.ts frontend/tests/store-logout-race-isolation.spec.ts`
  - `git commit -m "fix(frontend): 🐛 add async epoch guards across stores to prevent cross-account cache poisoning"`

### Task 28: Guard post-`await` listeners/timers on unmount and add sequence guards to `SymbolSelect` / `PriceHistoryChart`

**Files:**
- Modify: `frontend/src/views/Login.vue:392-422`
- Modify: `frontend/src/components/settings/ImBotSettings.vue:396-457,559-623`
- Modify: `frontend/src/components/SymbolSelect.vue:67-86`
- Modify: `frontend/src/components/PriceHistoryChart.vue:59-71`
- Modify: `frontend/src/components/settings/TwoFactorSettings.vue:170-182`
- Modify: `frontend/src/views/Assets.vue:672-702`
- Test: `frontend/tests/lifecycle-unmount-cleanup.spec.ts`

- [ ] **Step 1: Write failing vitest `lifecycle-unmount-cleanup.spec.ts`**
  - Mount `Login.vue` / `ImBotSettings.vue` with deferred API promise, unmount before promise resolves, resolve promise, and assert no `requestAnimationFrame` loop / `window.addEventListener` / `setInterval` starts after unmount.
  - Test `SymbolSelect.vue` & `PriceHistoryChart.vue` discard stale slower responses when a newer query resolves first.
- [ ] **Step 2: Run test to confirm RED**
  - `NODE_OPTIONS="--max-old-space-size=2048" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend exec -- vitest run --config vite.config.mobile.ts --pool=forks --poolOptions.forks.maxForks=2 tests/lifecycle-unmount-cleanup.spec.ts`
- [ ] **Step 3: Implement unmount & sequence guards**
  - Add `let isUnmounted = false` (flipped in `onBeforeUnmount`/`onUnmounted`) in `Login.vue` and `ImBotSettings.vue` and check after every `await` before starting rAF, listeners, or `setInterval`.
  - Add `onUnmounted(() => { if (debounceTimer) clearTimeout(debounceTimer) })` and `searchSeq` in `SymbolSelect.vue`; add `loadSeq` in `PriceHistoryChart.vue`.
  - Track and clear `setTimeout` handles on unmount in `TwoFactorSettings.vue` (plus `try/catch` around dynamic `import('qrcode')`) and `Assets.vue`.
- [ ] **Step 4: Run test to confirm GREEN**
  - `NODE_OPTIONS="--max-old-space-size=2048" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend exec -- vitest run --config vite.config.mobile.ts --pool=forks --poolOptions.forks.maxForks=2 tests/lifecycle-unmount-cleanup.spec.ts`
- [ ] **Step 5: Commit**
  - `git add frontend/src/views/Login.vue frontend/src/components/settings/ImBotSettings.vue frontend/src/components/SymbolSelect.vue frontend/src/components/PriceHistoryChart.vue frontend/src/components/settings/TwoFactorSettings.vue frontend/src/views/Assets.vue frontend/tests/lifecycle-unmount-cleanup.spec.ts`
  - `git commit -m "fix(frontend): 🐛 guard post-await listeners/timers against unmount leaks and stale responses"`

### Task 29: Use `localToday()`, memoize heavy template markdown/JSON parsing, and eliminate prop mutation

**Files:**
- Modify: `frontend/src/components/NetWorthChart.vue:176-186`
- Modify: `frontend/src/components/PriceHistoryChart.vue:157-169`
- Modify: `frontend/src/components/market/FxTrendChart.vue:186-197`
- Modify: `frontend/src/views/NewsPulse.vue:62-79`
- Modify: `frontend/src/components/AnalysisPanel.vue:113`
- Modify: `frontend/src/components/scheduler/JobLogDrawer.vue:50,165`
- Modify: `frontend/src/components/skills/SkillCard.vue:131-142`
- Modify: `frontend/src/components/mcp/McpCard.vue:141-163`
- Modify: parent views handling `@toggle` from `SkillCard` / `McpCard` (check exact call sites in `views/Skills.vue`, `views/Mcp.vue` or components)

- [ ] **Step 1: Implement changes**
  - Replace `new Date().toISOString().slice(0, 10)` with `localToday()` from `@/utils/format` in `NetWorthChart.vue`, `PriceHistoryChart.vue`, `FxTrendChart.vue`.
  - Memoize `parseHoldings` & `renderMarkdown` via `computed` caches (keyed by item `id` + raw string / `expandedId`) in `NewsPulse.vue`, `AnalysisPanel.vue`, `JobLogDrawer.vue`.
  - Remove `props.skill.enabled = ...` and `props.server.enabled = ...` in `SkillCard.vue` and `McpCard.vue`; ensure parent `@toggle` handlers update the item state cleanly.
- [ ] **Step 2: Run full frontend vitest + desktop build + mobile build**
  - `NODE_OPTIONS="--max-old-space-size=3072" ./backend/tests/run_isolated.sh -m 8G -- npm --prefix frontend exec -- vitest run --config vite.config.mobile.ts --pool=forks --poolOptions.forks.maxForks=2`
  - `NODE_OPTIONS="--max-old-space-size=3072" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend run build`
  - `NODE_OPTIONS="--max-old-space-size=3072" ./backend/tests/run_isolated.sh -m 6G -- npm --prefix frontend run build:mobile`
- [ ] **Step 3: Revert any incidental `components*.d.ts` churn and commit**
  - `git add` only the edited source files
  - `git commit -m "refactor(frontend): ♻️ use localToday, memoize markdown rendering, and avoid prop mutation"`

---

## Wave 5: C Backend DDD Boundary Cleanup

### Task 30: Invert `common/balance.c` upward dependencies via hook registration

**Files:**
- Modify: `backend/src/common/balance.h`
- Modify: `backend/src/common/balance.c:1-5`
- Modify: `backend/src/main.c`
- Modify: `backend/src/core/ledger/ledger_engine.c` (or registration helper)

- [ ] **Step 1: Define hook table in `common/balance.h` and remove upward includes in `common/balance.c`**
  - Define `mf_balance_hooks_t` (`get_fx_to_cny`, `invalidate_snapshot_from`, etc.) with `balance_register_hooks(const mf_balance_hooks_t* hooks)` in `common/balance.h`.
  - Remove `#include "core/ledger/ledger_engine.h"` and `#include "services/market/exchange_rate_service.h"` from `common/balance.c`.
  - Register hooks in `main.c` and ensure unit tests that call `balance_apply_delta*` without `main.c` also have hooks registered or clean fallback.
- [ ] **Step 2: Build & run full CTest unit suite**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 4G -j 2 --rss 1536 unit backend/build-asan`
- [ ] **Step 3: Commit**
  - `git add backend/src/common/balance.h backend/src/common/balance.c backend/src/main.c backend/src/core/ledger/ledger_engine.c`
  - `git commit -m "refactor(core): ♻️ invert balance.c dependencies via hook registration"`

### Task 31: Remove reverse controller includes from `services/` and dead `file_usecase_import_*` / `exec_web_search` stubs

**Files:**
- Modify: `backend/src/services/import_service.c:1-10`
- Modify: `backend/src/services/export_service.c:1-10`
- Modify: `backend/src/interfaces/http/controllers/import_export_controller.h`
- Modify: `backend/src/application/file/usecases.h`
- Modify: `backend/src/application/file/usecases.c:42-73`
- Modify: `backend/src/services/ai/tools/report_tool.c:128-143,206-223`

- [ ] **Step 1: Clean up reverse includes and dead stubs**
  - Verify `import_service.c` and `export_service.c` do not depend on `interfaces/http/controllers/import_export_controller.h` (move any shared declarations to `services/import_export_service.h` or `application/import_export/`).
  - Verify `file_usecase_import_transactions` and `file_usecase_import_daily_expenses` have zero callers, then remove them.
  - Remove the stub `exec_web_search` from `report_tool.c` (preserving real `web_search_tool.c` registration).
- [ ] **Step 2: Build & run full CTest unit suite + `test_link.sh` + `test_ai_tool_call.sh`**
  - `./backend/tests/run_isolated.sh -m 4G -j 2 build backend/build-asan && ./backend/tests/run_isolated.sh -m 4G -j 2 --rss 1536 unit backend/build-asan`
  - `MINEFOLIO_BUILD_DIR="$PWD/backend/build-asan" ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- bash backend/tests/test_link.sh`
  - `MINEFOLIO_BUILD_DIR="$PWD/backend/build-asan" ./backend/tests/run_isolated.sh -m 2G --rss 1536 -- bash backend/tests/test_ai_tool_call.sh`
- [ ] **Step 3: Commit**
  - `git add backend/src/services/import_service.c backend/src/services/export_service.c backend/src/interfaces/http/controllers/import_export_controller.h backend/src/application/file/usecases.h backend/src/application/file/usecases.c backend/src/services/ai/tools/report_tool.c`
  - `git commit -m "refactor(application): ♻️ remove reverse controller includes and dead import/web_search stubs"`
