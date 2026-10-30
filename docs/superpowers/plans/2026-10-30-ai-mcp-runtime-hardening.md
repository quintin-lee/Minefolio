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
└── CMakeLists.txt                         # Modify: register new test targets in CMake
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
    mf_mcp_server_t s = { .timeout_ms = 1000 };
    mf_mcp_client_t* c = mf_mcp_client_new(&s);
    assert(c != NULL);
    assert(c->err_fd == -1);
    assert(c->framer.head == 0 && c->framer.tail == 0);
    assert(sizeof(c->last_error) >= 1024);

    int pipefd[2];
    assert(pipe(pipefd) == 0);
    c->read_fd = pipefd[0];

    const char* frame1 = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"status\":\"ok\"}}\n";
    write(pipefd[1], frame1, strlen(frame1));

    char err[256] = {0};
    /* Call tool using mocked descriptor */
    char* resp = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    assert(resp != NULL);
    assert(strstr(resp, "\"status\":\"ok\"") != NULL);
    free(resp);

    close(pipefd[1]);
    mf_mcp_client_free(c);
    printf("PASS: test_mcp_client_io completed successfully\n");
    return 0;
}
```

- [ ] **Step 2: Register `test_mcp_client_io` in `backend/CMakeLists.txt` and verify compilation fails**

Add to `backend/CMakeLists.txt`:
```cmake
add_executable(test_mcp_client_io tests/unit/test_mcp_client_io.c ${SOURCES})
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
3. In `stdio_call()`, allocate output buffer `resp` with size `MCP_MAX_FRAME_SIZE + MCP_IO_BUFFER_SIZE + 1`.
4. Implement chunked memory scan via `memchr(c->framer.buf + c->framer.head, '\n', ...)` and 4KB `read()` refills.
5. In dual-FD polling: poll both `c->read_fd` and `c->err_fd` (when `>= 0`). On stderr read activity, call `drain_stderr_nonblocking()`. If `read()` on stderr returns 0, close `c->err_fd` and set to `-1` to eliminate `POLLHUP` busy-loops.
6. On timeout (`prc == 0`) or child EOF (`r == 0`), drain any trailing stderr before returning.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build backend/build --target test_mcp_client_io && ./backend/build/test_mcp_client_io`
Expected: PASS

- [ ] **Step 5: Write unit test for anti-deadlock non-blocking stderr drain (`test_mcp_stderr_capture.c`)**

Create `backend/tests/unit/test_mcp_stderr_capture.c`:
Verify that when `c->err_fd` has pending data, `drain_stderr_nonblocking` appends to `c->last_error` without blocking, closes on EOF, and surfaces errors when child process exits.

- [ ] **Step 6: Register test and verify execution**

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

- [ ] **Step 1: Write failing unit test for child process file descriptor isolation (`test_mcp_child_sanitation.c`)**

Create `backend/tests/unit/test_mcp_child_sanitation.c`:
Verify that a spawned script that inspects `/proc/self/fd/` only sees file descriptors 0, 1, 2.

- [ ] **Step 2: Run test to verify failure**

Run: `cmake --build backend/build --target test_mcp_child_sanitation && ./backend/build/test_mcp_child_sanitation`
Expected: FAIL (child process detects leaked descriptors from test runner).

- [ ] **Step 3: Implement post-dup2 `closefrom(3)` in `mcp_client.c`**

In `backend/src/services/ai/tools/mcp/mcp_client.c`:
Ensure `#define _GNU_SOURCE` is present.
In the child process fork branch (`pid == 0`):
1. Execute `dup2(in_pipe[0], STDIN_FILENO)`, `dup2(out_pipe[1], STDOUT_FILENO)`, `dup2(err_pipe[1], STDERR_FILENO)`.
2. Close `in_pipe[0]`, `in_pipe[1]`, `out_pipe[0]`, `out_pipe[1]`, `err_pipe[0]`, `err_pipe[1]`.
3. Call `closefrom(3)` (or scan `/proc/self/fd/` on fallback).
4. In parent fork failure branch, ensure `err_pipe[0]` and `err_pipe[1]` are closed.

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

- [ ] **Step 1: Write failing unit test for parallel tool dispatch (`test_ai_parallel_tools.c`)**

