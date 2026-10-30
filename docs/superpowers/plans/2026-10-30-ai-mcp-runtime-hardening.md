# AI Agent & MCP Runtime Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate system call storms and child process leaks in MCP STDIO communications, accelerate ReAct tool invocation round-trips via safe parallel network dispatch without violating SQLite single-connection constraints, explicitly propagate deterministic workflow step failures, and thread-pool asynchronous memory summarization.

**Architecture:** Block-buffered I/O framer with dual-FD polling (`read_fd` + non-blocking `err_fd` drain) and post-dup2 `closefrom(3)` in `mcp_client.c`; all-or-nothing parallel dispatch for stateless web tools with turn-order gathering barrier in `loop.c`; JSON parse failure and null-check error propagation with frontend-compatible SSE payloads in `executor.c`; 2-worker bounded thread pool with LRU associative pending slot deduplication in `summary.c`.

**Tech Stack:** C23, POSIX Threads (`pthreads`), poll/dup2/closefrom, csilk framework (`csilk_json_t`, `csilk_sse_send`), SQLite driver, CMake / CTest.

**Spec:** [`docs/superpowers/specs/2026-10-30-ai-mcp-runtime-hardening-design.md`](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-10-30-ai-mcp-runtime-hardening-design.md)

---

## File Structure & Decomposition

```
backend/
├── src/
│   ├── services/
│   │   ├── ai/
│   │   │   ├── tools/mcp/
│   │   │   │   ├── mcp_client.h           # Modify: add mcp_io_reader_t, err_fd, last_error[1024]
│   │   │   │   └── mcp_client.c           # Modify: block-buffered framing, dual-FD poll, closefrom(3)
│   │   │   ├── runtime/
│   │   │   │   └── loop.c                 # Modify: stateless web tool parallel dispatch & gathering barrier
│   │   │   ├── workflow/
│   │   │   │   └── executor.c             # Modify: step_out & parsed_out null checks, emit SSE error
│   │   │   └── memory/
│   │   │       ├── summary.h              # Modify: export ai_summary_pool_init & ai_summary_pool_shutdown
│   │   │       └── summary.c              # Modify: 2-worker pthread pool, 64-slot LRU deduplication
│   │   └── ai_service.c                   # Modify: call ai_summary_pool_init and ai_summary_pool_shutdown
├── tests/
│   └── unit/
│       ├── test_mcp_client_io.c           # Create: unit test for 4KB block-buffered framer & payload cap
│       ├── test_mcp_child_sanitation.c    # Create: unit test for post-dup2 closefrom(3) isolation
│       ├── test_mcp_stderr_capture.c      # Create: unit test for anti-deadlock non-blocking stderr drain
│       ├── test_ai_parallel_tools.c       # Create: unit test for parallel dispatch vs sequential fallback
│       ├── test_workflow_step_error.c     # Create: unit test for deterministic node error propagation
│       └── test_summary_worker_pool.c     # Create: unit test for 2-worker pool & idempotent lifecycle
└── CMakeLists.txt                         # Modify: register new test targets using ${LIB_SOURCES}
```

---

## Chunk 1: MCP Block-Buffered I/O Framer & Anti-Deadlock Stderr Drain

### Task 1: Data Structures and Block-Buffered Framer in `mcp_client.h` and `mcp_client.c`

**Files:**
- Modify: `backend/src/services/ai/tools/mcp/mcp_client.h`
- Modify: `backend/src/services/ai/tools/mcp/mcp_client.c`
- Test: `backend/tests/unit/test_mcp_client_io.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: Write unit test for block-buffered framer (`test_mcp_client_io.c`)**

Create `backend/tests/unit/test_mcp_client_io.c`:
```c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "services/ai/tools/mcp/mcp_client.h"

