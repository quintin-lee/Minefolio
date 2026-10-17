# AI 运行时 SSE 流式传输内存池与零拷贝优化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 彻底消除 AI 长连接流式传输（SSE）中的高频堆内存分配与语法树遍历开销，消除 `ai_trace` 追加输出的 $O(N^2)$ 复杂度瓶颈，基于 4KB 栈缓冲与 `csilk_arena` 实现高并发下的极速零拷贝流式转发。

**Architecture:** 
1. **高性能转义引擎** (`fast_json.h/.c`)：提供 RFC 8259 规范的零堆就地字符串转义，针对控制字符、引号与反斜杠快速编码，高位 UTF-8 零开销原样穿透。
2. **平摊 $O(1)$ Trace 缓冲** (`ai_trace.h/.c`)：在 `ai_trace_t` 中追踪 `output_len` 与 `output_cap`，通过几何级数倍增扩容彻底消除 `strlen` 全文扫描与逐字重分配。
3. **双级缓冲区流式网桥** (`ai_service.c`)：`sse_bridge_text_chunk` 优先使用 4KB 栈上直接装配 JSON Payload；超大 Chunk 自动回退至会话级 `csilk_arena` 内存池，流式结束批量销毁。
4. **工作流模拟分块零堆化** (`workflow/executor.c`)：重构大文本步骤的打字机流式推送，移除 AST 构造与中间堆分配。

**Tech Stack:** C23, GCC 16 / Linux, csilk v0.5.2 (`csilk_arena`, `csilk_sse`), CTest, CMake.

---

### File Structure Map

| 目标文件 | 责任与边界 |
|---|---|
| `backend/src/services/ai/runtime/fast_json.h` | 声明高性能快速 JSON 转义与栈格式化接口 |
| `backend/src/services/ai/runtime/fast_json.c` | 实现 RFC 8259 快速字符串就地转义算法 |
| `backend/src/common/ai_trace.h` | 扩充 `ai_trace_t`，新增 `output_len` 和 `output_cap` 字段 |
| `backend/src/common/ai_trace.c` | 重写 `ai_trace_append_output` 实现平摊 $O(1)$ 几何扩容 |
| `backend/src/services/ai_service.c` | 重构 `sse_bridge_t` 与 `sse_bridge_text_chunk` 双级缓冲装配，注入 `csilk_arena` 生命周期 |
| `backend/src/services/ai/workflow/executor.c` | 重构工作流打字机分块推送，复用快速转义与栈缓冲 |
| `backend/tests/unit/test_ai_runtime.c` | 编写/补充针对快速转义、Trace 平摊扩容及 Arena 边界的单元测试用例 |

---

### Task 1: 高性能快速 JSON 转义核心模块实现与单元测试 (TDD)

**Files:**
- Create: `backend/src/services/ai/runtime/fast_json.h`
- Create: `backend/src/services/ai/runtime/fast_json.c`
- Test: `backend/tests/unit/test_ai_runtime.c`

- [x] **Step 1: 在 `backend/tests/unit/test_ai_runtime.c` 编写失败的转义单元测试**

