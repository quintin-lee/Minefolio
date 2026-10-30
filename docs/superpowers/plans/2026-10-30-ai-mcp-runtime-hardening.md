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
│   │   │   │   ├── loop.h                 # Modify: export parallel tool batch helpers
│   │   │   │   └── loop.c                 # Modify: stateless web tool parallel dispatch & gathering barrier
│   │   │   ├── workflow/
│   │   │   │   └── executor.c             # Modify: step_out & parsed_out null checks, emit SSE error
│   │   │   └── memory/
│   │   │       ├── summary.h              # Modify: export pool lifecycle & test enqueue helpers
│   │   │       └── summary.c              # Modify: 2-worker pthread pool, 64-slot LRU deduplication
│   │   └── ai_service.c                   # Modify: call ai_summary_pool_init and ai_summary_pool_shutdown
├── tests/
│   └── unit/
│       ├── test_mcp_client_io.c           # Create: unit test for 4KB block-buffered framer & payload cap
│       ├── test_mcp_child_sanitation.c    # Create: unit test for post-dup2 closefrom(3) isolation
│       ├── test_mcp_stderr_capture.c      # Create: unit test for anti-deadlock non-blocking stderr drain
│       ├── test_ai_parallel_tools.c       # Create: unit test for parallel dispatch vs sequential fallback
│       ├── test_workflow_step_error.c     # Create: unit test for deterministic node error propagation
│       └── test_summary_worker_pool.c     # Create: unit test for 2-worker pool & 20-task queueing
└── CMakeLists.txt                         # Modify: register new test targets using ${LIB_SOURCES}
```

---

## Chunk 1: MCP Block-Buffered I/O Framer & Anti-Deadlock Stderr Drain

### Task 1: Data Structures and Block-Buffered Framer in `mcp_client.h` and `mcp_client.c`

**Files:**
- Modify: `backend/src/services/ai/tools/mcp/mcp_client.h`
- Modify: `backend/src/services/ai/tools/mcp/mcp_client.c`
- Test: `backend/tests/unit/test_mcp_client_io.c`
- Test: `backend/tests/unit/test_mcp_stderr_capture.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: Write unit test for block-buffered framer (`test_mcp_client_io.c`)**

Create `backend/tests/unit/test_mcp_client_io.c`:
```c
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "services/ai/tools/mcp/mcp_client.h"

typedef struct {
    int    pipe_fd;
    size_t total_bytes;
} feeder_arg_t;

static void* huge_stream_feeder(void* arg) {
    feeder_arg_t* a = (feeder_arg_t*)arg;
    char chunk[4096];
    memset(chunk, 'X', sizeof(chunk));
    size_t written = 0;
    while (written < a->total_bytes) {
        size_t to_write = (a->total_bytes - written > sizeof(chunk)) ? sizeof(chunk) : (a->total_bytes - written);
        ssize_t w = write(a->pipe_fd, chunk, to_write);
        if (w <= 0) break;
        written += w;
    }
    close(a->pipe_fd);
    return NULL;
}

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

    int out_pipe[2];
    int in_pipe[2];
    assert(pipe(out_pipe) == 0);
    assert(pipe(in_pipe) == 0);
    c->read_fd = out_pipe[0];
    c->write_fd = in_pipe[1];

    /* Test 1: Single small frame */
    const char* frame1 = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"status\":\"ok\"}}\n";
    assert(write(out_pipe[1], frame1, strlen(frame1)) == (ssize_t)strlen(frame1));

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

    assert(write(out_pipe[1], big_frame, strlen(big_frame)) == (ssize_t)strlen(big_frame));
    char* resp2 = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    assert(resp2 != NULL);
    assert(strlen(resp2) > big_sz);
    free(resp2);
    free(big_frame);

    /* Test 3: Multiple small frames in one buffer (framing cursor advancement) */
    const char* multi = "{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{\"idx\":3}}\n{\"jsonrpc\":\"2.0\",\"id\":4,\"result\":{\"idx\":4}}\n";
    assert(write(out_pipe[1], multi, strlen(multi)) == (ssize_t)strlen(multi));
    char* resp_m1 = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    assert(resp_m1 != NULL);
    assert(strstr(resp_m1, "\"idx\":3") != NULL);
    free(resp_m1);
    char* resp_m2 = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    assert(resp_m2 != NULL);
    assert(strstr(resp_m2, "\"idx\":4") != NULL);
    free(resp_m2);

    /* Close previous out_pipe before Test 4 to avoid leaking descriptors */
    close(out_pipe[1]);
    close(out_pipe[0]);

    /* Test 4: Frame exceeding 1MB cap safely intercepted without pipe deadlock */
    int huge_pipe[2];
    assert(pipe(huge_pipe) == 0);
    c->read_fd = huge_pipe[0];
    feeder_arg_t f_arg = { .pipe_fd = huge_pipe[1], .total_bytes = 1024 * 1024 + 16 };
    pthread_t f_th;
    assert(pthread_create(&f_th, NULL, huge_stream_feeder, &f_arg) == 0);

    char* resp3 = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    pthread_join(f_th, NULL);
    assert(resp3 == NULL);
    assert(strstr(err, "1MB limit") != NULL);

    close(huge_pipe[0]);
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
7. On timeout (`prc == 0`) or child EOF (`r == 0`), drain any trailing stderr before returning. If `c->last_error[0] != '\0'`, format `snprintf(out_err, err_sz, "stdio child stderr: %s", c->last_error)`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build backend/build --target test_mcp_client_io && ./backend/build/test_mcp_client_io`
Expected: PASS