Create `backend/tests/unit/test_ai_parallel_tools.c`:
1. Dispatch 2 mock `web_search` calls with 200ms sleep each; assert total elapsed wall-clock duration is < 300ms.
2. Dispatch 1 `web_search` and 1 local DB query tool; assert fallback to sequential execution.

- [ ] **Step 2: Run test to verify failure**

Run: `cmake --build backend/build --target test_ai_parallel_tools && ./backend/build/test_ai_parallel_tools`
Expected: FAIL

- [ ] **Step 3: Implement parallel dispatch in `src/services/ai/runtime/loop.c`**

In `src/services/ai/runtime/loop.c`:
1. Define `ai_parallel_task_t` with owned strings (`tool_call_id`, `tool_name`, `raw_arguments`).
2. Add static helper `is_stateless_network_tool(name)` checking `web_search` and `fetch_webpage`.
3. In `ai_runtime_execute_stream()`, check if `n_calls > 1 && n_calls <= 8` and all calls satisfy `is_stateless_network_tool()`.
4. If eligible:
   - Serialized pre-evaluation: run `ai_policy_evaluate(..., AI_RISK_NO_OVERRIDE)`, emit `cbs->on_tool_call()`, check and record `seen_hashes` with bounds guard `seen_count < LOOP_DUP_CALL_SLOTS`. Free decision with `ai_policy_decision_free()`.
   - Spawn parallel worker threads (`pthread_create(&threads[t], ...)`), tracking `thread_spawned[t]`. If creation fails, execute inline.
   - Join all spawned workers: `pthread_join(threads[t], NULL)`.
   - Serialized post-gather barrier: in exact turn order `t = 0 .. n_calls - 1`: record tool budget for executed tasks, record trace span, emit `cbs->on_tool_result()`, append to `tc_record`, append clamped tool result to `msgs`, and free owned task memory.
5. If not eligible: keep existing sequential execution loop.

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

- [ ] **Step 1: Write failing unit test for workflow error propagation (`test_workflow_step_error.c`)**

Create `backend/tests/unit/test_workflow_step_error.c`:
Register mock workflow with a failing node returning `NULL`. Assert SSE event `type: "error"` is emitted with both `message` and `error` keys populated, and execution halts.

- [ ] **Step 2: Run test to verify failure**

Run: `cmake --build backend/build --target test_workflow_step_error && ./backend/build/test_workflow_step_error`
Expected: FAIL

- [ ] **Step 3: Implement step validation & error emission in `executor.c`**

In `backend/src/services/ai/workflow/executor.c`:
1. Check `!step_out || !parsed_out`.
2. Free `cur_ctx_str` and `step_out`.
3. Emit SSE event `error` with JSON containing `type: "error"`, `step_index`, `step_id`, `message`, and `error`.
4. Free `ctx_obj` and return -1 without calling terminal LLM stream reporter.

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

- [ ] **Step 1: Write failing unit test for summary worker pool (`test_summary_worker_pool.c`)**

Create `backend/tests/unit/test_summary_worker_pool.c`:
Verify `ai_summary_pool_init()`, non-blocking enqueue, 2 persistent workers processing queue, session slot deduplication, and idempotent `ai_summary_pool_shutdown()` memory cleanup.

- [ ] **Step 2: Run test to verify compilation failure**

Run: `cmake --build backend/build --target test_summary_worker_pool && ./backend/build/test_summary_worker_pool`
Expected: FAIL with `implicit declaration of function 'ai_summary_pool_init'`

- [ ] **Step 3: Implement worker pool in `summary.h` and `summary.c`**

1. In `summary.h`: export `void ai_summary_pool_init(void);` and `void ai_summary_pool_shutdown(void);`.
2. In `summary.c`:
   - Define `summary_worker_pool_t` with 2 workers, 64-capacity queue, mutex, and `not_empty` condition variable.
   - Implement `summary_pool_worker_entry()`: exit immediately on `s_pool.shutdown`.
   - Implement `ai_summary_pool_init()`: reset state, start workers.
   - Implement `ai_summary_pool_shutdown()`: set shutdown, broadcast, join workers, drain queue (free tasks and release inflight slots), and free `s_pending` text.
   - Update `ai_summary_maybe_trigger()` to enqueue tasks non-blockingly; if full, drop task safely.
   - Update slot placement: check matching `session_id` to overwrite, then empty slot, then oldest `created_at`.
3. In `ai_service.c`:
   - Call `ai_summary_pool_init()` at the entry of `ai_init()`.
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