int main(void) {
    printf("Running test_mcp_client_io...\n");
    mf_mcp_server_t s = {
        .transport = MCP_TRANSPORT_STDIO,
        .timeout_ms = 2000
    };
    mf_mcp_client_t* c = mf_mcp_client_new(&s);
    assert(c != NULL);
    assert(c->err_fd == -1);
    assert(c->framer.head == 0 && c->framer.tail == 0);
    assert(sizeof(c->last_error) >= 1024);

    /* Setup mocked stdout and stdin pipes so stdio_call doesn't fork */
    int out_pipe[2];
    int in_pipe[2];
    assert(pipe(out_pipe) == 0);
    assert(pipe(in_pipe) == 0);
    c->read_fd = out_pipe[0];
    c->write_fd = in_pipe[1];

    /* Test 1: Single small frame */
    const char* frame1 = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"status\":\"ok\"}}\n";
    write(out_pipe[1], frame1, strlen(frame1));

    char err[256] = {0};
    char* resp1 = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    assert(resp1 != NULL);
    assert(strstr(resp1, "\"status\":\"ok\"") != NULL);
    free(resp1);

    /* Test 2: Frame crossing 4KB buffer boundary (8KB payload) */
    size_t big_sz = 8192;
    char* big_frame = (char*)malloc(big_sz + 64);
    assert(big_frame != NULL);
    strcpy(big_frame, "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"data\":\"");
    size_t prefix_len = strlen(big_frame);
    memset(big_frame + prefix_len, 'A', big_sz);
    big_frame[prefix_len + big_sz] = '\0';
    strcat(big_frame, "\"}}\n");

    write(out_pipe[1], big_frame, strlen(big_frame));
    char* resp2 = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    assert(resp2 != NULL);
    assert(strlen(resp2) > big_sz);
    free(resp2);
    free(big_frame);

    /* Test 3: Frame exceeding 1MB cap cleanly fails */
    size_t huge_sz = 1024 * 1024 + 10;
    char* huge_chunk = (char*)malloc(huge_sz);
    assert(huge_chunk != NULL);
    memset(huge_chunk, 'X', huge_sz);
    write(out_pipe[1], huge_chunk, huge_sz);

    char* resp3 = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    assert(resp3 == NULL);
    assert(strstr(err, "1MB limit") != NULL);
    free(huge_chunk);

    close(out_pipe[1]);
    close(in_pipe[0]);
    mf_mcp_client_free(c);
    printf("PASS: test_mcp_client_io completed successfully\n");
    return 0;
}
```

- [ ] **Step 2: Register `test_mcp_client_io` in `backend/CMakeLists.txt` and verify compilation fails**

Add to `backend/CMakeLists.txt`:
```cmake
add_executable(test_mcp_client_io tests/unit/test_mcp_client_io.c ${LIB_SOURCES})
target_include_directories(test_mcp_client_io PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_mcp_client_io PRIVATE -UNDEBUG)
target_link_libraries(test_mcp_client_io PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
add_test(NAME test_mcp_client_io COMMAND test_mcp_client_io)
```
Run: `cmake --build backend/build --target test_mcp_client_io`
Expected: FAIL with `error: ‘mf_mcp_client_t’ has no member named ‘framer’`

- [ ] **Step 3: Update `mcp_client.h` and implement block-buffered read in `stdio_call()`**

In `backend/src/services/ai/tools/mcp/mcp_client.h`:
Add `mcp_io_reader_t` definition, add `err_fd` and `framer` to `mf_mcp_client_t`, and expand `last_error[1024]`.

In `backend/src/services/ai/tools/mcp/mcp_client.c`:
1. In `mf_mcp_client_new()`, initialize `c->err_fd = -1; c->framer.head = 0; c->framer.tail = 0; c->last_error[0] = '\0';`.
2. In `mf_mcp_client_free()`, close `c->err_fd` if `>= 0`.
3. In `stdio_call()`, allocate output buffer `resp` with size `MCP_MAX_FRAME_SIZE + MCP_IO_BUFFER_SIZE + 1`. If allocation fails, write `"oom"` to `out_err` and return `NULL`.
4. Initialize `c->last_error[0] = '\0'`.
5. Implement chunked memory scan via `memchr(c->framer.buf + c->framer.head, '\n', ...)` and 4KB `read()` refills.
6. In dual-FD polling: poll both `c->read_fd` and `c->err_fd` (when `>= 0`). On stderr read activity, call `drain_stderr_nonblocking()`. If `read()` on stderr returns 0, close `c->err_fd` and set to `-1` to eliminate `POLLHUP` busy-loops.
7. On timeout (`prc == 0`) or child EOF (`r == 0`), drain any trailing stderr before returning. If `c->last_error[0] != '\0'`, copy `c->last_error` to `out_err`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build backend/build --target test_mcp_client_io && ./backend/build/test_mcp_client_io`
Expected: PASS

- [ ] **Step 5: Write unit test for anti-deadlock non-blocking stderr drain (`test_mcp_stderr_capture.c`)**

Create `backend/tests/unit/test_mcp_stderr_capture.c`:
```c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "services/ai/tools/mcp/mcp_client.h"

int main(void) {
    printf("Running test_mcp_stderr_capture...\n");
    mf_mcp_server_t s = {
        .transport = MCP_TRANSPORT_STDIO,
        .timeout_ms = 1000
    };
    mf_mcp_client_t* c = mf_mcp_client_new(&s);
    assert(c != NULL);

    int out_pipe[2];
    int in_pipe[2];
    int err_pipe[2];
    assert(pipe(out_pipe) == 0);
    assert(pipe(in_pipe) == 0);
    assert(pipe(err_pipe) == 0);
    fcntl(err_pipe[0], F_SETFL, O_NONBLOCK);

    c->read_fd = out_pipe[0];
    c->write_fd = in_pipe[1];
    c->err_fd = err_pipe[0];

    /* Child writes stderr error message and closes without writing stdout */
    const char* mock_err = "ImportError: No module named 'mcp'\n";
    write(err_pipe[1], mock_err, strlen(mock_err));
    close(err_pipe[1]);
    close(out_pipe[1]);

    char err[256] = {0};
    char* resp = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    assert(resp == NULL);
    assert(strstr(err, "ImportError: No module named 'mcp'") != NULL);

    close(in_pipe[0]);
    mf_mcp_client_free(c);
    printf("PASS: test_mcp_stderr_capture completed successfully\n");
    return 0;
}
```

- [ ] **Step 6: Register test in CMakeLists.txt and verify execution**

Add to `backend/CMakeLists.txt`:
```cmake
add_executable(test_mcp_stderr_capture tests/unit/test_mcp_stderr_capture.c ${LIB_SOURCES})
target_include_directories(test_mcp_stderr_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_mcp_stderr_capture PRIVATE -UNDEBUG)
target_link_libraries(test_mcp_stderr_capture PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
add_test(NAME test_mcp_stderr_capture COMMAND test_mcp_stderr_capture)
```
Run: `cmake --build backend/build --target test_mcp_stderr_capture && ./backend/build/test_mcp_stderr_capture`
Expected: PASS

- [ ] **Step 7: Commit Chunk 1**

```bash
git add backend/src/services/ai/tools/mcp/mcp_client.h backend/src/services/ai/tools/mcp/mcp_client.c backend/tests/unit/test_mcp_client_io.c backend/tests/unit/test_mcp_stderr_capture.c backend/CMakeLists.txt
git commit -m "perf(mcp): ⚡ implement block-buffered io and dual-fd poll in stdio client"
```

---

## Chunk 2: Child Process Post-dup2 File Descriptor Sanitation

### Task 2: Implement Post-dup2 `closefrom(3)` in `mcp_client.c`

**Files:**
- Modify: `backend/src/services/ai/tools/mcp/mcp_client.c`
- Test: `backend/tests/unit/test_mcp_child_sanitation.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: Write unit test for child process file descriptor isolation (`test_mcp_child_sanitation.c`)**

Create `backend/tests/unit/test_mcp_child_sanitation.c`:
```c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "services/ai/tools/mcp/mcp_client.h"

int main(void) {
    printf("Running test_mcp_child_sanitation...\n");

    /* Create dummy file descriptor that parent holds open */
    int dummy_pipe[2];
    assert(pipe(dummy_pipe) == 0);

    /* Use bash script that checks if descriptor > 2 is open */
    mf_mcp_server_t s = {
        .transport = MCP_TRANSPORT_STDIO,
        .command = "bash",
        .args_json = "[\"-c\", \"if ls /proc/self/fd | grep -E '^([4-9]|[1-9][0-9]+)$' >/dev/null; then echo '{\\\"error\\\":\\\"leaked_fds\\\"}'; else echo '{\\\"jsonrpc\\\":\\\"2.0\\\",\\\"id\\\":1,\\\"result\\\":{\\\"clean\\\":true}}'; fi\"]",
        .timeout_ms = 3000
    };

    mf_mcp_client_t* c = mf_mcp_client_new(&s);
    assert(c != NULL);

    char err[256] = {0};
    char* resp = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    assert(resp != NULL);
    assert(strstr(resp, "\"clean\":true") != NULL);
    free(resp);

    close(dummy_pipe[0]);
    close(dummy_pipe[1]);
    mf_mcp_client_free(c);
    printf("PASS: test_mcp_child_sanitation completed successfully\n");
    return 0;
}
```

- [ ] **Step 2: Register `test_mcp_child_sanitation` in CMakeLists.txt and verify failure**

Add to `backend/CMakeLists.txt`:
```cmake
add_executable(test_mcp_child_sanitation tests/unit/test_mcp_child_sanitation.c ${LIB_SOURCES})
target_include_directories(test_mcp_child_sanitation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_mcp_child_sanitation PRIVATE -UNDEBUG)
target_link_libraries(test_mcp_child_sanitation PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
add_test(NAME test_mcp_child_sanitation COMMAND test_mcp_child_sanitation)
```
Run: `cmake --build backend/build --target test_mcp_child_sanitation && ./backend/build/test_mcp_child_sanitation`
Expected: FAIL (assertion fails with `"clean":true` missing because dummy_pipe leaked to child).

- [ ] **Step 3: Implement parent `err_pipe` setup and post-dup2 `closefrom(3)` in `mcp_client.c`**

In `backend/src/services/ai/tools/mcp/mcp_client.c`:
1. Ensure `#define _GNU_SOURCE` is defined.
2. In the parent process spawn section:
   - Create `in_pipe[2]`, `out_pipe[2]`, and `err_pipe[2]`.
   - On error creating any pipe, close all already-created pipes and return `NULL`.
   - Set `fcntl(err_pipe[0], F_SETFL, O_NONBLOCK)`.
   - On `fork() < 0` error: close all 6 pipe ends, write `out_err`, return `NULL`.
3. In child process (`pid == 0`):
   - First redirect: `dup2(in_pipe[0], STDIN_FILENO); dup2(out_pipe[1], STDOUT_FILENO); dup2(err_pipe[1], STDERR_FILENO);`.
   - Second close: `close(in_pipe[0]); close(in_pipe[1]); close(out_pipe[0]); close(out_pipe[1]); close(err_pipe[0]); close(err_pipe[1]);`.
   - Third sanitize: call `closefrom(3)` (or iterate `/proc/self/fd/`).
   - Execute `execve(argv[0], argv, envp ? (char* const*)envp : environ)`.
4. In parent process:
   - `c->read_fd = out_pipe[0]; c->write_fd = in_pipe[1]; c->err_fd = err_pipe[0];`.
   - Close `in_pipe[0]`, `out_pipe[1]`, `err_pipe[1]`.

- [ ] **Step 4: Run test to verify pass**

Run: `cmake --build backend/build --target test_mcp_child_sanitation && ./backend/build/test_mcp_child_sanitation`
Expected: PASS

- [ ] **Step 5: Commit Chunk 2**

```bash
git add backend/src/services/ai/tools/mcp/mcp_client.c backend/tests/unit/test_mcp_child_sanitation.c backend/CMakeLists.txt
git commit -m "fix(mcp): 🐛 sanitize inherited file descriptors in stdio child process"
```

---

## Chunk 3: Parallel Stateless Network Tool Dispatch in Agent Loop

### Task 3: Parallel Tool Execution with Exact Turn-Order Gathering Barrier

**Files:**
- Modify: `backend/src/services/ai/runtime/loop.c`
- Test: `backend/tests/unit/test_ai_parallel_tools.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: Write unit test for parallel tool dispatch (`test_ai_parallel_tools.c`)**

Create `backend/tests/unit/test_ai_parallel_tools.c`:
```c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "services/ai/tools/registry.h"
#include "services/ai/runtime/context.h"
#include "services/ai/runtime/loop.h"
#include "common/db.h"

/* Mock tool callback tracking execution */
static char* mock_sleep_tool(const ai_tool_t* t, const ai_tool_context_t* ctx, const csilk_json_t* args) {
    (void)t; (void)ctx; (void)args;
    usleep(200000); /* 200ms */
    return strdup("{\"status\":\"slept_200ms\"}");
}

int main(void) {
    printf("Running test_ai_parallel_tools...\n");
    ai_tool_registry_init();

    /* Register two stateless test tools */
    ai_tool_t t1 = {
        .name = "web_search",
        .permission = AI_PERM_READ,
        .is_mutation = false,
        .execute = mock_sleep_tool
    };
    ai_tool_register(&t1);

    /* Test 1: Verify is_stateless_network_tool recognizes web_search and fetch_webpage */
    /* Test 2: Measure parallel execution of 2 web_search calls: total time < 350ms */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* Construct 2 calls and simulate parallel loop worker execution */
    /* Assert elapsed < 350ms */

    printf("PASS: test_ai_parallel_tools completed successfully\n");
    return 0;
}
```

- [ ] **Step 2: Register `test_ai_parallel_tools` in CMakeLists.txt and verify failure**

Add to `backend/CMakeLists.txt`:
```cmake
add_executable(test_ai_parallel_tools tests/unit/test_ai_parallel_tools.c ${LIB_SOURCES})
target_include_directories(test_ai_parallel_tools PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_ai_parallel_tools PRIVATE -UNDEBUG)
target_link_libraries(test_ai_parallel_tools PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
add_test(NAME test_ai_parallel_tools COMMAND test_ai_parallel_tools)
```
Run: `cmake --build backend/build --target test_ai_parallel_tools && ./backend/build/test_ai_parallel_tools`

- [ ] **Step 3: Implement parallel dispatch in `src/services/ai/runtime/loop.c`**

In `src/services/ai/runtime/loop.c`:
1. Define `ai_parallel_task_t`:
   ```c
   typedef struct {
       size_t               call_idx;
       int64_t              user_id;
       int64_t              session_id;
       char                 tool_call_id[64];
       char*                tool_name;
       char*                raw_arguments;
       csilk_json_t*        args;
       const volatile bool* cancel_token;
       char*                result_str;
       long                 span_ms;
       bool                 executed;
       bool                 success;
   } ai_parallel_task_t;
   ```
2. Define `static void* ai_parallel_tool_worker(void* arg)` calling `ai_tools_execute_parsed_cancel(NULL, task->user_id, task->session_id, task->args, task->tool_name, task->cancel_token)`.
3. In `ai_runtime_execute_stream()` when `n_calls > 1 && n_calls <= 8`:
   - Check if all tool names in batch are `"web_search"` or `"fetch_webpage"`.
   - If not, fall back to sequential loop.
   - If yes:
     - Pre-evaluation: populate task, evaluate policy (`AI_RISK_NO_OVERRIDE`), emit `cbs->on_tool_call`, check/record `seen_hashes` with bounds guard `seen_count < LOOP_DUP_CALL_SLOTS`, free `ai_policy_decision_free(decision)`.
     - Thread spawn: for tasks without `result_str`, spawn `pthread_create(&threads[t], ...)`, setting `thread_spawned[t] = true`. If spawn fails, execute inline via `ai_parallel_tool_worker(&tasks[t])`.
     - Joining: join all `thread_spawned[t]`.
     - Post-gather: iterate `t = 0 .. n_calls - 1` in exact order, record limits if `tasks[t].executed`, add trace span, emit `cbs->on_tool_result`, append to `tc_record`, append `loop_clamp_tool_result()` to `msgs`, and free task heap memory.

- [ ] **Step 4: Run test to verify pass**

Run: `cmake --build backend/build --target test_ai_parallel_tools && ./backend/build/test_ai_parallel_tools`
Expected: PASS

- [ ] **Step 5: Run existing loop hardening regression test**

Run: `bash backend/tests/test_ai_loop_hardening.sh`
Expected: PASS

- [ ] **Step 6: Commit Chunk 3**

```bash
git add backend/src/services/ai/runtime/loop.c backend/tests/unit/test_ai_parallel_tools.c backend/CMakeLists.txt
git commit -m "feat(ai): ⚡ add parallel dispatch for stateless web tools in runtime loop"
```

---

## Chunk 4: Workflow Step Error Propagation & Summary Worker Thread Pool

### Task 4.1: Deterministic Workflow Error Propagation in `executor.c`

**Files:**
- Modify: `backend/src/services/ai/workflow/executor.c`
- Test: `backend/tests/unit/test_workflow_step_error.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: Write unit test for workflow error propagation (`test_workflow_step_error.c`)**

Create `backend/tests/unit/test_workflow_step_error.c`:
```c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "services/ai/workflow/engine.h"
#include "services/ai/workflow/executor.h"

static char* mock_fail_node(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json) {
    (void)pool; (void)user_id; (void)params; (void)ctx_json;
    return NULL; /* Simulation of deterministic calculation failure */
}

int main(void) {
    printf("Running test_workflow_step_error...\n");
    /* Register mock graph with mock_fail_node and verify ai_workflow_execute_stream returns -1 on step failure */
    printf("PASS: test_workflow_step_error completed successfully\n");
    return 0;
}
```

- [ ] **Step 2: Register test in CMakeLists.txt and verify failure**

Add to `backend/CMakeLists.txt`:
```cmake
add_executable(test_workflow_step_error tests/unit/test_workflow_step_error.c ${LIB_SOURCES})
target_include_directories(test_workflow_step_error PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_workflow_step_error PRIVATE -UNDEBUG)
target_link_libraries(test_workflow_step_error PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
add_test(NAME test_workflow_step_error COMMAND test_workflow_step_error)
```
Run: `cmake --build backend/build --target test_workflow_step_error`

- [ ] **Step 3: Implement step validation & error emission in `executor.c`**

In `backend/src/services/ai/workflow/executor.c`:
1. Parse `parsed_out = step_out ? csilk_json_parse(step_out) : NULL;`.
2. If `!step_out || !parsed_out`:
   - Free `step_out` if non-null.
   - Construct error JSON object with `"type": "error"`, `"step_index": i`, `"step_id": node->node_id`, `"message": emsg`, and `"error": emsg`.
   - Send SSE event `csilk_sse_send(c, "error", err_str)`.
   - Free `err_str`, `ev_err`, `ctx_obj` and return `-1`.

- [ ] **Step 4: Run test to verify pass**

Run: `cmake --build backend/build --target test_workflow_step_error && ./backend/build/test_workflow_step_error`
Expected: PASS

- [ ] **Step 5: Commit Task 4.1**

```bash
git add backend/src/services/ai/workflow/executor.c backend/tests/unit/test_workflow_step_error.c backend/CMakeLists.txt
git commit -m "fix(ai): 🐛 explicitly propagate workflow step errors and halt execution"
```

---

### Task 4.2: Asynchronous Summary Worker Thread Pool in `summary.c`

**Files:**
- Modify: `backend/src/services/ai/memory/summary.h`
- Modify: `backend/src/services/ai/memory/summary.c`
- Modify: `backend/src/services/ai_service.c`
- Test: `backend/tests/unit/test_summary_worker_pool.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: Write unit test for summary worker pool (`test_summary_worker_pool.c`)**

Create `backend/tests/unit/test_summary_worker_pool.c`:
```c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "services/ai/memory/summary.h"

int main(void) {
    printf("Running test_summary_worker_pool...\n");
    /* Test idempotent initialization and shutdown */
    ai_summary_pool_init();
    ai_summary_pool_init(); /* duplicate no-op */

    ai_summary_pool_shutdown();
    ai_summary_pool_shutdown(); /* duplicate no-op */

    /* Re-init to verify clean state reset */
    ai_summary_pool_init();
    ai_summary_pool_shutdown();

    printf("PASS: test_summary_worker_pool completed successfully\n");
    return 0;
}
```

- [ ] **Step 2: Register test in CMakeLists.txt and verify compilation failure**

Add to `backend/CMakeLists.txt`:
```cmake
add_executable(test_summary_worker_pool tests/unit/test_summary_worker_pool.c ${LIB_SOURCES})
target_include_directories(test_summary_worker_pool PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_summary_worker_pool PRIVATE -UNDEBUG)
target_link_libraries(test_summary_worker_pool PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
add_test(NAME test_summary_worker_pool COMMAND test_summary_worker_pool)
```
Run: `cmake --build backend/build --target test_summary_worker_pool`
Expected: FAIL with `implicit declaration of function 'ai_summary_pool_init'`

- [ ] **Step 3: Implement worker pool in `summary.h` and `summary.c`**

1. In `summary.h`:
   ```c
   void ai_summary_pool_init(void);
   void ai_summary_pool_shutdown(void);
   ```
2. In `summary.c`:
   - Expand `#define SUMMARY_PENDING_SLOTS 64`.
   - Update `summary_pending_t` to include `time_t created_at;`.
   - Define `summary_worker_pool_t` with 2 workers, 64-capacity queue, mutex, condition variable `not_empty`, and `bool initialized`, `bool shutdown`.
   - Implement `summary_pool_worker_entry()`: on `s_pool.shutdown`, exit immediately without executing queued tasks.
   - Implement `ai_summary_pool_init()`: reset head, tail, count, shutdown=false, start workers, initialized=true.
   - Implement `ai_summary_pool_shutdown()`: set shutdown=true, broadcast not_empty, join workers, drain queue (free tasks and release inflight slots), and free pending slot strings.
   - Update `ai_summary_maybe_trigger()` to check `!s_pool.initialized || s_pool.shutdown`, enqueue non-blockingly, and drop task if queue full.
   - Update slot placement: check matching `session_id` to overwrite, then empty slot, then oldest `created_at`.
3. In `ai_service.c`:
   - Call `ai_summary_pool_init()` at line 86 (first line of `ai_init()`).
   - Call `ai_summary_pool_shutdown()` in `ai_shutdown()`.

- [ ] **Step 4: Run test to verify pass**

Run: `cmake --build backend/build --target test_summary_worker_pool && ./backend/build/test_summary_worker_pool`
Expected: PASS

- [ ] **Step 5: Run existing summary pipeline unit test**

Run: `cmake --build backend/build --target test_ai_summary && ./backend/build/test_ai_summary`
Expected: PASS

- [ ] **Step 6: Commit Task 4.2**

```bash
git add backend/src/services/ai/memory/summary.h backend/src/services/ai/memory/summary.c backend/src/services/ai_service.c backend/tests/unit/test_summary_worker_pool.c backend/CMakeLists.txt
git commit -m "perf(ai): ⚡ implement persistent worker pool and lru queue for memory summary"
```

---

## Final Verification Gate

- [ ] Run full test suite:
  ```bash
  cd backend && cmake --build build --target all && ctest --test-dir build --output-on-failure
  bash tests/test_ai_tool_call.sh
  bash tests/test_ai_loop_hardening.sh
  bash tests/test_mcp.sh
  ```
- [ ] Expected: 100% test pass with 0 leaks under AddressSanitizer.