- [ ] **Step 5: Write unit test for anti-deadlock non-blocking stderr drain (`test_mcp_stderr_capture.c`)**

Create `backend/tests/unit/test_mcp_stderr_capture.c`:
```c
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "services/ai/tools/mcp/mcp_client.h"

typedef struct {
    int err_pipe_wr;
    int out_pipe_wr;
} child_sim_t;

static void* verbose_stderr_feeder(void* arg) {
    child_sim_t* sim = (child_sim_t*)arg;
    
    /* 1. First write distinct error string */
    const char* header = "stdio child stderr: Module Crashed on Import!\n";
    assert(write(sim->err_pipe_wr, header, strlen(header)) == (ssize_t)strlen(header));

    /* 2. Write 80 KB padding to prove parent drains without deadlocking child */
    char chunk[4096];
    memset(chunk, 'E', sizeof(chunk));
    size_t total = 80 * 1024;
    size_t written = 0;
    while (written < total) {
        size_t take = (total - written > sizeof(chunk)) ? sizeof(chunk) : (total - written);
        ssize_t w = write(sim->err_pipe_wr, chunk, take);
        if (w <= 0) break;
        written += w;
    }
    
    /* Simulate child exit: close stderr, then close stdout without sending JSON */
    close(sim->err_pipe_wr);
    close(sim->out_pipe_wr);
    return NULL;
}

int main(void) {
    printf("Running test_mcp_stderr_capture...\n");
    mf_mcp_server_t s = {
        .transport = MCP_TRANSPORT_STDIO,
        .timeout_ms = 2000
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

    child_sim_t sim = {
        .err_pipe_wr = err_pipe[1],
        .out_pipe_wr = out_pipe[1]
    };
    pthread_t th;
    assert(pthread_create(&th, NULL, verbose_stderr_feeder, &sim) == 0);

    char err[512] = {0};
    char* resp = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    pthread_join(th, NULL);

    assert(resp == NULL);
    assert(strlen(c->last_error) > 0);
    assert(strstr(err, "stdio child stderr:") != NULL || strstr(err, "Module Crashed") != NULL);

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

    /* Create dummy file descriptors that parent process holds open */
    int dummy_pipe1[2];
    int dummy_pipe2[2];
    assert(pipe(dummy_pipe1) == 0);
    assert(pipe(dummy_pipe2) == 0);

    /* Use bash script checking that only descriptors 0, 1, 2 remain open */
    mf_mcp_server_t s = {
        .transport = MCP_TRANSPORT_STDIO,
        .command = "bash",
        .args_json = "[\"-c\", \"if ls /proc/self/fd | grep -E '^([4-9]|[1-9][0-9]+)$' >/dev/null; then echo '{\\\"error\\\":\\\"leaked_fds\\\"}'; else echo '{\\\"jsonrpc\\\":\\\"2.0\\\",\\\"id\\\":1,\\\"result\\\":{\\\"clean\\\":true}}'; fi\"]",
        .timeout_ms = 4000
    };

    mf_mcp_client_t* c = mf_mcp_client_new(&s);
    assert(c != NULL);

    char err[256] = {0};
    char* resp = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    assert(resp != NULL);
    assert(strstr(resp, "\"clean\":true") != NULL);
    free(resp);

    close(dummy_pipe1[0]);
    close(dummy_pipe1[1]);
    close(dummy_pipe2[0]);
    close(dummy_pipe2[1]);
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
Expected: FAIL (assertion fails with `"clean":true` missing because dummy pipes leaked to child).

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
- Modify: `backend/src/services/ai/runtime/loop.h`
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
#include "services/ai/runtime/loop.h"

static char* mock_sleep_200ms(const ai_tool_t* t, const ai_tool_context_t* ctx, const csilk_json_t* args) {
    (void)t; (void)ctx; (void)args;
    usleep(200000); /* 200ms sleep */
    return strdup("{\"status\":\"slept_200ms\"}");
}

int main(void) {
    printf("Running test_ai_parallel_tools...\n");
    ai_tool_registry_init();

    /* Register mock sleep tool override for web_search */
    ai_tool_t t_search = {
        .name = "web_search",
        .permission = AI_PERM_READ,
        .is_mutation = false,
        .execute = mock_sleep_200ms
    };
    ai_tool_register(&t_search);

    /* 1. Test stateless classification */
    assert(is_stateless_network_tool("web_search") == true);
    assert(is_stateless_network_tool("fetch_webpage") == true);
    assert(is_stateless_network_tool("get_assets") == false);
    assert(is_stateless_network_tool("mcp:1:tool") == false);

    /* 2. Test batch eligibility */
    const char* valid_batch[] = {"web_search", "fetch_webpage"};
    assert(ai_parallel_batch_eligible(valid_batch, 2) == true);
    const char* mixed_batch[] = {"web_search", "get_assets"};
    assert(ai_parallel_batch_eligible(mixed_batch, 2) == false);

    /* 3. Benchmark parallel execution of 2 calls */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    csilk_json_t* a1 = csilk_json_object();
    csilk_json_t* a2 = csilk_json_object();
    char* r1 = NULL;
    char* r2 = NULL;

    int rc = ai_tools_execute_parallel_batch("web_search", a1, "web_search", a2, &r1, &r2);
    clock_gettime(CLOCK_MONOTONIC, &end);
    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;

    assert(rc == 0);
    assert(r1 != NULL && r2 != NULL);
    assert(elapsed_ms < 350); /* Parallel execution takes ~200ms, well below serial 400ms */

    free(r1);
    free(r2);
    csilk_json_free(a1);
    csilk_json_free(a2);

    printf("PASS: test_ai_parallel_tools completed successfully\n");
    return 0;
}
```

