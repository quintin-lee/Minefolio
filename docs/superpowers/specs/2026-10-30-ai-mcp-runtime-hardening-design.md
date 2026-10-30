# AI Agent & MCP Runtime Hardening Design

## 1. Context & Motivation

Minefolio's AI subsystem provides a comprehensive suite of financial ReAct Agent capabilities, custom Model Context Protocol (MCP) tool integration, and deterministic financial diagnosis workflows.

During recent in-depth architectural and code-level audits, three structural bottlenecks and stability risks were identified:
1. **MCP STDIO I/O Syscall Storm**: `mf_mcp_client_call_tool` reads child process stdout byte-by-byte via `read(fd, cur, 1)` with a per-byte `poll()`. For large tool payloads (e.g. 100 KB JSON schemas), this causes over 100,000 user/kernel context switches, leading to CPU thrashing and elevated latency.
2. **Child Process Resource & Debugging Leak**:
   - `fork()` without child-side file descriptor sanitation leaks host TCP listening sockets and open database pool connections to child processes, risking `Address already in use` upon daemon restarts.
   - Child process `stderr` is uncaptured. Script syntax errors or missing environment packages (e.g. Python module import errors) produce silent `EOF` failures with unhelpful `"stdio child produced no response"` messages.
3. **Serial Tool Execution Bottleneck**:
   - When the LLM emits multiple read-only tool calls in a single turn (e.g. concurrent `web_search` and local `get_assets`), the runtime executes them strictly sequentially in a `for` loop, multiplying round-trip response time.
4. **Silent Workflow Step Failures & Unpooled Summary Workers**:
   - In `executor.c`, failures in deterministic computation steps (`step_out == NULL` or JSON parse failure) are silently ignored, continuing with empty context and leading downstream LLM steps to hallucinate.
   - In `summary.c`, background summarizer threads are spawned via unpooled `pthread_create`, and a static 16-slot pending array silently discards generated summaries when full.

This design document establishes the architectural specification for hardening the MCP I/O layer, introducing safe parallel tool execution for read-only tools, propagating workflow step errors explicitly, and pooling asynchronous memory workers.

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
│  (Read-only vs Mutation)     │                            │     (2-Worker Thread Pool)   │
└──────────────┬───────────────┘                            └──────────────┬───────────────┘
               │                                                           │
       ┌───────┴───────┐                                           ┌───────┴───────┐
       ▼               ▼                                           ▼               ▼
┌──────────────┐┌──────────────┐                            ┌──────────────┐┌──────────────┐
│ Read-only    ││ Mutation     │                            │ Worker 1     ││ Worker 2     │
│ Worker Group ││ Sequential   │                            │ (LLM Summary)││ (LLM Summary)│
└──────┬───────┘└──────┬───────┘                            └──────┬───────┘└──────┬───────┘
       │               │                                           │               │
       │        ┌──────┴────────────────────────┐                  ▼               ▼
       │        │                               │           ┌──────────────────────────────┐
       ▼        ▼                               ▼           │ 64-Slot Circular LRU Pending │
┌──────────────────────────────┐ ┌────────────────────────┐ └──────────────┬───────────────┘
│  Local Tool Registry         │ │  MCP Bridge & Client   │                │
│  (Asset, Trans, WebSearch)   │ │  (src/services/ai/mcp) │                ▼
└──────────────────────────────┘ └──────────────┬─────────┘   (Safe main-thread DB flush)
                                                │
                                 ┌──────────────┴──────────────┐
                                 ▼                             ▼
                  ┌──────────────────────────────┐ ┌──────────────────────────────┐
                  │ 4KB Block-Buffered IO Framer │ │ Child Process Sandbox        │
                  │ (memchr newline fast scan)   │ │ (closefrom & stderr pipe)    │
                  └──────────────────────────────┘ └──────────────────────────────┘
