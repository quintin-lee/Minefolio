# AI Agent & MCP Runtime Hardening Design

## 1. Context & Motivation

Minefolio's AI subsystem provides a comprehensive suite of financial ReAct Agent capabilities, custom Model Context Protocol (MCP) tool integration, and deterministic financial diagnosis workflows.

During recent in-depth architectural and code-level audits, four structural bottlenecks and stability risks were identified:
1. **MCP STDIO I/O Syscall Storm**: In `src/services/ai/tools/mcp/mcp_client.c`, `stdio_call` reads child process stdout byte-by-byte via `read(fd, cur, 1)` with a per-byte `poll()`. For large tool payloads (e.g. 100 KB JSON schemas), this causes over 100,000 user/kernel context switches, leading to CPU thrashing and elevated latency across `initialize`, `tools/list`, and `tools/call`.
2. **Child Process Resource & Debugging Leak**:
   - `fork()` without child-side file descriptor sanitation leaks host TCP listening sockets and open database pool connections to child processes, risking `Address already in use` upon daemon restarts.
   - Child process `stderr` is uncaptured. Script syntax errors or missing environment packages (e.g. Python module import errors) produce silent `EOF` failures with unhelpful `"stdio child produced no response"` messages. Furthermore, unread stderr exceeding 64KB fills the kernel pipe buffer, causing the child process to block indefinitely on `write(2, ...)` while the parent polls only for stdout.
3. **Serial Network Tool Execution Bottleneck**:
   - When the LLM emits multiple read-only stateless network tool calls in a single turn (e.g. concurrent `web_search` for different ticker symbols, followed by `fetch_webpage`), the runtime executes them strictly sequentially in a `for` loop, multiplying round-trip response time.
4. **Silent Workflow Step Failures & Unpooled Summary Workers**:
   - In `executor.c`, failures in deterministic computation steps (`step_out == NULL` or JSON parse failure) are silently ignored, continuing with empty context and leading downstream LLM steps to hallucinate.
   - In `summary.c`, background summarizer threads are spawned via unpooled `pthread_create`, and a static 16-slot pending array silently discards generated summaries when full.

This design document establishes the architectural specification for hardening the MCP I/O layer, introducing safe parallel tool execution for stateless network tools, propagating workflow step errors explicitly with frontend protocol alignment, and pooling asynchronous memory workers with clean lifecycle management.

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
│  Batch Classifier & Router   │                            │    Summary Task Queue Pool   │
│  (All-or-Nothing Fallback)   │                            │    (2 Workers, Idempotent)   │
└──────────────┬───────────────┘                            └──────────────┬───────────────┘
               │                                                           │
       ┌───────┴──────────────────────────────┐                    ┌───────┴───────┐
       │                                      │                    ▼               ▼
       ▼ (If 100% Stateless Web & <=8)        ▼ (If ANY DB/Mutation)┌──────────────┐┌──────────────┐
┌──────────────────────────────┐       ┌──────────────────────────┐ │ Worker 1     ││ Worker 2     │
│ Parallel Worker Group        │       │ Sequential Execution     │ │ (LLM Summary)││ (LLM Summary)│
│ (pthread_create per task)    │       │ (Existing safe loop)     │ └──────┬───────┘└──────┬───────┘
└──────────────┬───────────────┘       └──────────────┬───────────┘        │
               │                                      │                    ▼
               ▼                                      ▼             ┌──────────────────────────────┐
┌─────────────────────────────────────────────────────────────────┐ │  64-Slot Associative LRU     │
│ Ordered Gathering Barrier & SSE Emission                        │ │ (timestamp + free on replace)│
│ - Realloc-safe owned task copies (id, name, args)               │ └──────────────┬───────────────┘
│ - Sequential on_tool_result & clamp_tool_result insertion       │                │
└──────────────────────────────┬──────────────────────────────────┘                ▼
                               │                                      (Safe main-thread DB flush)
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│ MCP Bridge & Client Layer (src/services/ai/tools/mcp)           │
│ - mf_mcp_client_t Framer State in stdio_call                    │
│ - Dual-FD Polling (stdout + non-blocking stderr drain)          │
│ - Post-dup2 File Descriptor Sanitation: closefrom(3)            │
└─────────────────────────────────────────────────────────────────┘
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
- `mf_mcp_client_new()` initializes `c->err_fd = -1`, `c->framer.head = 0`, `c->framer.tail = 0`, and zeroes `c->last_error`.
- `mf_mcp_client_free()` closes `c->err_fd` if `>= 0` in addition to `c->read_fd` and `c->write_fd`.