```c
/* 在 test_ai_runtime.c 顶部引入头文件声明（待创建） */
#include "services/ai/runtime/fast_json.h"

static void
test_ai_json_escape_fast_comprehensive(void)
{
    printf("--- Running test_ai_json_escape_fast_comprehensive ---\n");

    /* 1. 常规无特殊字符文本 */
    {
        char out[128];
        int n = ai_json_escape_fast("hello world", 11, out, sizeof(out));
        assert(n == 11);
        assert(strcmp(out, "hello world") == 0);
    }

    /* 2. 引号与反斜杠 */
    {
        char out[128];
        const char* raw = "quote \" and slash \\ test";
        int n = ai_json_escape_fast(raw, strlen(raw), out, sizeof(out));
        assert(n > 0);
        assert(strcmp(out, "quote \\\" and slash \\\\ test") == 0);
    }

    /* 3. 控制字符 \n, \r, \t, 以及 \x01 */
    {
        char out[128];
        const char* raw = "line1\nline2\t\"tab\"\x01";
        int n = ai_json_escape_fast(raw, strlen(raw), out, sizeof(out));
        assert(n > 0);
        assert(strcmp(out, "line1\\nline2\\t\\\"tab\\\"\\u0001") == 0);
    }

    /* 4. UTF-8 中文与 Emoji 原样保留 */
    {
        char out[128];
        const char* raw = "投资组合 📈 $1000 🪙";
        int n = ai_json_escape_fast(raw, strlen(raw), out, sizeof(out));
        assert(n == (int)strlen(raw));
        assert(strcmp(out, raw) == 0);
    }

    /* 5. 缓冲区溢出保护 */
    {
        char out[8];
        const char* raw = "abcdefghijk";
        int n = ai_json_escape_fast(raw, strlen(raw), out, sizeof(out));
        assert(n == -1);
    }
}
```

并在 `main()` 中注册调用 `test_ai_json_escape_fast_comprehensive()`。

- [x] **Step 2: 运行测试验证编译/执行失败**

```bash
cmake --build backend/build --target test_ai_runtime
```
Expected: FAIL，提示找不到 `fast_json.h` 或未定义标识符。

- [x] **Step 3: 编写 `fast_json.h` 与 `fast_json.c` 最小实现**

在 `backend/src/services/ai/runtime/fast_json.h` 中写入：
```c
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将原始文本按照 RFC 8259 规范就地转义并输出到目标缓冲区
 *
 * @param src 原始字符串起始指针
 * @param src_len 原始字符串长度
 * @param dst 目标缓冲区
 * @param dst_cap 目标缓冲区最大容量 (必须保留至少 1 字节存储 '\0')
 * @return int 成功写入目标缓冲区的字符数 (不含末尾 '\0')；若缓冲区不足则返回 -1
 */
int ai_json_escape_fast(const char* src, size_t src_len, char* dst, size_t dst_cap);

#ifdef __cplusplus
}
#endif
```

在 `backend/src/services/ai/runtime/fast_json.c` 中写入：
```c
#include "services/ai/runtime/fast_json.h"
#include <stdint.h>
#include <stdio.h>

int
ai_json_escape_fast(const char* src, size_t src_len, char* dst, size_t dst_cap)
{
    if (!src || !dst || dst_cap == 0) {
        return -1;
    }

    static const char hex_digits[] = "0123456789abcdef";
    size_t d = 0;

    for (size_t s = 0; s < src_len; s++) {
        unsigned char c = (unsigned char)src[s];

        if (c == '"') {
            if (d + 2 >= dst_cap) return -1;
            dst[d++] = '\\';
            dst[d++] = '"';
        } else if (c == '\\') {
            if (d + 2 >= dst_cap) return -1;
            dst[d++] = '\\';
            dst[d++] = '\\';
        } else if (c == '\b') {
            if (d + 2 >= dst_cap) return -1;
            dst[d++] = '\\';
            dst[d++] = 'b';
        } else if (c == '\f') {
            if (d + 2 >= dst_cap) return -1;
            dst[d++] = '\\';
            dst[d++] = 'f';
        } else if (c == '\n') {
            if (d + 2 >= dst_cap) return -1;
            dst[d++] = '\\';
            dst[d++] = 'n';
        } else if (c == '\r') {
            if (d + 2 >= dst_cap) return -1;
            dst[d++] = '\\';
            dst[d++] = 'r';
        } else if (c == '\t') {
            if (d + 2 >= dst_cap) return -1;
            dst[d++] = '\\';
            dst[d++] = 't';
        } else if (c < 0x20) {
            /* 控制字符转义为 \u00XX */
            if (d + 6 >= dst_cap) return -1;
            dst[d++] = '\\';
            dst[d++] = 'u';
            dst[d++] = '0';
            dst[d++] = '0';
            dst[d++] = hex_digits[(c >> 4) & 0x0F];
            dst[d++] = hex_digits[c & 0x0F];
        } else {
            /* 普通 ASCII 字符及 UTF-8 高位直接透明拷贝 */
            if (d + 1 >= dst_cap) return -1;
            dst[d++] = (char)c;
        }
    }

    dst[d] = '\0';
    return (int)d;
}
```

