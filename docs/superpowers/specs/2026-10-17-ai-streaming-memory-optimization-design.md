# AI 运行时 SSE 流式传输内存池与零拷贝优化设计方案

- **日期**：2026-10-17
- **状态**：Approved (已评审通过)
- **目标范围**：AI 助手 SSE 流式传输 Token 桥接、`ai_trace` 平摊扩容、工作流进度分发及请求级 Arena 生命周期管理

---

## 1. 背景与问题定义

Minefolio 后端基于 C23 与 csilk 框架实现了统一的 AI Agent 运行时。在长连接流式聊天（Server-Sent Events, SSE）与多步骤工作流分析场景下，存在两处高频隐性性能损耗与内存碎片源头：

1. **Token 桥接路径频繁堆分配与 AST 构造**：
   在 `sse_bridge_text_chunk()` 中，LLM 回调产生的每一个 Token（通常仅数个字符）都会经历：
   - 构造 `csilk_json_t` 对象节点（`csilk_json_object()`）；
   - 复制并挂载属性键值对（`csilk_json_add_string`、`csilk_json_add_number`）；
   - 全树深度遍历序列化为 JSON 字符串（`csilk_json_serialize()`）；
   - 释放序列化字符串与深度释放整个 AST 树节点（`csilk_json_free()`）。
   对于一个千级别 Token 的对话，该路径触发数千次 `malloc`/`free` 及多次字符串遍历。

2. **`ai_trace_append_output()` 的 $O(N^2)$ 隐性瓶颈**：
   目前追踪层追加输出文本时，未记录已用长度与预留容量，每次追加都执行 `strlen(output_content)` 重新扫描整串，并调用 `realloc(output_content, olen + clen + 1)` 重新申请单字节扩充。文本越长，扫描越慢，内存拷贝频率越高，形成严重的二次方复杂度。

3. **工作流中间状态内存碎片**：
   在工作流各步骤执行（`ai_workflow_execute_stream`）与文本模拟分块分发中，同样频繁经历临时 JSON 序列化与局部堆释放。

---

## 2. 架构设计与核心结构改造

### 2.1 `ai_trace_t` 平摊扩容结构定义

在 `backend/src/common/ai_trace.h` 中，扩充 `ai_trace_t` 结构体，增加显式的已用长度与总容量记录：

```c
typedef struct {
    /* ... 现有追踪指标字段保持完全兼容 ... */
    char*           output_content;   /**< 累计模型输出文本缓冲区 */
    size_t          output_len;       /**< 当前累计字符串实际字节数 (O(1) 访问) */
    size_t          output_cap;       /**< 当前已分配缓冲区总容量 (包含终止符 '\0') */
    /* ... */
} ai_trace_t;
```

#### 生命周期与扩容规则：
- **初始化**：在 `ai_trace_init()` 中，预分配 `output_cap = 1024` 字节初始缓冲区，`output_len = 0`，首字符置 `\0`。
- **追加策略**：在 `ai_trace_append_output()` 中，当所需容量 `required = output_len + chunk_len + 1 > output_cap` 时，采用 $2 \times$ 几何级数扩容，直至满足需求。
- **复杂度收益**：消除全部 `strlen` 扫描，单次追加均摊复杂度降至 $O(1)$，全生命周期内存拷贝次数从 $N$ 次骤降至 $\approx \log_2(N)$ 次。

---

### 2.2 `sse_bridge_t` 挂载请求级 Arena

在 `backend/src/services/ai_service.c` 中，为流式网络桥 `sse_bridge_t` 注入一个轻量 Arena 内存池：

```c
typedef struct {
    csilk_ctx_t*    ctx;
    struct timespec last_send_time;
    int             sse_initialized;
    ai_trace_t*     trace;
    csilk_arena_t*  arena;            /**< 会话级内存池，用于大分块或临时转义回退 */
} sse_bridge_t;
```

- **分配**：在 `ai_service_stream_chat()` 入口通过 `csilk_arena_new(16384)` 初始化（默认 Chunk 16KB）。
- **销毁**：在 `ai_service_stream_chat()` 退出路径（统一收拢点）调用 `csilk_arena_free()`，所有在流式周期内借用分配的临时缓冲一次性回收。

---

### 2.3 `ai_wf_context_t` 工作流上下文内存池集成

在 `backend/src/services/ai/workflow/graph.h` 中，工作流上下文挂载独立的 `csilk_arena_t* arena`：
- 工作流各步骤节点（`node->execute`）产生的中间计算状态及报告参数在此 Arena 中分配。
- 工作流流式分块推送直接采用栈缓冲就地转义，无需堆内存介入。

---

## 3. 高性能快速转义与零堆流式发射

### 3.1 极速 JSON 转义核心：`ai_json_escape_fast`

在 `backend/src/services/ai/runtime/`（或 `common/`）提供高效 RFC 8259 规范字符串转义函数：

```c
int ai_json_escape_fast(const char* src, size_t src_len, char* dst, size_t dst_cap);
```