```

---

## 3. Detailed Specifications

### 3.1 MCP Block-Buffered I/O Framer (`src/services/ai/tools/mcp/mcp_client.c`)

#### Data Structure
```c
#define MCP_IO_BUFFER_SIZE 4096
#define MCP_MAX_FRAME_SIZE (1 << 20) /* 1 MB */

typedef struct {
    char   buf[MCP_IO_BUFFER_SIZE];
    size_t head;
    size_t tail;
} mcp_io_reader_t;
```

#### Protocol & Framing Rules
1. Instead of reading 1 byte per system call, `mcp_client.c` reads chunks of up to 4096 bytes into `mcp_io_reader_t`.
2. When searching for frame termination (`\n`), `memchr(reader->buf + reader->head, '\n', reader->tail - reader->head)` is evaluated in memory.
3. If `\n` is detected, the line is sliced, appended to the accumulated dynamic string, trailing `\r` and `\n` stripped, and returned to the caller.
4. If buffer contains no `\n`, entire available contents are moved to output, and another `poll()` + `read()` chunk is fetched.
5. If total consumed frame length exceeds `MCP_MAX_FRAME_SIZE` without a newline delimiter, the connection is aborted with error `"stdio response frame exceeded 1MB limit"`.

### 3.2 Child Process Sanitation & Stderr Pipe (`src/services/ai/tools/mcp/mcp_client.c`)

#### File Descriptor Sanitation
In the child process path after `fork()` and before `execve()`:
```c
static void mcp_sanitize_child_fds(int keep_in, int keep_out, int keep_err)
{
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    closefrom(3);
#else
    int max_fd = (int)sysconf(_SC_OPEN_MAX);
    if (max_fd < 0 || max_fd > 4096) max_fd = 4096;
    for (int fd = 3; fd < max_fd; fd++) {
        if (fd != keep_in && fd != keep_out && fd != keep_err) {
            close(fd);
        }
    }
#endif
}
```

#### Stderr Capture Pipe
1. The parent creates an unidirectional pipe `err_pipe[2]`.
2. Child redirects `dup2(err_pipe[1], STDERR_FILENO)`.
3. Parent sets `err_pipe[0]` to `O_NONBLOCK`.
4. If child process produces EOF before valid JSON-RPC frame completion, parent reads up to 1024 bytes from `err_pipe[0]` into `c->last_error` and surfaces it in `out_err`.

### 3.3 Parallel Read-Only Tool Dispatch (`src/services/ai/runtime/loop.c`)

#### Classification Rules
- **Read-Only Group**: `tool->permission == AI_PERM_READ && !tool->is_mutation && !ai_tool_requires_id(name)`.
- **Mutation / State-Altering Group**: `tool->is_mutation || tool->permission != AI_PERM_READ || ai_tool_requires_id(name)`.

#### Execution Model
1. If all tools in the turn are in the Read-Only Group:
   - Allocate `pthread_t` workers for each tool call up to `min(n_calls, 8)`.
   - Each worker receives an isolated `ai_tool_context_t` copy and executes `ai_tools_execute_parsed_cancel()`.
   - Main thread waits for completion using `pthread_join()`.
   - SSE events (`on_tool_result`) and `ai_trace_add_tool_span` are protected by a mutex to guarantee ordered, serialized stream frames.
2. If any tool is in the Mutation Group:
   - Execute strictly sequentially in order of LLM tool call index to preserve transaction order and double-spend protection.

### 3.4 Deterministic Workflow Error Propagation (`src/services/ai/workflow/executor.c`)

#### Node Execution Validation
When executing intermediate deterministic step `i`:
```c
char* step_out = node->execute(pool, user_id, params, cur_ctx_str);
if (!step_out) {
    /* Step execution failed */
    csilk_json_t* ev_err = csilk_json_object();
    csilk_json_add_number(ev_err, "step_index", (double)i);
    csilk_json_add_string(ev_err, "step_id", node->node_id);
    csilk_json_add_string(ev_err, "error", "Deterministic node returned null result");
    csilk_json_add_bool(ev_err, "fatal", true);
    
    char* err_str = csilk_json_serialize(ev_err, &slen);
    csilk_sse_send(c, "step_error", err_str ? err_str : "{}");
    csilk_sse_send(c, "workflow_error", "{\"error\":\"deterministic calculation failed\"}");
    
    free(err_str);
    csilk_json_free(ev_err);
    csilk_json_free(ctx_obj);
    return -1;
}
```
1. Emit `step_error` SSE event with explicit node ID and error detail.
2. Abort workflow execution immediately without calling the terminal LLM, saving prompt tokens and avoiding hallucinations based on missing financial metrics.

### 3.5 Asynchronous Summary Worker Pool (`src/services/ai/memory/summary.c`)

#### Pool Specifications
- Dedicated worker pool with `NUM_WORKERS = 2` background pthreads.
- Thread-safe FIFO task queue with bounded capacity of 64 entries.
- Circular pending queue `s_pending` expanded from 16 to 64 slots.
- Slot eviction uses LRU timestamp replacement instead of dropping incoming summaries.
- Database writing is strictly serialized on the main request thread in `ai_summary_flush_pending()` to maintain SQLite/PostgreSQL single-connection and connection pool thread safety.

---

## 4. Error Handling & Security Matrix

| Failure Condition | Detection Mechanism | Mitigation / Recovery Action | User-Facing Outcome |
| :--- | :--- | :--- | :--- |
| MCP child script crashes (e.g. missing import) | `read(stdout)` returns EOF; `err_pipe` has bytes | Read up to 1KB from `err_pipe[0]`, populate `out_err` | Clear error: `"child process crashed: ImportError..."` |
| Malicious / runaway MCP payload (>1MB) | Byte counter in framer exceeds `MCP_MAX_FRAME_SIZE` | Abort read, close pipes, mark client error | Tool returns error: `"payload exceeded 1MB limit"` |
| Client disconnects during parallel tool execution | `*ctx->cancel_token == true` | Workers observe cancel token, skip long operations, join | In-flight execution cleanly halted; no orphaned threads |
| Parent holds open database / web server port | Child executes `closefrom(3)` before `execve` | All inherited file descriptors > 2 closed | Zero socket leaks or daemon restart lockups |
| Deterministic calculation SQL failure | `node->execute` returns NULL | Workflow emits `step_error` and `workflow_error` SSE | Client UI displays explicit step failure; 0 token waste |

---

## 5. Verification & Testing Strategy

### 5.1 Unit Tests
1. **`test_mcp_client_io.c`**:
   - Feed multi-kilobyte JSON-RPC responses (10KB, 100KB, 500KB) through a pipe.
   - Assert syscall count is reduced by >= 99% compared to byte-by-byte baseline.
   - Assert correct message extraction and trailing delimiter stripping.
2. **`test_mcp_child_sanitation.c`**:
   - Spawn mock child script that inspects `/proc/self/fd/`.
   - Assert that only descriptors 0, 1, 2 (and test pipe) exist in the child.
3. **`test_mcp_stderr_capture.c`**:
   - Spawn script writing to stderr and exiting with code 1.
   - Assert error string contains exact text written to stderr.
4. **`test_ai_parallel_tools.c`**:
   - Dispatch 3 mock read-only tools with 200ms latency each.
   - Assert total elapsed duration is < 350ms (parallel), compared to 600ms (serial).
5. **`test_workflow_step_error.c`**:
   - Register workflow with a failing intermediate node.
   - Assert `step_error` and `workflow_error` SSE frames are emitted, and terminal LLM function is not invoked.

### 5.2 Regression Tests
- Execute full existing regression test scripts:
  - `tests/test_ai_tool_call.sh`
  - `tests/test_ai_loop_hardening.sh`
  - `tests/test_mcp.sh`