- [x] **Step 4: 编译并运行测试验证通过**

```bash
cmake --build backend/build --target test_ai_runtime && ./backend/build/test_ai_runtime
```
Expected: PASS，控制台输出 `--- Running test_ai_json_escape_fast_comprehensive ---` 且所有断言通过。

- [x] **Step 5: 提交代码**

```bash
git add backend/src/services/ai/runtime/fast_json.h backend/src/services/ai/runtime/fast_json.c backend/tests/unit/test_ai_runtime.c
git commit -m "feat(ai): ✨ add fast zero-heap JSON string escaper with unit tests"
```

---

### Task 2: `ai_trace_t` 平摊扩容重构与高频追加基准测试 (TDD)

**Files:**
- Modify: `backend/src/common/ai_trace.h:40-60`
- Modify: `backend/src/common/ai_trace.c:40-70,145-170`
- Test: `backend/tests/unit/test_ai_runtime.c`

- [x] **Step 1: 在 `backend/tests/unit/test_ai_runtime.c` 编写扩容测试**

```c
static void
test_ai_trace_amortized_growth(void)
{
    printf("--- Running test_ai_trace_amortized_growth ---\n");

    ai_trace_t t;
    ai_trace_init(&t, 1, 100, "openai", "gpt-4o");

    assert(t.output_content != NULL);
    assert(t.output_len == 0);
    assert(t.output_cap >= 1024);

    /* 模拟追加 10,000 次短 Token */
    const char* token = "abc";
    for (int i = 0; i < 10000; i++) {
        ai_trace_append_output(&t, token);
    }

    assert(t.output_len == 30000);
    assert(t.output_cap >= 30001);
    assert(t.accumulated_len == 30000);
    assert(strlen(t.output_content) == 30000);

    /* 验证前缀与后缀完整性 */
    assert(strncmp(t.output_content, "abcabcabc", 9) == 0);
    assert(strcmp(t.output_content + 30000 - 3, "abc") == 0);

    ai_trace_free(&t);
}
```

- [x] **Step 2: 运行测试确认编译失败（字段不存在）**

```bash
cmake --build backend/build --target test_ai_runtime
```
Expected: FAIL，提示 `output_len` 或 `output_cap` 不是 `ai_trace_t` 的成员。

- [x] **Step 3: 修改 `ai_trace.h` 增加长度与容量字段**

在 `backend/src/common/ai_trace.h` 的 `ai_trace_t` 定义中增加：
```c
    char*           output_content;     /**< 完整响应输出内容 */
    size_t          output_len;         /**< 当前输出内容字节长度 (O(1) 访问) */
    size_t          output_cap;         /**< 当前输出缓冲区分配容量 (含末尾 '\0') */
```

- [x] **Step 4: 修改 `ai_trace.c` 实现几何级数倍增**

在 `ai_trace_init()` 中：
```c
    t->output_cap = 1024;
    t->output_len = 0;
    t->output_content = (char*)malloc(t->output_cap);
    if (t->output_content) {
        t->output_content[0] = '\0';
    }
    t->accumulated_len = 0;
```

重写 `ai_trace_append_output()`：
```c
void
ai_trace_append_output(ai_trace_t* t, const char* chunk)
{
    if (!t || !chunk || !chunk[0]) {
        return;
    }
    size_t clen = strlen(chunk);
    size_t required = t->output_len + clen + 1;

    if (required > t->output_cap) {
        size_t new_cap = t->output_cap ? (t->output_cap * 2) : 1024;
        while (new_cap < required) {
            new_cap *= 2;
        }
        char* buf = (char*)realloc(t->output_content, new_cap);
        if (!buf) {
            return;
        }
        t->output_content = buf;
        t->output_cap = new_cap;
    }

    memcpy(t->output_content + t->output_len, chunk, clen);
    t->output_len += clen;
    t->output_content[t->output_len] = '\0';
    t->accumulated_len = (long)t->output_len;
}
```