#### Dual-FD Polling, Anti-Deadlock, and Framing in `stdio_call()`
All stdio reading in `mcp_client.c` (`initialize`, `list_tools`, `call_tool`) routes through static `stdio_call()`.
The function allocates output buffer `char* resp = malloc(MCP_MAX_FRAME_SIZE + MCP_IO_BUFFER_SIZE + 1)` and initializes `c->last_error[0] = '\0'`.

```c
/* Helper: Drain stderr non-blockingly; returns false on EOF / hangup */
static bool drain_stderr_nonblocking(mf_mcp_client_t* c) {
    if (!c || c->err_fd < 0) return false;
    char buf[512];
    ssize_t n;
    while ((n = read(c->err_fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        size_t cur_len = strlen(c->last_error);
        if (cur_len + n < sizeof(c->last_error) - 1) {
            strncat(c->last_error, buf, sizeof(c->last_error) - cur_len - 1);
        }
    }
    if (n == 0) {
        /* EOF reached on stderr: close descriptor to eliminate POLLHUP busy-loop */
        close(c->err_fd);
        c->err_fd = -1;
        return false;
    }
    return true;
}
```

1. **In-Memory Scan**:
   - If `c->framer.head < c->framer.tail`:
     - Search for `\n` using `memchr(c->framer.buf + c->framer.head, '\n', c->framer.tail - c->framer.head)`.
     - If found at pointer `p`:
       - Append bytes from `head` to `p` into `resp`.
       - Update `c->framer.head = (size_t)(p - c->framer.buf) + 1`.
       - Trim trailing `\r` and `\n` from `resp`, append `\0`, and return `resp`.
     - If not found:
       - Append all bytes from `head` to `tail` into `resp`.
       - Reset `c->framer.head = c->framer.tail = 0`.
2. **Dual-FD Polling & Buffer Refill**:
   - When `c->framer.head == c->framer.tail`:
     - Reset `c->framer.head = c->framer.tail = 0`.
     - Configure `struct pollfd pfds[2]`:
       - `pfds[0] = { .fd = c->read_fd, .events = POLLIN, .revents = 0 };`
       - `pfds[1] = { .fd = c->err_fd, .events = POLLIN, .revents = 0 };` (only if `c->err_fd >= 0`).
     - Call `int prc = poll(pfds, (c->err_fd >= 0) ? 2 : 1, timeout_ms)`.
     - If `prc < 0 && errno == EINTR`, retry poll.
     - If `prc == 0` (Timeout):
       - Drain stderr: `drain_stderr_nonblocking(c)`.
       - Write error to `out_err`: `snprintf(out_err, err_sz, "stdio call timed out after %d ms. %s", timeout_ms, c->last_error)`.
       - Reset `c->framer.head = c->framer.tail = 0`, free `resp`, return `NULL`.
     - If `c->err_fd >= 0 && (pfds[1].revents & (POLLIN | POLLHUP | POLLERR))`:
       - `drain_stderr_nonblocking(c)`.
     - If `pfds[0].revents & (POLLIN | POLLHUP | POLLERR)`:
       - `ssize_t r = read(c->read_fd, c->framer.buf, MCP_IO_BUFFER_SIZE)`.
       - If `r == 0` (EOF):
         - Drain stderr: `drain_stderr_nonblocking(c)`.
         - If `n > 0`: trim trailing `\r`/`\n`, null-terminate `resp[n] = '\0'`, return `resp`.
         - If `n == 0`:
           - If `c->last_error[0] != '\0'`, format `snprintf(out_err, err_sz, "%s", c->last_error)`.
           - Else, format `snprintf(out_err, err_sz, "stdio child produced no response")`.
           - Reset framer to 0, free `resp`, return `NULL`.
       - If `r < 0`: on `EAGAIN` retry poll; on fatal errors, reset framer to 0, free `resp`, return `NULL`.
       - Set `c->framer.tail = (size_t)r`.
3. **Hard Payload Cap**:
   - If accumulated length `n >= MCP_MAX_FRAME_SIZE`, reset `c->framer.head = c->framer.tail = 0`, free `resp`, write error `"stdio response frame exceeded 1MB limit"` into `out_err`, and return `NULL`.

---

### 3.2 Child Process Sanitation & Stderr Pipe (`src/services/ai/tools/mcp/mcp_client.c`)

