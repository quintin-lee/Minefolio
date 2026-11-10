# AI Config Lifecycle Sink & Service Slimming Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract the AI configuration snapshot lifecycle and RCU reference counting from `services/ai_service.c` into `application/ai/config_provider.h/.c`, decouple network probing into `application/ai/probe_usecase.h/.c`, completely eliminate the unsafe `ai_get_config()` interface across the repository, and reduce `services/ai_service.c` from ~1300 lines to < 550 lines while preserving strict DDD layering.

**Architecture:** 
1. `application/ai/config_provider.h/.c` encapsulates `ai_config_snapshot_t`, `atomic_int refcount`, RCU hot-reload (`ai_config_provider_reload`), mutex lock, pre-built `csilk_ai_t` instance, and paired `acquire`/`release`/`AI_CONFIG_SCOPE`.
2. `application/ai/probe_usecase.h/.c` encapsulates connection testing and dynamic model discovery, called by `interfaces/http/controllers/ai_controller.c`.
3. `services/ai_service.c` retains only SSE streaming bridge callbacks, chat dispatch, and service lifecycle coordination (`ai_init` / `ai_shutdown` coordinating summary worker pool, MCP subprocess pool, and config provider).
4. `application/ai/usecases.c` calls `ai_config_provider_reload(db_pool)` on settings updates without restarting worker threads or terminating MCP children.

**Tech Stack:** C23, POSIX pthreads, C11/C23 `<stdatomic.h>`, csilk HTTP framework, libcurl, SQLite/PostgreSQL, CMake (Makefiles).

**Spec:** [`docs/superpowers/specs/2026-11-10-ai-service-config-lifecycle-and-slimming-design.md`](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-11-10-ai-service-config-lifecycle-and-slimming-design.md)

---

## Global Constraints

- **Strict DDD Layering**: Dependency direction is one-way (`interfaces/` -> `application/` -> `domain/` -> `infrastructure/`). `application/` code must NEVER include or depend on `services/`.
- **`void* pool` Convention**: All functions in `application/ai/` taking a database connection pool must use `void* pool` as the parameter type.
- **No Unsafe Raw Config Interface**: The function `ai_get_config()` is completely eliminated from headers and source files; all consumers must use `ai_config_acquire()` / `ai_config_release()` or `AI_CONFIG_SCOPE`.
- **HTTP 200 Envelope**: All REST endpoints must return HTTP 200 with `{code: 0, message: "ok", data: ...}` using `common/response.h` helpers.
- **Build & Test Rules**: Build out-of-source in `backend/build/` via `cmake --build backend/build --parallel`. All tests (`test_link.sh`, `ctest`) must pass with 0 errors and 0 warnings.

---

## Review Focus

1. **Snapshot Reference Leak in `ai_usecase_models_list`**: The existing code called `ai_get_config()` without `release()`. Verify that Task 4 pins this with `ai_config_release(cfg)` on both success and error paths.
2. **Concurrent RCU Hot-Reload vs In-Flight Reader**: In-flight readers holding a snapshot must read valid data even if `ai_config_provider_reload()` swaps the snapshot concurrently. Task 1 test must pin this with 8 reader threads + concurrent reloads.
3. **Double Free on Writer Teardown**: When the last reader releases after shutdown/reload, verify `refcount` hits exactly 0 and cleans up `ai_config_free()` + `csilk_ai_free()` without double-free.
4. **Summary & MCP Subprocess Lifecycle Preservation**: `ai_init` / `ai_shutdown` in `services/ai_service.c` must continue to invoke `ai_summary_pool_init/shutdown` and `mf_mcp_stdio_pool_shutdown`. Settings hot-reload via `ai_config_provider_reload()` must NOT touch those pools.
5. **No Regressions in Unit Test Suite**: `tests/unit/test_ai_summary.c` and `./tests/test_link.sh` must remain 100% green without modification to integration test contracts.

---

## Tasks

### Task 1: AI Config Provider (`application/ai/config_provider.h/.c`) & Unit Test Suite

**Files:**
- Create: `backend/src/application/ai/config_provider.h`
- Create: `backend/src/application/ai/config_provider.c`
- Create: `backend/tests/unit/test_ai_config_provider.c`
- Modify: `backend/CMakeLists.txt:140-205`