- [x] **Step 5: 编译并运行测试验证通过**

```bash
cmake --build backend/build --target test_ai_runtime && ./backend/build/test_ai_runtime
```
Expected: PASS，10,000 次追加在 1 毫秒内完成，验证平摊 $O(1)$。

- [x] **Step 6: 提交代码**

```bash
git add backend/src/common/ai_trace.h backend/src/common/ai_trace.c backend/tests/unit/test_ai_runtime.c
git commit -m "perf(ai): ⚡️ amortize ai_trace output buffer growth and eliminate O(N^2) strlen"
```

---

### Task 3: `sse_bridge_text_chunk` 双级无堆缓冲区与 Arena 注入

**Files:**
- Modify: `backend/src/services/ai_service.c:200-265,500-535`

- [x] **Step 1: 在 `backend/src/services/ai_service.c` 扩展 `sse_bridge_t`**

在 `sse_bridge_t` 中加入 `csilk_arena_t* arena` 声明：
```c
typedef struct {
    csilk_ctx_t*    ctx;
    struct timespec last_send_time;
    int             sse_initialized;
    ai_trace_t*     trace;
    csilk_arena_t*  arena;
} sse_bridge_t;
```

- [x] **Step 2: 改造 `sse_bridge_text_chunk` 为栈缓冲与就地转义**

包含 `#include "services/ai/runtime/fast_json.h"`，将原先构造 `csilk_json_t`、序列化及释放代码替换为：
```c
static void
sse_bridge_text_chunk(const char* delta, void* udata)
{
    sse_bridge_t* b = (sse_bridge_t*)udata;
    if (!delta || !b || !b->ctx) {
        return;
    }
    if (!b->sse_initialized) {
        csilk_sse_init(b->ctx);
        clock_gettime(CLOCK_MONOTONIC, &b->last_send_time);
        b->sse_initialized = 1;
    }
    if (b->trace) {
        ai_trace_record_first_token(b->trace);
        ai_trace_append_output(b->trace, delta);
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed_ms = (now.tv_sec - b->last_send_time.tv_sec) * 1000 +
                      (now.tv_nsec - b->last_send_time.tv_nsec) / 1000000;
    if (elapsed_ms >= 15000) {
        csilk_sse_send(b->ctx, NULL, NULL);
    }

    struct timespec real_now;
    clock_gettime(CLOCK_REALTIME, &real_now);
    int64_t   ts_ms = (int64_t)real_now.tv_sec * 1000 + (real_now.tv_nsec / 1000000);
    struct tm tm_buf;
    localtime_r(&real_now.tv_sec, &tm_buf);
    char time_str[32];
    int  ms = (int)(real_now.tv_nsec / 1000000);
    snprintf(time_str,
             sizeof(time_str),
             "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tm_buf.tm_year + 1900,
             tm_buf.tm_mon + 1,
             tm_buf.tm_mday,
             tm_buf.tm_hour,
             tm_buf.tm_min,
             tm_buf.tm_sec,
             ms);

    char   stack_buf[4096];
    char*  target_buf = stack_buf;
    size_t dlen = strlen(delta);
    size_t max_needed = dlen * 6 + 128;

    if (max_needed > sizeof(stack_buf)) {
        if (b->arena) {
            target_buf = (char*)csilk_arena_alloc(b->arena, max_needed);
        } else {
            target_buf = (char*)malloc(max_needed);
        }
        if (!target_buf) return;
    }

    int written = snprintf(target_buf, max_needed, "{\"content\":\"");
    int esc_len = ai_json_escape_fast(delta, dlen, target_buf + written, max_needed - written - 64);
    if (esc_len >= 0) {
        written += esc_len;
        snprintf(target_buf + written, max_needed - written,
                 "\",\"timestamp\":%lld,\"time\":\"%s\"}",
                 (long long)ts_ms, time_str);
        csilk_sse_send(b->ctx, "delta", target_buf);
    }

    if (target_buf != stack_buf && !b->arena) {
        free(target_buf);
    }
    b->last_send_time = now;
}
```

