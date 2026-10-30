# AI Agent & MCP Runtime Hardening Design

## 1. Context & Motivation

Minefolio's AI subsystem provides a comprehensive suite of financial ReAct Agent capabilities, custom Model Context Protocol (MCP) tool integration, and deterministic financial diagnosis workflows.

During recent in-depth architectural and code-level audits, three structural bottlenecks and stability risks were identified:
1. **MCP STDIO I/O Syscall Storm**: `mf_mcp_client_call_tool` reads child process stdout byte-by-byte via `read(fd, cur, 1)` with a per-byte `poll()`. For large tool payloads (e.g. 100 KB JSON schemas), this causes over 100,000 user/kernel context switches, leading to CPU thrashing and elevated latency.
2. **Child Process Resource & Debugging Leak**:
   - `fork()` without child-side file descriptor sanitation leaks host TCP listening sockets and open database pool connections to child processes, risking `Address already in use` upon daemon restarts.
   - Child process `stderr` is uncaptured. Script syntax errors or missing environment packages (e.g. Python module import errors) produce silent `EOF` failures with unhelpful `"stdio child produced no response"` messages.
3. **Serial Tool Execution Bottleneck**:
   - When the LLM emits multiple read-only / network-bound tool calls in a single turn (e.g. concurrent `web_search` and remote MCP tools), the runtime executes them strictly sequentially in a `for` loop, multiplying round-trip response time.
4. **Silent Workflow Step Failures & Unpooled Summary Workers**:
   - In `executor.c`, failures in deterministic computation steps (`step_out == NULL` or JSON parse failure) are silently ignored, continuing with empty context and leading downstream LLM steps to hallucinate.
   - In `summary.c`, background summarizer threads are spawned via unpooled `pthread_create`, and a static 16-slot pending array silently discards generated summaries when full.

This design document establishes the architectural specification for hardening the MCP I/O layer, introducing safe parallel tool execution for network/MCP I/O tools, propagating workflow step errors explicitly with frontend protocol alignment, and pooling asynchronous memory workers with clean lifecycle management.

---

## 2. Architecture & Subsystem Design

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Unified AI Runtime Loop                           │
│                      (src/services/ai/runtime/loop.c)                       │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
        ┌──────────────────────────────┴──────────────────────────────┐
        ▼                                                             ▼
┌──────────────────────────────┐                            ┌──────────────────────────────┐
│     Parallel Tool Router     │                            │    Summary Task Queue Pool   │
│ (Network/MCP vs Local DB)    │                            │    (2 Workers, Init/Shutdown)│
└──────────────┬───────────────┘                            └──────────────┬───────────────┘
               │                                                           │
       ┌───────┴───────┐                                           ┌───────┴───────┐
       ▼               ▼                                           ▼               ▼
┌──────────────┐┌──────────────┐                            ┌──────────────┐┌──────────────┐
│ Network/MCP  ││ Local DB &   │                            │ Worker 1     ││ Worker 2     │
│ Read-only    ││ Mutations    │                            │ (LLM Summary)││ (LLM Summary)│
│ Worker Group ││ Sequential   │                            └──────┬───────┘└──────┬───────┘
└──────┬───────┘└──────┬───────┘                                           │
       │               │                                                   ▼
       ▼               ▼                                    ┌──────────────────────────────┐
┌──────────────────────────────┐                            │  64-Slot Associative LRU     │
│  Results Gather Array [t]    │                            │ (timestamp + free on replace)│
│  (Joined -> Ordered Events)  │                            └──────────────┬───────────────┘
└──────────────┬───────────────┘                                           │
               │                                                           ▼
               ▼                                              (Safe main-thread DB flush)
┌──────────────────────────────┐
│  MCP Bridge & Client         │
│  (src/services/ai/tools/mcp) │
└──────────────┬───────────────┘
               │
        ┌──────┴──────────────────────────────────────┐
        ▼                                             ▼
┌──────────────────────────────────────────┐ ┌──────────────────────────────────────────┐
│ mf_mcp_client_t Framer State             │ │ Child Sandbox Post-dup2 Sanitation       │
│ - mcp_io_reader_t framer                 │ │ - dup2(0, 1, 2) FIRST                   │
│ - err_fd + last_error[1024]              │ │ - closefrom(3) AFTER dup2                │
│ - closed in mf_mcp_client_free           │ │ - non-blocking stderr drain to last_error│
└──────────────────────────────────────────┘ └──────────────────────────────────────────┘
```

---

## 3. Detailed Specifications

### 3.1 MCP Block-Buffered I/O Framer (`src/services/ai/tools/mcp/mcp_client.c`)

#### Data Structures & Header Alignment
In `src/services/ai/tools/mcp/mcp_client.h`:
```c
#define MCP_IO_BUFFER_SIZE 4096
#define MCP_MAX_FRAME_SIZE (1 << 20) /* 1 MB */