#### Child Process Execution Sequence
Ensure `#define _GNU_SOURCE` precedes `<unistd.h>`.
1. In parent:
   - Create `in_pipe[2]`, `out_pipe[2]`, and `err_pipe[2]`.
   - On pipe creation failure, cleanly close all created descriptors and return `NULL`.
   - Set `err_pipe[0]` to non-blocking (`fcntl(err_pipe[0], F_SETFL, O_NONBLOCK)`).
   - `pid_t pid = fork();`
   - If `pid < 0` (fork failure): close all six pipe descriptors (`in_pipe`, `out_pipe`, `err_pipe`), free resources, write `out_err`, return `NULL`.
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

---

### 3.3 Parallel Stateless Network Tool Dispatch (`src/services/ai/runtime/loop.c`)

#### Concurrency Boundary: Strict Protection for SQLite Single Connection
Minefolio's `csilk_db_pool_t` with SQLite driver maintains a single underlying `sqlite3*` handle (`src/common/db.c`). Concurrent queries on this handle trigger `SQLITE_MISUSE` or crashes. Furthermore, `mcp_bridge_dispatch` queries SQLite on every call (`mf_mcp_server_repo_find_by_id`, `mf_mcp_server_tool_repo_load`).

Therefore, parallel dispatch applies **only to all-or-nothing batches of stateless network tools requiring zero database access**:
- **Stateless Network Tool Criteria**:
  - `strcmp(name, "web_search") == 0 || strcmp(name, "fetch_webpage") == 0`
- **Batch Parallelism Rule**:
  - If **ALL** tools in the turn are stateless network tools AND `n_calls <= 8`, execute via Parallel Worker Group.
  - If **ANY** tool in the turn touches local database (`get_assets`, `get_transactions`, etc.), is an MCP tool (`mcp:*`), or is a mutation (`is_mutation == true` / `requires_id == true`), the **entire batch falls back to standard sequential execution**.

#### Parallel Dispatch Data Structures & Worker
```c
typedef struct {
    size_t               call_idx;
    int64_t              user_id;
    int64_t              session_id;
    char                 tool_call_id[64];
    char*                tool_name;       /* strdup'd owned string */
    char*                raw_arguments;   /* strdup'd owned string */
    csilk_json_t*        args;
    const volatile bool* cancel_token;
    char*                result_str;
    long                 span_ms;
    bool                 executed;
    bool                 success;
} ai_parallel_task_t;

static void* ai_parallel_tool_worker(void* arg) {
    ai_parallel_task_t* task = (ai_parallel_task_t*)arg;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    
    /* Stateless network tools pass NULL for pool, valid user_id/session_id to pass auth */
    task->result_str = ai_tools_execute_parsed_cancel(
        NULL, task->user_id, task->session_id, task->args, task->tool_name, task->cancel_token);
        
    clock_gettime(CLOCK_MONOTONIC, &t1);
    task->span_ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    task->executed = true;
    task->success = (task->result_str != NULL);
    if (!task->result_str) {
        task->result_str = strdup("{\"error\":\"tool execution failed\"}");
    }
    return NULL;
}
```

#### Dispatch and Gather Flow in `loop.c`
When `n_calls > 1` and all `t = 0 .. n_calls - 1` are stateless web tools with `n_calls <= 8`:
1. **Main-Thread Pre-Evaluation (Serialized & In Exact Turn Order)**:
   - For each `t = 0 .. n_calls - 1`:
     - Initialize `tasks[t]` with `user_id = ctx->user_id`, `session_id = ctx->session_id`, `cancel_token = ctx->cancel_token`.
     - Copy owned strings:
       ```c
       strncpy(tasks[t].tool_call_id, tc[t].id ? tc[t].id : "", sizeof(tasks[t].tool_call_id) - 1);
       tasks[t].tool_name = strdup(tc[t].name ? tc[t].name : "");
       tasks[t].raw_arguments = strdup(tc[t].arguments ? tc[t].arguments : "{}");
       tasks[t].args = csilk_json_parse(tasks[t].raw_arguments);
       ```
     - Run security policy:
       ```c
       ai_policy_decision_t* decision = ai_policy_evaluate(
           ctx->user_id, ctx->session_id, tasks[t].tool_name, tasks[t].args, AI_RISK_NO_OVERRIDE);
       ```
     - Emit `cbs->on_tool_call()` before any execution starts.
     - Evaluate duplicate hash `uint64_t call_hash = loop_call_hash(tasks[t].tool_name, tasks[t].raw_arguments);`.
     - If blocked by policy: populate `tasks[t].result_str` with policy denial JSON, `tasks[t].executed = false`.
     - Else if duplicate: populate `tasks[t].result_str` with duplicate call notice, `tasks[t].executed = false`.
     - Else: if `seen_count < LOOP_DUP_CALL_SLOTS`, record `seen_hashes[seen_count++] = call_hash;`.
     - Free policy decision: `ai_policy_decision_free(decision);`.