**Interfaces:**
- Produces:
  ```c
  void ai_config_provider_init(void* pool);
  void ai_config_provider_shutdown(void);
  int  ai_config_provider_reload(void* pool);
  ai_config_t* ai_config_acquire(void);
  void ai_config_release(ai_config_t* cfg);
  csilk_ai_t* ai_config_get_ai_instance(const ai_config_t* cfg);
  #define AI_CONFIG_SCOPE(cfg) ...
  ```

- [ ] **Step 1: Write unit test `backend/tests/unit/test_ai_config_provider.c`**

Create `backend/tests/unit/test_ai_config_provider.c` testing:
1. `ai_config_provider_init(NULL)` loads config from default file or fallback.
2. `ai_config_acquire()` returns non-NULL `ai_config_t*`.
3. 8 concurrent threads running 2,000 acquires/releases each without data races.
4. Hot reload: thread A holds old snapshot, thread B calls `ai_config_provider_reload(NULL)`, thread A confirms data remains valid, releases; thread C acquires new snapshot.
5. `ai_config_provider_shutdown()` makes subsequent acquires return NULL.
6. `AI_CONFIG_SCOPE` macro cleanly releases config upon scope exit.

- [ ] **Step 2: Register test in `backend/CMakeLists.txt`**

Add `add_executable(test_ai_config_provider tests/unit/test_ai_config_provider.c ${LIB_SOURCES})` and `target_link_libraries(test_ai_config_provider PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)` with `add_test(NAME test_ai_config_provider COMMAND test_ai_config_provider)`.

- [ ] **Step 3: Run CMake build to verify test fails (compilation failure before implementation)**

Run: `cmake --build backend/build --target test_ai_config_provider`
Expected: FAIL (missing `config_provider.h`)

- [ ] **Step 4: Implement `backend/src/application/ai/config_provider.h` and `config_provider.c`**

1. Define private `ai_config_snapshot_t` with `ai_config_t config`, `csilk_ai_t* ai_instance`, and `atomic_int refcount`.
2. Implement `ai_config_provider_init(void* pool)`, `ai_config_provider_shutdown(void)`, `ai_config_provider_reload(void* pool)` with atomic pointer swap under `pthread_mutex_t g_snapshot_lock`.
3. Implement `ai_config_acquire(void)` (increments refcount) and `ai_config_release(ai_config_t* cfg)` (decrements refcount, frees snapshot on 0).
4. Implement `ai_config_get_ai_instance(const ai_config_t* cfg)`.
5. Implement `AI_CONFIG_SCOPE` macro in header.

- [ ] **Step 5: Build and run `test_ai_config_provider` to verify PASS**

Run: `cmake --build backend/build --target test_ai_config_provider && backend/build/test_ai_config_provider`
Expected: PASS (all tests including concurrency and RCU reload pass).

- [ ] **Step 6: Commit**

```bash
git add backend/src/application/ai/config_provider.h backend/src/application/ai/config_provider.c backend/tests/unit/test_ai_config_provider.c backend/CMakeLists.txt
git commit -m "feat(ai): 🎸 implement application/ai/config_provider with RCU snapshot and tests"
```

---

### Task 2: AI Probe & Model Discovery Usecase (`application/ai/probe_usecase.h/.c`) & Controller Integration

**Files:**
- Create: `backend/src/application/ai/probe_usecase.h`
- Create: `backend/src/application/ai/probe_usecase.c`
- Modify: `backend/src/interfaces/http/controllers/ai_controller.c:415-445`

**Interfaces:**
- Consumes: `application/ai/config_provider.h` (`ai_config_acquire`, `ai_config_release`)
- Produces:
  ```c
  int ai_usecase_test_connection(const csilk_json_t* body, csilk_json_t** out_resp, ai_usecase_result_t* out_res);
  int ai_usecase_fetch_models(const csilk_json_t* body, csilk_json_t** out_resp, ai_usecase_result_t* out_res);
  ```

- [ ] **Step 1: Create `backend/src/application/ai/probe_usecase.h`**

Declare `ai_usecase_test_connection` and `ai_usecase_fetch_models`.

- [ ] **Step 2: Implement `backend/src/application/ai/probe_usecase.c`**