typedef struct {
    char   buf[MCP_IO_BUFFER_SIZE];
    size_t head;
    size_t tail;
} mcp_io_reader_t;

typedef struct {
    const mf_mcp_server_t* server;
    char*                  session_id;
    int                    read_fd;    /* stdio stdout */
    int                    write_fd;   /* stdio stdin */
    int                    err_fd;     /* stdio stderr (parent read end) */
    pid_t                  child_pid;
    mcp_io_reader_t        framer;     /* persistent across calls on pooled clients */
    char                   last_error[1024]; /* expanded to 1024 to prevent overflow */
} mf_mcp_client_t;
```

#### Lifecycle Management in `mcp_client.c`
- `mf_mcp_client_new()` initializes `c->err_fd = -1`, `c->framer.head = 0`, `c->framer.tail = 0`, and `memset(c->last_error, 0, sizeof(c->last_error))`.
- `mf_mcp_client_free()` closes `c->err_fd` if `>= 0` in addition to `c->read_fd` and `c->write_fd`.

#### Protocol & Framing Rules
1. In `mf_mcp_client_call_tool()`, the persistent framer `c->framer` is consumed.
2. If `c->framer.head == c->framer.tail`, it is reset to 0, and `poll()` + `read(c->read_fd, c->framer.buf, MCP_IO_BUFFER_SIZE)` is called to refill the buffer.
3. `memchr(c->framer.buf + c->framer.head, '\n', c->framer.tail - c->framer.head)` is evaluated in memory.
4. When `\n` is found:
   - Slice bytes up to `\n`, append to output buffer `resp`, advance `head` past `\n`.
   - Strip trailing `\r` and `\n` from `resp`, return result.
5. When `\n` is not found:
   - Append all remaining bytes from `head` to `tail` to `resp`, reset `head = tail = 0`, and repeat read chunk.
6. If total accumulated length exceeds `MCP_MAX_FRAME_SIZE`, fail with `"stdio response frame exceeded 1MB limit"`.

### 3.2 Child Process Sanitation & Stderr Pipe (`src/services/ai/tools/mcp/mcp_client.c`)

#### Child Process Execution Sequence
To prevent closing pipe file descriptors before redirection:
1. In parent:
   - Create `in_pipe[2]`, `out_pipe[2]`, and `err_pipe[2]`.
   - Set `err_pipe[0]` to non-blocking (`fcntl(err_pipe[0], F_SETFL, O_NONBLOCK)`).
2. In child (`pid == 0`):
   - First redirect standard descriptors:
     ```c
     dup2(in_pipe[0], STDIN_FILENO);
     dup2(out_pipe[1], STDOUT_FILENO);
     dup2(err_pipe[1], STDERR_FILENO);
     ```
   - Second, close the original pipe descriptors:
     ```c
     close(in_pipe[0]);  close(in_pipe[1]);
     close(out_pipe[0]); close(out_pipe[1]);
     close(err_pipe[0]); close(err_pipe[1]);
     ```
   - Third, sanitize all remaining file descriptors `>= 3`:
     ```c
     #if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
         closefrom(3);
     #else
         int max_fd = (int)sysconf(_SC_OPEN_MAX);
         if (max_fd < 0 || max_fd > 4096) max_fd = 4096;
         for (int fd = 3; fd < max_fd; fd++) {
             close(fd);
         }
     #endif
     ```
   - Execute `execve(argv[0], argv, envp ? (char* const*)envp : environ)`.
   - On error, `_exit(127)`.
3. In parent:
   - Store `c->child_pid = pid; c->read_fd = out_pipe[0]; c->write_fd = in_pipe[1]; c->err_fd = err_pipe[0];`.
   - Close `in_pipe[0]`, `out_pipe[1]`, `err_pipe[1]`.

#### Stderr Capture on Failure
If child terminates, timeouts, or returns EOF without completing a JSON-RPC frame:
```c
if (c->err_fd >= 0) {
    char err_buf[sizeof(c->last_error)];
    ssize_t n_err = read(c->err_fd, err_buf, sizeof(err_buf) - 1);
    if (n_err > 0) {
        err_buf[n_err] = '\0';
        snprintf(c->last_error, sizeof(c->last_error), "stdio child stderr: %s", err_buf);
        if (out_err && err_sz > 0) {
            snprintf(out_err, err_sz, "%s", c->last_error);
        }
    }
}
```

### 3.3 Parallel Network & MCP Tool Dispatch (`src/services/ai/runtime/loop.c`)

#### Concurrency Boundary: Strict Protection for SQLite Single Connection
Minefolio's `csilk_db_pool_t` with SQLite driver maintains a single underlying `sqlite3*` handle. Concurrent queries on this handle cause `SQLITE_MISUSE` or crashes. Therefore:
- **Eligible for Parallel Execution**: Network-bound, stateless I/O tools:
  1. Built-in web tools: `web_search` and `fetch_webpage`.
  2. Read-only MCP remote tools: Identified via new export `bool mcp_bridge_is_read_only(const char* tool_name)`.
- **Must Remain Serialized**:
  1. All local database tools (`get_assets`, `get_transactions`, `get_cashflow_summary`, `get_portfolio_summary`, etc.).
  2. All mutation/state-altering tools (`is_mutation == true` or `requires_id == true`).

#### Execution and Event Gathering Architecture
When `n_calls > 1`:
1. Tool partition check:
   - If ALL tools in the batch satisfy the parallel criteria (Network / MCP Read-only), dispatch concurrently.
   - If ANY tool touches local database or is a mutation, maintain existing sequential execution.
2. Worker execution:
   - Pre-allocate a result array: `ai_parallel_result_t results[n_calls];`.
   - Launch up to `min(n_calls, 8)` `pthread` workers.
   - Each worker executes its assigned tool call index `t` and writes into `results[t]`.
3. Main thread barrier & ordered event emission:
   - Main thread calls `pthread_join()` on all workers.
   - Once all workers complete, the main thread iterates `for (size_t t = 0; t < n_calls; t++)` in exact turn order:
     - Emits `on_tool_result` via callbacks.
     - Appends `ai_trace_add_tool_span`.
     - Inserts the `role="tool"` message into `msgs` array.
   - This completely eliminates race conditions, preserves exact turn ordering, and avoids mutex-based interleaving.

### 3.4 Deterministic Workflow Error Propagation (`src/services/ai/workflow/executor.c`)

#### Comprehensive Failure Detection & Resource Cleanup
In `src/services/ai/workflow/executor.c`, for deterministic step `i`:
```c
char* cur_ctx_str = csilk_json_serialize(ctx_obj, &slen);
char* step_out = node->execute(pool, user_id, params, cur_ctx_str);
free(cur_ctx_str);