2. **Parallel Execution**:
   - Track spawned threads: `bool thread_spawned[8] = {false}; pthread_t threads[8];`.
   - For each unblocked task `t`:
     - If `pthread_create(&threads[t], NULL, ai_parallel_tool_worker, &tasks[t]) == 0`:
       - `thread_spawned[t] = true;`
     - Else (spawn failure):
       - Fallback inline immediately: `ai_parallel_tool_worker(&tasks[t]);`.
   - Join all spawned workers:
     - For each `t = 0 .. n_calls - 1`:
       - If `thread_spawned[t]`: `pthread_join(threads[t], NULL);`.
3. **Main-Thread Post-Gather (Serialized & In Exact Turn Order)**:
   - For each `t = 0 .. n_calls - 1`:
     - If `tasks[t].executed`:
       - `ai_runtime_limits_record_tool_call(&ctx->stats, 1);`
     - `ai_trace_add_tool_span(trace, tasks[t].tool_name, tasks[t].span_ms, strlen(tasks[t].result_str), tasks[t].success);`
     - Emit `cbs->on_tool_result(tasks[t].tool_call_id, tasks[t].tool_name, tasks[t].result_str, user_data);`
     - Assemble `tc_record` for persistence using owned `tool_call_id`, `tool_name`, `raw_arguments`.
     - Append `role="tool"` message into `msgs`:
       ```c
       mc++;
       msgs = (csilk_ai_message_t*)realloc(msgs, sizeof(csilk_ai_message_t) * mc);
       msgs[mc - 1] = (csilk_ai_message_t){
           .role = "tool",
           .content = loop_clamp_tool_result(tasks[t].result_str),
           .tool_call_id = strdup(tasks[t].tool_call_id),
           .tool_calls = NULL,
           .tool_call_count = 0,
       };
       ```
     - Free task heap memory:
       ```c
       csilk_json_free(tasks[t].args);
       free(tasks[t].tool_name);
       free(tasks[t].raw_arguments);
       free(tasks[t].result_str);
       ```

---

### 3.4 Deterministic Workflow Error Propagation (`src/services/ai/workflow/executor.c`)

#### Comprehensive Failure Detection & Resource Cleanup
In `src/services/ai/workflow/executor.c`, for deterministic step `i`:
```c
char* cur_ctx_str = csilk_json_serialize(ctx_obj, &slen);
char* step_out = node->execute(pool, user_id, params, cur_ctx_str);
free(cur_ctx_str);

csilk_json_t* parsed_out = step_out ? csilk_json_parse(step_out) : NULL;
if (!step_out || !parsed_out) {
    if (step_out) {
        free(step_out);
    }
    
    /* Emit standard error event compatible with frontend stores/chat.ts */
    csilk_json_t* ev_err = csilk_json_object();
    csilk_json_add_string(ev_err, "type", "error");
    csilk_json_add_number(ev_err, "step_index", (double)i);
    csilk_json_add_string(ev_err, "step_id", node->node_id);
    /* Both "message" and "error" populated for full frontend compatibility */
    const char* emsg = !step_out ? "Node execution failed" : "Malformed JSON returned by node";
    csilk_json_add_string(ev_err, "message", emsg);
    csilk_json_add_string(ev_err, "error", emsg);
    
    char* err_str = csilk_json_serialize(ev_err, &slen);
    csilk_sse_send(c, "error", err_str ? err_str : "{}");
    
    free(err_str);
    csilk_json_free(ev_err);
    csilk_json_free(ctx_obj);
    return -1;
}
```

#### Frontend Contract Alignment
- In `frontend/src/stores/chat.ts` (lines 845–859) and `frontend/src/api/ai.ts` (lines 561–575), `runWorkflowStream` extracts `chunk.message`.
- Populating `message` in the `error` event payload ensures `activeMsg.error` receives the exact error string.
- Setting `wfState.status = 'error'` on receiving `type: 'error'` halts execution before the terminal LLM report generator, saving prompt tokens and eliminating hallucinated reports.