1. Move `format_friendly_error` helper from `services/ai_service.c` into a shared utility or implement in `probe_usecase.c`.
2. Move connection testing logic from `ai_service_test_connection` into `ai_usecase_test_connection`:
   - Acquires config via `ai_config_acquire()`.
   - Decrypts API key using `auth_key_decrypt` or reads provider key.
   - Instantiates test `csilk_ai_t*` and executes `csilk_ai_chat` with ping message.
   - Computes `latency_ms`.
   - Releases config via `ai_config_release()`.
   - Returns result in `out_resp` (`{success, latency_ms, message}`).
3. Move model fetching logic (`curl_write_memory_cb` and HTTP GET to `/models` or `/api/tags`) from `ai_service_fetch_models` into `ai_usecase_fetch_models`.

- [ ] **Step 3: Update `backend/src/interfaces/http/controllers/ai_controller.c`**

1. Include `"application/ai/probe_usecase.h"`.
2. In `api_ai_test_connection_handler`: replace `ai_service_test_connection(c)` with call to `ai_usecase_test_connection(body, &out_resp, &res)` and `respond_ok(c, out_resp)`.
3. In `api_ai_fetch_models_handler`: replace `ai_service_fetch_models(c)` with call to `ai_usecase_fetch_models(body, &out_resp, &res)` and `respond_ok(c, out_resp)`.

- [ ] **Step 4: Build backend and verify compilation**

Run: `cmake --build backend/build --parallel`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add backend/src/application/ai/probe_usecase.h backend/src/application/ai/probe_usecase.c backend/src/interfaces/http/controllers/ai_controller.c
git commit -m "refactor(ai): ♻️ extract test_connection and fetch_models into probe_usecase"
```

---

### Task 3: Refactor & Slim Down `services/ai_service.c` & `services/ai_service.h`

**Files:**
- Modify: `backend/src/services/ai_service.h`
- Modify: `backend/src/services/ai_service.c`

**Interfaces:**
- Consumes: `application/ai/config_provider.h`
- Produces:
  ```c
  void ai_init(csilk_db_pool_t* pool);
  void ai_shutdown(void);
  void ai_chat_handler(csilk_ctx_t* c);
  void ai_service_stream_report(csilk_ctx_t* c, ...);
  ```

- [ ] **Step 1: Update `backend/src/services/ai_service.h`**

1. Include `"application/ai/config_provider.h"`.
2. Remove deleted declarations: `ai_get_config`, `ai_service_test_connection`, `ai_service_fetch_models`.
3. Keep `ai_init` and `ai_shutdown` for service lifecycle coordination.
4. Keep `ai_chat_handler` and `ai_service_stream_report`.
5. Keep inline compatibility helper `static inline csilk_ai_t* ai_snapshot_ai_instance(const ai_config_t* cfg) { return ai_config_get_ai_instance(cfg); }`.

- [ ] **Step 2: Refactor `backend/src/services/ai_service.c`**

1. Remove `typedef struct ai_config_snapshot_s`, `g_snapshot`, `g_snapshot_lock`, `make_snapshot`, `snapshot_writer_release`.
2. Remove implementations of `ai_config_acquire`, `ai_config_release`, `ai_snapshot_ai_instance`, `ai_get_config`.
3. Remove `ai_service_test_connection`, `curl_write_memory_cb`, `ai_service_fetch_models`.
4. Simplify `ai_init(pool)` to:
   ```c
   void ai_init(csilk_db_pool_t* pool) {
       ai_config_provider_init(pool);
       ai_summary_pool_init();
   }
   ```
5. Simplify `ai_shutdown(void)` to:
   ```c
   void ai_shutdown(void) {
       ai_summary_pool_shutdown();
       int killed = mf_mcp_stdio_pool_shutdown();
       if (killed > 0) {
           CSILK_LOG_I("ai_shutdown: terminated %d idle MCP stdio children", killed);
       }
       ai_config_provider_shutdown();
   }
   ```
6. Verify line count: `wc -l backend/src/services/ai_service.c` is < 550 lines.

- [ ] **Step 3: Build backend and verify compilation**

Run: `cmake --build backend/build --parallel`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add backend/src/services/ai_service.h backend/src/services/ai_service.c
git commit -m "refactor(ai): ♻️ slim down ai_service.c to pure SSE bridge and lifecycle coordinator"
```

---

### Task 4: Complete Migration of Call Sites: Eliminate `ai_get_config()` and Fix Leak in `usecases.c`