csilk_json_t* parsed_out = step_out ? csilk_json_parse(step_out) : NULL;
if (!step_out || !parsed_out) {
    /* Step execution failed or returned invalid JSON */
    if (step_out) {
        free(step_out);
    }
    
    /* Emit standard error event compatible with frontend stores/chat.ts */
    csilk_json_t* ev_err = csilk_json_object();
    csilk_json_add_string(ev_err, "type", "error");
    csilk_json_add_number(ev_err, "step_index", (double)i);
    csilk_json_add_string(ev_err, "step_id", node->node_id);
    csilk_json_add_string(ev_err, "error", !step_out ? "Node execution failed" : "Malformed JSON returned by node");
    
    char* err_str = csilk_json_serialize(ev_err, &slen);
    csilk_sse_send(c, "error", err_str ? err_str : "{}");
    
    free(err_str);
    csilk_json_free(ev_err);
    csilk_json_free(ctx_obj);
    return -1;
}
```

#### Frontend Contract Alignment
- In `frontend/src/stores/chat.ts` and `frontend/src/api/ai.ts`, the frontend listens for SSE event `error`.
- Emitting `type: "error"` immediately transitions `wfState.status = 'error'`, displays the error notification, and prevents defaulting to `completed`.
- Execution halts before calling the terminal LLM report generator, saving prompt tokens and eliminating hallucinated reports.

### 3.5 Asynchronous Summary Worker Pool (`src/services/ai/memory/summary.c`)

#### Data Structure & Associative Pending Slots
```c
#define SUMMARY_QUEUE_CAP 64
#define SUMMARY_PENDING_SLOTS 64

typedef struct {
    int64_t session_id;
    int64_t user_id;
    char*   text;
    int     completion_tokens;
    time_t  created_at; /* timestamp for LRU replacement */
} summary_pending_t;