- [x] **Step 3: 在 `ai_service_stream_chat` 注入并管理 Arena 生命周期**

```c
    csilk_arena_t* arena = csilk_arena_new(16384);
    sse_bridge_t sse_bridge = {
        .ctx = c,
        .last_send_time = {0},
        .sse_initialized = 1,
        .trace = &trace,
        .arena = arena,
    };
    clock_gettime(CLOCK_MONOTONIC, &sse_bridge.last_send_time);

    /* ... execute ... */
    ai_runtime_execute_stream(pool, &rctx, &cbs, &sse_bridge);

    csilk_sse_close(c);
    if (arena) {
        csilk_arena_free(arena);
    }
```

- [x] **Step 4: 编译 Minefolio 主程序及测试套件**

```bash
cmake --build backend/build --parallel
```
Expected: PASS，0 警告 0 错误编译成功。

- [x] **Step 5: 提交代码**

```bash
git add backend/src/services/ai_service.c
git commit -m "perf(ai): ⚡️ assemble stream delta using fast-path stack buffer and arena fallback"
```

---

### Task 4: 工作流执行器分块流式分发重构

**Files:**
- Modify: `backend/src/services/ai/workflow/executor.c:120-140`

- [x] **Step 1: 包含 `fast_json.h` 并重写步骤分块循环**

在 `backend/src/services/ai/workflow/executor.c` 中，引入 `services/ai/runtime/fast_json.h`。将原有的 `ev_chunk = csilk_json_object()` 动态构建替换为：

```c
                    while (offset < rlen) {
                        size_t take = (offset + chunk_sz < rlen) ? chunk_sz : (rlen - offset);
                        char   chunk[128];
                        memcpy(chunk, step_out + offset, take);
                        chunk[take] = '\0';
                        offset += take;

                        char   sse_payload[512];
                        int    w = snprintf(sse_payload, sizeof(sse_payload), "{\"content\":\"");
                        int    el = ai_json_escape_fast(chunk, take, sse_payload + w, sizeof(sse_payload) - w - 8);
                        if (el >= 0) {
                            w += el;
                            snprintf(sse_payload + w, sizeof(sse_payload) - w, "\"}");
                            csilk_sse_send(c, "delta", sse_payload);
                        }
                    }
```

- [x] **Step 2: 编译后端并运行单元测试**

```bash
cmake --build backend/build --target test_ai_tools && ./backend/build/test_ai_tools
```
Expected: PASS。

- [x] **Step 3: 提交代码**

```bash
git add backend/src/services/ai/workflow/executor.c
git commit -m "perf(workflow): ⚡️ optimize step chunk SSE streaming with zero-heap JSON framing"
```

---

### Task 5: 全量测试回归与基准验证

**Files:**
- Test: 全量 CTest 单元测试
- Test: 前端 Vitest 单元测试

- [x] **Step 1: 运行后端全部 CTest 套件**

```bash
cd backend/build && ctest -R "test_ai.*" --output-on-failure
```
Expected: `test_ai_tools`, `test_ai_policy`, `test_ai_runtime`, `test_ai_summary` 100% 全部通过。

- [x] **Step 2: 运行前端全量单元测试确保协议完全无损兼容**

```bash
npm --prefix frontend test -- --run
```
Expected: 20 个测试套件，105 个用例全部 PASS，重点包含 `chat.store.spec.ts` 与 `typewriter.spec.ts`。

- [x] **Step 3: 最终文档与交付提交**

```bash
git add docs/superpowers/plans/2026-10-17-ai-streaming-memory-optimization.md
git commit -m "docs: 📝 add complete implementation plan for AI streaming memory optimization"
```