---

### 3.5 Asynchronous Summary Worker Pool (`src/services/ai/memory/summary.c`)

#### Data Structure & Header Export
In `src/services/ai/memory/summary.h`:
```c
void ai_summary_pool_init(void);
void ai_summary_pool_shutdown(void);
```

In `src/services/ai/memory/summary.c`:
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
    pthread_cond_t  not_empty; /* Non-blocking enqueue on full; no not_full needed */
    bool            shutdown;
    bool            initialized;
    pthread_t       workers[2];
} summary_worker_pool_t;

static summary_worker_pool_t s_pool = {0};
```

#### Worker Loop Implementation
```c
static void* summary_pool_worker_entry(void* arg) {
    (void)arg;
    while (1) {
        pthread_mutex_lock(&s_pool.lock);
        while (!s_pool.shutdown && s_pool.count == 0) {
            pthread_cond_wait(&s_pool.not_empty, &s_pool.lock);
        }
        if (s_pool.shutdown) {
            /* Exit immediately on shutdown to prevent multi-minute HTTP hangs */
            pthread_mutex_unlock(&s_pool.lock);
            break;
        }
        summary_task_t* task = s_pool.queue[s_pool.head];
        s_pool.head = (s_pool.head + 1) % SUMMARY_QUEUE_CAP;
        s_pool.count--;
        pthread_mutex_unlock(&s_pool.lock);

        summary_worker(task);
    }
    return NULL;
}
```

#### Pool Lifecycle Management
- `void ai_summary_pool_init(void)`:
  - Called at the **very entry** of `ai_init()` in `src/services/ai_service.c` before any configuration loading or early returns.
  - Idempotent: checks `if (s_pool.initialized) return;`.
  - Resets: `s_pool.shutdown = false; s_pool.head = 0; s_pool.tail = 0; s_pool.count = 0;`.
  - Initializes mutex, condition variable, spawns 2 worker threads running `summary_pool_worker_entry`, and sets `s_pool.initialized = true`.
- `void ai_summary_pool_shutdown(void)`:
  - Called during `ai_shutdown()` in `src/services/ai_service.c`.
  - Idempotent: checks `if (!s_pool.initialized) return;`.
  - Acquires `s_pool.lock`, sets `s_pool.shutdown = true`, broadcasts `not_empty`, releases lock.
  - Joins both worker threads: `pthread_join(s_pool.workers[0], NULL); pthread_join(s_pool.workers[1], NULL);`.
  - Drains queue: for any remaining queued tasks, calls `inflight_release(task->inflight_idx)`, frees `task->user_prompt`, and frees `task`.
  - Drains pending slots: iterates `s_pending`, frees all `s_pending[i].text != NULL` to ensure zero AddressSanitizer leaks.
  - Destroys synchronizers and sets `s_pool.initialized = false`.

#### Non-blocking Enqueue & Associative Deduplicating Pending Slots
1. **Non-blocking Enqueue**:
   - `ai_summary_maybe_trigger()` first guards: `if (!s_pool.initialized || s_pool.shutdown) return;`.
   - Acquires `s_pool.lock`.
   - If `s_pool.count == SUMMARY_QUEUE_CAP`, drop task immediately: free `task->user_prompt`, free `task`, call `inflight_release(task->inflight_idx)`, unlock, and log warning. Never block main HTTP request threads.
   - Enqueue task: `s_pool.queue[s_pool.tail] = task; s_pool.tail = (s_pool.tail + 1) % SUMMARY_QUEUE_CAP; s_pool.count++;`.
   - Signals `pthread_cond_signal(&s_pool.not_empty)` and unlocks.
2. **Pending Slot Replacement with Session Deduplication**:
   - Worker acquires `s_pending_lock`.
   - **Step 1: Check existing session**: If an entry exists where `s_pending[i].text != NULL && s_pending[i].session_id == session_id`, free `s_pending[i].text`, update slot with new summary, and set `created_at = time(NULL)`.
   - **Step 2: Check empty slot**: If no existing session found, search for slot where `s_pending[i].text == NULL`.
   - **Step 3: LRU Eviction**: If all 64 slots are full with distinct sessions, find slot with the oldest `created_at`, free previous string, and overwrite.
3. **Thread-Safe Database Flush**:
   - Main request thread in `ai_summary_flush_pending()` acquires `s_pending_lock`, extracts text for `session_id`, clears slot, releases lock, and performs insertion on the main thread.

---

## 4. Error Handling & Security Matrix

| Failure Condition | Detection Mechanism | Mitigation / Recovery Action | User-Facing Outcome |
| :--- | :--- | :--- | :--- |
| MCP child script crashes (e.g. missing import) | `read(stdout)` returns EOF; `err_fd` has bytes | Read up to 1024B from `err_fd`, populate `c->last_error` | Explicit error: `"stdio child stderr: ImportError: No module named 'mcp'"` |
| Child emits >64KB stderr before writing stdout | Stderr pipe buffer fills; `pfds[1]` reports `POLLIN` | Parent drains stderr non-blockingly during poll loop | Anti-deadlock: child continues execution; parent captures stderr |
| Child closes stderr descriptor early | Read end receives `POLLHUP` / EOF | Parent closes `err_fd`, sets to -1; poll continues on stdout only | Anti-busy-loop: poll remains blocked; 0% CPU consumption |
| Malicious / runaway MCP payload (>1MB) | Byte counter in framer exceeds `MCP_MAX_FRAME_SIZE` | Abort read, close pipes, reset framer | Tool returns error: `"payload exceeded 1MB limit"` |
| Client disconnects during parallel tool execution | `*ctx->cancel_token == true` | Workers observe cancel token, skip operations, join | In-flight execution cleanly halted; no orphaned threads |
| Child inherits listening socket / DB pool | Child executes `closefrom(3)` after `dup2(0,1,2)` | All inherited file descriptors > 2 closed | Zero socket leaks or daemon restart lockups |
| Deterministic calculation SQL failure | `node->execute` returns NULL or invalid JSON | Free `cur_ctx_str`, emit SSE `type: "error"`, abort | Client UI transitions to error status; 0 token waste |
| Summary queue flooded by burst chat turns | Task queue reaches capacity (64) | Free `task->user_prompt`, release inflight slot | Safe backpressure; no memory leak or thread exhaustion |

---

## 5. Verification & Testing Strategy

### 5.1 Unit Tests & CMake Targets
The following new unit test targets are registered under `CMakeLists.txt` via `add_executable()` and `add_test()`:
1. **`tests/unit/test_mcp_client_io.c`**:
   - White-box unit test including `services/ai/tools/mcp/mcp_client.c`.
   - Feed multi-kilobyte JSON-RPC responses (10KB, 100KB, 500KB) through a pipe to `stdio_call()`.
   - Assert `mcp_io_reader_t` correctly extracts single and multiple frames across buffer boundaries without dropping bytes.
   - Assert that payloads exceeding 1MB cleanly fail with expected error code.
2. **`tests/unit/test_mcp_child_sanitation.c`**:
   - Spawn mock child script that inspects `/proc/self/fd/`.
   - Assert that only descriptors 0, 1, 2 exist in child process.
3. **`tests/unit/test_mcp_stderr_capture.c`**:
   - Spawn script writing >64KB to stderr to verify non-blocking drain immunity against pipe deadlock.
   - Assert `c->last_error` captures the stderr text and execution succeeds or fails cleanly.
4. **`tests/unit/test_ai_parallel_tools.c`**:
   - Dispatch 2 mock `web_search` queries with 200ms sleep each.
   - Assert total wall-clock duration is < 300ms (parallel), compared to 400ms (serial).
   - Dispatch 1 `web_search` and 1 local `get_assets` query; assert fallback to sequential execution to protect SQLite single connection.
5. **`tests/unit/test_workflow_step_error.c`**:
   - Register workflow with a failing intermediate node.
   - Assert SSE event `type: "error"` is emitted with `message` and `error` fields, `cur_ctx_str` is freed, and terminal LLM report generator is not invoked.
6. **`tests/unit/test_summary_worker_pool.c`**:
   - Enqueue 20 concurrent summary tasks.
   - Assert tasks are processed by the 2 persistent worker threads without spawning additional pthreads.
   - Verify idempotent multi-cycle `ai_summary_pool_init()` -> `ai_summary_pool_shutdown()` -> `ai_summary_pool_init()`.
   - Assert clean shutdown without thread leaks, dangling in-flight locks, or AddressSanitizer errors.

### 5.2 Regression Tests
- Execute full existing regression test suite:
  - `tests/test_ai_tool_call.sh`
  - `tests/test_ai_loop_hardening.sh`
  - `tests/test_mcp.sh`