typedef struct {
    summary_task_t* queue[SUMMARY_QUEUE_CAP];
    size_t          head;
    size_t          tail;
    size_t          count;
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
    bool            shutdown;
    pthread_t       workers[2];
} summary_worker_pool_t;
```

#### Pool Lifecycle Management
- `void ai_summary_pool_init(void)`:
  - Called during `ai_init()` in `ai_service.c`.
  - Initializes mutex, condition variables, and spawns 2 worker threads running `summary_pool_worker_entry`.
- `void ai_summary_pool_shutdown(void)`:
  - Called during `ai_shutdown()` in `ai_service.c`.
  - Sets `shutdown = true`, broadcasts `not_empty`, joins both worker threads, drains/frees any remaining queued tasks, and destroys synchronizers.

#### Queue Overflow & Associative LRU Replacement
1. **Task Queue Full**:
   - If `count == SUMMARY_QUEUE_CAP`, reject task immediately: free `task->user_prompt`, free `task`, call `inflight_release(task->inflight_idx)`, and log warning. Never leak memory or leave in-flight slot locked.
2. **Pending Slot Replacement**:
   - Worker acquires `s_pending_lock`.
   - If an empty slot exists, use it.
   - If all 64 slots are occupied, find slot `idx` with the oldest `created_at`.
   - Free previous string `free(s_pending[idx].text)`, overwrite with new summary, and update `created_at = time(NULL)`.
3. **Database Flush**:
   - Main request thread in `ai_summary_flush_pending()` locks `s_pending_lock`, extracts matching `session_id`, sets `s_pending[idx].text = NULL`, and performs thread-safe database insertion on the main thread.

---

## 4. Error Handling & Security Matrix

| Failure Condition | Detection Mechanism | Mitigation / Recovery Action | User-Facing Outcome |
| :--- | :--- | :--- | :--- |
| MCP child script crashes (e.g. missing import) | `read(stdout)` returns EOF; `err_fd` has bytes | Read up to 1024B from `err_fd`, populate `c->last_error` | Explicit error: `"stdio child stderr: ImportError: No module named 'mcp'"` |
| Malicious / runaway MCP payload (>1MB) | Byte counter in framer exceeds `MCP_MAX_FRAME_SIZE` | Abort read, close pipes, mark client error | Tool returns error: `"payload exceeded 1MB limit"` |
| Client disconnects during parallel tool execution | `*ctx->cancel_token == true` | Workers observe cancel token, skip long operations, join | In-flight execution cleanly halted; no orphaned threads |
| Child inherits listening socket / DB pool | Child executes `closefrom(3)` after `dup2(0,1,2)` | All inherited file descriptors > 2 closed | Zero socket leaks or daemon restart lockups |
| Deterministic calculation SQL failure | `node->execute` returns NULL or invalid JSON | Free `cur_ctx_str`, emit SSE `type: "error"`, abort | Client UI transitions to error status; 0 token waste |
| Summary queue flooded by burst chat turns | Task queue reaches capacity (64) | Free `task->user_prompt`, release inflight slot | Safe backpressure; no memory leak or thread exhaustion |

---

## 5. Verification & Testing Strategy

### 5.1 Unit Tests
1. **`test_mcp_client_io.c`**:
   - Feed multi-kilobyte JSON-RPC responses (10KB, 100KB, 500KB) through a pipe to `mf_mcp_client_call_tool()`.
   - Assert `mcp_io_reader_t` correctly extracts single and multiple frames across buffer boundaries without dropping bytes.
   - Assert that payloads exceeding 1MB cleanly fail with expected error code.
2. **`test_mcp_child_sanitation.c`**:
   - Spawn mock child script that inspects `/proc/self/fd/`.
   - Assert that only descriptors 0, 1, 2 exist in child process.
3. **`test_mcp_stderr_capture.c`**:
   - Spawn script writing to stderr and exiting with code 1.
   - Assert `c->last_error` contains the captured stderr text.
4. **`test_ai_parallel_tools.c`**:
   - Dispatch 2 mock `web_search` queries with 200ms sleep each.
   - Assert total wall-clock duration is < 300ms (parallel), compared to 400ms (serial).
   - Dispatch 1 `web_search` and 1 local `get_assets` query; assert fallback to sequential execution to protect SQLite single connection.
5. **`test_workflow_step_error.c`**:
   - Register workflow with a failing intermediate node.
   - Assert SSE event `type: "error"` is emitted, `cur_ctx_str` is freed, and terminal LLM report generator is not invoked.
6. **`test_summary_worker_pool.c`**:
   - Enqueue 20 concurrent summary tasks.
   - Assert tasks are processed by the 2 persistent worker threads without spawning additional pthreads.
   - Assert clean shutdown via `ai_summary_pool_shutdown()` without thread leaks or hung processes.

### 5.2 Regression Tests
- Execute full existing regression test suite:
  - `tests/test_ai_tool_call.sh`
  - `tests/test_ai_loop_hardening.sh`
  - `tests/test_mcp.sh`