#### 转义规则规范：
- `"` $\to$ `\"`
- `\` $\to$ `\\`
- `\b` $\to$ `\b`
- `\f` $\to$ `\f`
- `\n` $\to$ `\n`
- `\r` $\to$ `\r`
- `\t` $\to$ `\t`
- ASCII 控制字符 ($< 0x20$) $\to$ `\u00XX` (如 `\x00` $\to$ `\u0000`)
- 高位 UTF-8 字符（$\ge 0x80$，包含中日韩文字、特殊符号、Emoji 表情）直接透传，零解码重编码损耗。
- 若目标缓冲区不足，返回 `-1` 触发上层扩容。

---

### 3.2 `sse_bridge_text_chunk` 栈/Arena 双级缓冲区组装

流式 Token 响应回调改造逻辑：

```c
static void
sse_bridge_text_chunk(const char* delta, void* udata)
{
    sse_bridge_t* b = (sse_bridge_t*)udata;
    if (!delta || !b || !b->ctx) return;

    if (!b->sse_initialized) {
        csilk_sse_init(b->ctx);
        clock_gettime(CLOCK_MONOTONIC, &b->last_send_time);
        b->sse_initialized = 1;
    }

    if (b->trace) {
        ai_trace_record_first_token(b->trace);
        ai_trace_append_output(b->trace, delta);
    }

    /* 保活心跳检测 (15s) */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed_ms = (now.tv_sec - b->last_send_time.tv_sec) * 1000 +
                      (now.tv_nsec - b->last_send_time.tv_nsec) / 1000000;
    if (elapsed_ms >= 15000) {
        csilk_sse_send(b->ctx, NULL, NULL);
    }

    /* 时间戳生成 */
    struct timespec real_now;
    clock_gettime(CLOCK_REALTIME, &real_now);
    int64_t ts_ms = (int64_t)real_now.tv_sec * 1000 + (real_now.tv_nsec / 1000000);
    struct tm tm_buf;
    localtime_r(&real_now.tv_sec, &tm_buf);
    char time_str[32];
    int ms = (int)(real_now.tv_nsec / 1000000);
    snprintf(time_str, sizeof(time_str), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, ms);

    /* 4KB 栈上双缓冲 + Arena 回退 */
    char stack_buf[4096];
    char* target_buf = stack_buf;
    size_t dlen = strlen(delta);
    size_t max_needed = dlen * 6 + 128; // 最坏转义膨胀 (6x) + 头部与尾部元数据

    if (max_needed > sizeof(stack_buf)) {
        target_buf = (char*)csilk_arena_alloc(b->arena, max_needed);
        if (!target_buf) return;
    }

    int written = snprintf(target_buf, max_needed, "{\"content\":\"");
    int esc_len = ai_json_escape_fast(delta, dlen, target_buf + written, max_needed - written - 64);
    if (esc_len < 0) return;
    written += esc_len;
    written += snprintf(target_buf + written, max_needed - written,
                        "\",\"timestamp\":%lld,\"time\":\"%s\"}",
                        (long long)ts_ms, time_str);

    csilk_sse_send(b->ctx, "delta", target_buf);
    b->last_send_time = now;
}
```

---

## 4. 边界处理与容错机制

1. **超大块防御**：极少数情况下模型以单块形式吐出超长 Markdown（如整张表格代码），`max_needed` 超过 4KB 栈预算时，平滑降级至 `csilk_arena_alloc`，绝不发生栈溢出或截断。
2. **非法控制字符过滤**：严格转义 `\u00XX`，保证组装后的字符串通过标准 JSON 解析器，不破坏前端的流式解码器。
3. **连接异常与资源回收保证**：无论流式过程中是否遭遇网络中断，外层 `ai_service_stream_chat` 与工作流调度器通过 `finally` 式的销毁结构（`csilk_arena_free` + `ai_trace_free`）确保资源释放。

---

## 5. 验证与回归计划

1. **单元测试矩阵** (`backend/tests/unit/test_ai_runtime.c` & `test_ai_summary.c`)：
   - `test_ai_json_escape_fast`：
     - 测试普通字符串；
     - 测试包含 `\"`、`\\`、换行、制表符的字符串；
     - 测试包含中日韩文字与多字节 Emoji（`📈🪙🤖`）的 Unicode 序列；
     - 测试控制字符 `\x00` 至 `\x1f` 的十六进制转义。
   - `test_ai_trace_append_amortized`：
     - 执行 10,000 次 1~3 字符小 Token 持续追加；
     - 验证最终拼接结果与期望字符串完全匹配；
     - 校验 `output_len` 与 `output_cap` 符合 $2^k$ 翻倍规律，耗时控制在毫秒级。
   - `test_ai_stream_arena`：
     - 验证 Arena 分配与大块溢出借用，验证批量销毁无内存悬挂。
2. **端到端回归**：
   - 运行 `ctest --output-on-failure`（除无 PG 本地服务的方言测试外其余 100% 通过）；
   - 运行前端 `npm test`（105/105 测试通过，重点关注 `chat.store.spec.ts` 与 `typewriter.spec.ts`）。