- [ ] **Step 2: Register `test_ai_parallel_tools` in CMakeLists.txt and verify compilation failure**

Add to `backend/CMakeLists.txt`:
```cmake
add_executable(test_ai_parallel_tools tests/unit/test_ai_parallel_tools.c ${LIB_SOURCES})
target_include_directories(test_ai_parallel_tools PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_ai_parallel_tools PRIVATE -UNDEBUG)
target_link_libraries(test_ai_parallel_tools PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
add_test(NAME test_ai_parallel_tools COMMAND test_ai_parallel_tools)
```
Run: `cmake --build backend/build --target test_ai_parallel_tools`
Expected: FAIL with `undefined reference to 'is_stateless_network_tool'`

- [ ] **Step 3: Implement parallel dispatch in `loop.h` and `loop.c`**

In `backend/src/services/ai/runtime/loop.h`:
Declare:
```c
bool is_stateless_network_tool(const char* name);
bool ai_parallel_batch_eligible(const char* const* names, size_t count);
int ai_tools_execute_parallel_batch(
    const char* name1, const csilk_json_t* a1,
    const char* name2, const csilk_json_t* a2,
    char** out1, char** out2);
```

In `backend/src/services/ai/runtime/loop.c`:
1. Implement `is_stateless_network_tool()` and `ai_parallel_batch_eligible()`.
2. Define `ai_parallel_task_t`:
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
3. Implement `ai_parallel_tool_worker()` and `ai_tools_execute_parallel_batch()`.
4. In `ai_runtime_execute_stream()`, when `n_calls > 1 && n_calls <= 8` and batch is eligible:
   - Main thread pre-evaluation: populate task, evaluate policy (`AI_RISK_NO_OVERRIDE`), emit `cbs->on_tool_call`, check/record `seen_hashes` with bounds guard `seen_count < LOOP_DUP_CALL_SLOTS`, free `ai_policy_decision_free(decision)`.
   - Thread spawn: for tasks where `!tasks[t].result_str`, spawn `pthread_create(&threads[t], ...)`, setting `thread_spawned[t] = true`. If spawn fails, execute inline via `ai_parallel_tool_worker(&tasks[t])`.
   - Joining: join all `thread_spawned[t]`.
   - Post-gather: iterate `t = 0 .. n_calls - 1` in exact order, record limits if `tasks[t].executed`, add trace span, emit `cbs->on_tool_result`, append to `tc_record`, append `loop_clamp_tool_result()` to `msgs`, and free task heap memory.
5. If batch not eligible: retain existing sequential loop.

- [ ] **Step 4: Run test to verify pass**

Run: `cmake --build backend/build --target test_ai_parallel_tools && ./backend/build/test_ai_parallel_tools`
Expected: PASS

- [ ] **Step 5: Run existing loop hardening regression test**

Run: `bash backend/tests/test_ai_loop_hardening.sh`
Expected: PASS

- [ ] **Step 6: Commit Chunk 3**