**Files:**
- Modify: `backend/src/application/ai/usecases.c:50-75, 315-355, 560-575`
- Modify: `backend/src/interfaces/http/controllers/receipt_controller.c:299-317`
- Modify: `backend/src/services/ai/memory/summary.c:485-515`
- Modify: `backend/src/services/ai/runtime/loop.c:1330-1350`
- Modify: `backend/src/services/ai/runtime/runtime.h:20-30`
- Modify: `backend/src/services/ai/runtime/runtime.c:15-22`
- Modify: `backend/tests/unit/test_ai_summary.c:155-165`

**Interfaces:**
- Consumes: `application/ai/config_provider.h` (`ai_config_acquire`, `ai_config_release`, `ai_config_provider_reload`)

- [ ] **Step 1: Fix `backend/src/application/ai/usecases.c`**

1. Include `"application/ai/config_provider.h"`.
2. In `ai_usecase_models_list` (lines 53-75):
   - Replace `ai_config_t* cfg = ai_get_config();` with `ai_config_t* cfg = ai_config_acquire();`.
   - Add `ai_config_release(cfg);` before normal return and error return paths.
3. In `ai_usecase_settings_get` (line 317):
   - Replace `ai_config_t* cfg = ai_get_config();` with `ai_config_t* cfg = ai_config_acquire();`.
4. In `ai_usecase_settings_update` (lines 566-570):
   - Remove `extern void ai_init` and `extern void ai_shutdown`.
   - Replace `ai_shutdown(); ai_init(db_pool);` with `ai_config_provider_reload(db_pool);`.

- [ ] **Step 2: Update remaining `ai_get_config` call sites across codebase**

1. `backend/src/interfaces/http/controllers/receipt_controller.c`:
   - Replace `ai_config_t* cfg = ai_get_config();` with `ai_config_t* cfg = ai_config_acquire();`.
2. `backend/src/services/ai/memory/summary.c`:
   - Replace `ai_config_t* cfg = ai_get_config();` with `ai_config_t* cfg = ai_config_acquire();`.
3. `backend/src/services/ai/runtime/loop.c`:
   - Replace `ai_config_t* cfg = ai_get_config();` with `ai_config_t* cfg = ai_config_acquire();`.
4. `backend/src/services/ai/runtime/runtime.h` & `runtime.c`:
   - Replace `ai_runtime_get_config(void)` return with `ai_config_acquire()`.
5. `backend/tests/unit/test_ai_summary.c`:
   - Replace `ai_config_t* cfg = ai_get_config();` with `ai_config_t* cfg = ai_config_acquire();`.

- [ ] **Step 3: Verify zero occurrences of `ai_get_config` in `backend/`**

Run: `git grep "ai_get_config" backend/`
Expected: 0 matches.

- [ ] **Step 4: Build backend and verify compilation**

Run: `cmake --build backend/build --parallel`
Expected: PASS with 0 warnings.

- [ ] **Step 5: Commit**

```bash
git add backend/src/application/ai/usecases.c backend/src/interfaces/http/controllers/receipt_controller.c backend/src/services/ai/memory/summary.c backend/src/services/ai/runtime/loop.c backend/src/services/ai/runtime/runtime.h backend/src/services/ai/runtime/runtime.c backend/tests/unit/test_ai_summary.c
git commit -m "fix(ai): 🐛 eliminate ai_get_config, fix models_list ref leak, and wire clean reload"
```

---

### Task 5: End-to-End Build & Test Verification

**Files:**
- Test verification: Unit tests and integration test suites.

- [ ] **Step 1: Run all unit tests via CTest**

Run: `ctest --test-dir backend/build --output-on-failure -R "test_ai_config_provider|test_ai_summary|test_ai_runtime|test_ai_tools|test_ai_policy"`
Expected: 100% tests PASS.

- [ ] **Step 2: Run core integration test suite `test_link.sh`**

Run: `./backend/tests/test_link.sh`
Expected: All 40 test cases and 174 assertions pass without error.

- [ ] **Step 3: Run full backend test suite**

Run: `ctest --test-dir backend/build --output-on-failure`
Expected: 100% tests PASS.

- [ ] **Step 4: Verify frontend build integrity**

Run: `npm --prefix frontend run build`
Expected: Build finishes with 0 TypeScript errors.

- [ ] **Step 5: Final review and commit any leftover test adjustments**

```bash
git status
```
Expected: Working tree clean.