```bash
git add backend/src/services/ai/runtime/loop.h backend/src/services/ai/runtime/loop.c backend/tests/unit/test_ai_parallel_tools.c backend/CMakeLists.txt
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
#include "services/ai/workflow/graph.h"
#include "services/ai/workflow/executor.h"

static int s_terminal_called = 0;

static char* mock_fail_node_null(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json) {
    (void)pool; (void)user_id; (void)params; (void)ctx_json;
    return NULL; /* Simulation of SQL failure */
}

static char* mock_fail_node_malformed_json(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json) {
    (void)pool; (void)user_id; (void)params; (void)ctx_json;
    return strdup("{invalid json syntax..."); /* Simulation of invalid output */
}

static char* mock_terminal_node(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json) {
    (void)pool; (void)user_id; (void)params; (void)ctx_json;
    s_terminal_called++;
    return strdup("{\"status\":\"ok\"}");
}

int main(void) {
    printf("Running test_workflow_step_error...\n");

    /* Test Case 1: Node returns NULL -> halts before terminal step */
    ai_workflow_node_t nodes1[2] = {
        { .node_id = "step1", .title = "Null Step", .execute = mock_fail_node_null },
        { .node_id = "step_term", .title = "Terminal Step", .execute = mock_terminal_node }
    };
    ai_workflow_graph_t g1 = { .id = "wf_err1", .title = "WF1", .nodes = nodes1, .node_count = 2 };
    ai_wf_context_t ctx1 = { .user_id = 1, .session_id = 1, .params = csilk_json_object() };

    csilk_ctx_t dummy_http_ctx = {0};
    s_terminal_called = 0;
    int rc1 = ai_workflow_execute_stream(&dummy_http_ctx, &g1, &ctx1);
    assert(rc1 == -1);
    assert(s_terminal_called == 0); /* Proves terminal node was NOT invoked */
    csilk_json_free((csilk_json_t*)ctx1.params);

    /* Test Case 2: Node returns malformed JSON -> halts before terminal step */
    ai_workflow_node_t nodes2[2] = {
        { .node_id = "step2", .title = "Bad JSON Step", .execute = mock_fail_node_malformed_json },
        { .node_id = "step_term", .title = "Terminal Step", .execute = mock_terminal_node }
    };
    ai_workflow_graph_t g2 = { .id = "wf_err2", .title = "WF2", .nodes = nodes2, .node_count = 2 };
    ai_wf_context_t ctx2 = { .user_id = 1, .session_id = 1, .params = csilk_json_object() };

    s_terminal_called = 0;
    int rc2 = ai_workflow_execute_stream(&dummy_http_ctx, &g2, &ctx2);
    assert(rc2 == -1);
    assert(s_terminal_called == 0);
    csilk_json_free((csilk_json_t*)ctx2.params);

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
Run: `cmake --build backend/build --target test_workflow_step_error && ./backend/build/test_workflow_step_error`
Expected: FAIL (assertion `rc1 == -1` fails because `executor.c` currently ignores step failures and executes terminal step).

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
#include <unistd.h>
#include "services/ai/memory/summary.h"
#include "services/ai/runtime/context.h"

int main(void) {
    printf("Running test_summary_worker_pool...\n");
    /* 1. Test idempotent initialization and shutdown */
    ai_summary_pool_init();
    ai_summary_pool_init(); /* duplicate no-op */

    /* 2. Enqueue 20 dummy test tasks and verify queue processing */
    for (int i = 1; i <= 20; i++) {
        int rc = ai_summary_pool_enqueue_task(i, 100 + i, "User prompt for summary test");
        assert(rc == 0);
    }

    /* Allow background workers to consume */
    usleep(150000);

    /* 3. Shutdown and verify queue draining and slot clearing */
    ai_summary_pool_shutdown();
    ai_summary_pool_shutdown(); /* duplicate no-op */

    /* 4. Re-init to verify clean state reset */
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
   int ai_summary_pool_enqueue_task(int64_t session_id, int64_t user_id, const char* prompt);
   ```
2. In `summary.c`:
   - Expand `#define SUMMARY_PENDING_SLOTS 64`.
   - Update `summary_pending_t` to include `time_t created_at;`.
   - Define `summary_worker_pool_t` with 2 workers, 64-capacity queue, mutex, condition variable `not_empty`, and `bool initialized`, `bool shutdown`.
   - Implement `summary_pool_worker_entry()`: on `s_pool.shutdown`, exit immediately without executing queued tasks.
   - Implement `ai_summary_pool_init()`: reset head, tail, count, shutdown=false, start workers, initialized=true.
   - Implement `ai_summary_pool_shutdown()`: set shutdown=true, broadcast not_empty, join workers, drain queue (free tasks and release inflight slots), and free pending slot strings.
   - Implement `ai_summary_pool_enqueue_task()`: acquires lock, queues task or drops if full.
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
