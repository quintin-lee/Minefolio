# Minefolio P1-05: 统一 AI Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立 Minefolio 专属的统一 AI Runtime，彻底解耦 Controller、Workflow 与底层 LLM 调用，提供可控、具备严格预算限制（轮数/超时/Token/工具/费用/取消）和分类错误体系的 Agent 核心执行循环。

**Architecture:** 采用分层运行时架构（Session、Context、Model、Tool、Workflow、Policy、Trace、Memory），在 `services/ai/runtime/` 下建立集中式状态机循环（Agent Loop）。Controller 与 Workflow 统一通过 `ai_runtime_execute` / `ai_runtime_execute_stream` 接口与运行时交互，工具调用前强制过 Policy 安全管道。

**Tech Stack:** C23 标准，csilk v0.5.2 (AI driver & JSON engine)，SQLite/PostgreSQL，POSIX clock API，CMake。

---

### Task 1: 错误分类体系 (Error Taxonomy: `runtime/error.h/.c`)

**Files:**
- Create: `backend/src/services/ai/runtime/error.h`
- Create: `backend/src/services/ai/runtime/error.c`
- Test: `backend/tests/unit/test_ai_runtime.c`

- [ ] **Step 1: 编写错误分类的单元测试 (Failing Test)**

在 `backend/tests/unit/test_ai_runtime.c` 中添加对错误枚举与辅助函数的测试用例：

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include "services/ai/runtime/error.h"

static void test_runtime_error_taxonomy(void) {
    assert(strcmp(ai_runtime_error_name(AI_RUNTIME_ERR_OK), "OK") == 0);
    assert(strcmp(ai_runtime_error_name(AI_RUNTIME_ERR_MODEL), "MODEL_ERROR") == 0);
    assert(strcmp(ai_runtime_error_name(AI_RUNTIME_ERR_TOOL), "TOOL_ERROR") == 0);
    assert(strcmp(ai_runtime_error_name(AI_RUNTIME_ERR_POLICY), "POLICY_ERROR") == 0);
    assert(strcmp(ai_runtime_error_name(AI_RUNTIME_ERR_TIMEOUT), "TIMEOUT") == 0);
    assert(strcmp(ai_runtime_error_name(AI_RUNTIME_ERR_CONTEXT_OVERFLOW), "CONTEXT_OVERFLOW") == 0);
    assert(strcmp(ai_runtime_error_name(AI_RUNTIME_ERR_VALIDATION), "VALIDATION_ERROR") == 0);
    assert(strcmp(ai_runtime_error_name(AI_RUNTIME_ERR_CANCELLED), "CANCELLED") == 0);

    ai_runtime_status_t st = {0};
    ai_runtime_status_set(&st, AI_RUNTIME_ERR_POLICY, "Permission denied for tool", "asset_delete requires ADMIN");
    assert(st.code == AI_RUNTIME_ERR_POLICY);
    assert(strcmp(st.message, "Permission denied for tool") == 0);
    assert(strcmp(st.detail, "asset_delete requires ADMIN") == 0);

    printf("PASS: test_runtime_error_taxonomy\n");
}

int main(void) {
    test_runtime_error_taxonomy();
    printf("All runtime initial tests passed!\n");
    return 0;
}
```

- [ ] **Step 2: 在 `backend/CMakeLists.txt` 中注册 `test_ai_runtime` 并验证编译失败**

修改 `backend/CMakeLists.txt` 注册新测试：
```cmake
add_executable(test_ai_runtime tests/unit/test_ai_runtime.c ${LIB_SOURCES})
target_include_directories(test_ai_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_ai_runtime PRIVATE -UNDEBUG)
target_link_libraries(test_ai_runtime PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
add_test(NAME test_ai_runtime COMMAND test_ai_runtime)
```

运行编译命令验证失败：
```bash
cd /data/home/quintin/workspace/source/c/Minefolio/backend/build && cmake .. && make test_ai_runtime
```
预期结果：编译失败，提示缺少 `services/ai/runtime/error.h`。

- [ ] **Step 3: 实现 `runtime/error.h` 与 `runtime/error.c`**

创建 `backend/src/services/ai/runtime/error.h`：
```c
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AI_RUNTIME_ERR_OK = 0,
    AI_RUNTIME_ERR_MODEL            = 1001,
    AI_RUNTIME_ERR_TOOL             = 1002,
    AI_RUNTIME_ERR_POLICY           = 1003,
    AI_RUNTIME_ERR_TIMEOUT          = 1004,
    AI_RUNTIME_ERR_CONTEXT_OVERFLOW = 1005,
    AI_RUNTIME_ERR_VALIDATION       = 1006,
    AI_RUNTIME_ERR_CANCELLED        = 1007,
} ai_runtime_error_t;

typedef struct {
    ai_runtime_error_t code;
    char               message[256];
    char               detail[512];
} ai_runtime_status_t;

const char* ai_runtime_error_name(ai_runtime_error_t code);

void ai_runtime_status_set(ai_runtime_status_t* status,
                           ai_runtime_error_t   code,
                           const char*          message,
                           const char*          detail);

#ifdef __cplusplus
}
#endif
```

创建 `backend/src/services/ai/runtime/error.c`：
```c
#include "services/ai/runtime/error.h"
#include <stdio.h>
#include <string.h>

const char*
ai_runtime_error_name(ai_runtime_error_t code)
{
    switch (code) {
    case AI_RUNTIME_ERR_OK:
        return "OK";
    case AI_RUNTIME_ERR_MODEL:
        return "MODEL_ERROR";
    case AI_RUNTIME_ERR_TOOL:
        return "TOOL_ERROR";
    case AI_RUNTIME_ERR_POLICY:
        return "POLICY_ERROR";
    case AI_RUNTIME_ERR_TIMEOUT:
        return "TIMEOUT";
    case AI_RUNTIME_ERR_CONTEXT_OVERFLOW:
        return "CONTEXT_OVERFLOW";
    case AI_RUNTIME_ERR_VALIDATION:
        return "VALIDATION_ERROR";
    case AI_RUNTIME_ERR_CANCELLED:
        return "CANCELLED";
    default:
        return "UNKNOWN_ERROR";
    }
}

void
ai_runtime_status_set(ai_runtime_status_t* status,
                       ai_runtime_error_t   code,
                       const char*          message,
                       const char*          detail)
{
    if (!status) {
        return;
    }
    status->code = code;
    if (message) {
        strncpy(status->message, message, sizeof(status->message) - 1);
        status->message[sizeof(status->message) - 1] = '\0';
    } else {
        status->message[0] = '\0';
    }
    if (detail) {
        strncpy(status->detail, detail, sizeof(status->detail) - 1);
        status->detail[sizeof(status->detail) - 1] = '\0';
    } else {
        status->detail[0] = '\0';
    }
}
```

- [ ] **Step 4: 编译并运行测试验证通过**

```bash
cd /data/home/quintin/workspace/source/c/Minefolio/backend/build && cmake .. && make test_ai_runtime && ./tests/unit/test_ai_runtime
```
预期结果：PASS: test_runtime_error_taxonomy

- [ ] **Step 5: 提交代码**

```bash
git add backend/src/services/ai/runtime/error.h backend/src/services/ai/runtime/error.c backend/tests/unit/test_ai_runtime.c backend/CMakeLists.txt
git commit -m "feat(ai): ✨ add unified ai runtime error taxonomy"
```

---

### Task 2: 预算与限制风控 (`runtime/limits.h/.c`)

**Files:**
- Create: `backend/src/services/ai/runtime/limits.h`
- Create: `backend/src/services/ai/runtime/limits.c`
- Modify: `backend/tests/unit/test_ai_runtime.c`

- [ ] **Step 1: 编写限制与预算的测试用例 (Failing Test)**

在 `backend/tests/unit/test_ai_runtime.c` 添加：
```c
#include "services/ai/runtime/limits.h"

static void test_runtime_limits_and_budgets(void) {
    ai_runtime_limits_t limits = {
        .max_iterations = 5,
        .timeout_ms = 1000,
        .token_budget = 2000,
        .tool_budget = 3,
        .cost_budget = 0.05,
    };
    ai_runtime_stats_t stats = {0};
    ai_runtime_status_t status = {0};
    volatile bool cancel_flag = false;

    /* 1. 正常轮次检查 */
    assert(ai_runtime_limits_check_pre_turn(&limits, &stats, &cancel_flag, 0, &status) == true);
    assert(status.code == AI_RUNTIME_ERR_OK);

    /* 2. 迭代轮数超限 */
    stats.iterations_done = 5;
    assert(ai_runtime_limits_check_pre_turn(&limits, &stats, &cancel_flag, 0, &status) == false);
    assert(status.code == AI_RUNTIME_ERR_TIMEOUT);

    /* 重置并测试取消标记 */
    stats.iterations_done = 1;
    cancel_flag = true;
    assert(ai_runtime_limits_check_pre_turn(&limits, &stats, &cancel_flag, 0, &status) == false);
    assert(status.code == AI_RUNTIME_ERR_CANCELLED);
    cancel_flag = false;

    /* 3. Token 预算超限 */
    stats.total_tokens = 2500;
    assert(ai_runtime_limits_check_pre_turn(&limits, &stats, &cancel_flag, 0, &status) == false);
    assert(status.code == AI_RUNTIME_ERR_CONTEXT_OVERFLOW);
    stats.total_tokens = 1000;

    /* 4. 工具预算超限 */
    stats.tool_calls_count = 3;
    assert(ai_runtime_limits_check_tool_budget(&limits, &stats, 1, &status) == false);
    assert(status.code == AI_RUNTIME_ERR_TOOL);

    /* 5. 费用预算超限 */
    stats.total_cost = 0.06;
    assert(ai_runtime_limits_check_pre_turn(&limits, &stats, &cancel_flag, 0, &status) == false);
    assert(status.code == AI_RUNTIME_ERR_CONTEXT_OVERFLOW);

    printf("PASS: test_runtime_limits_and_budgets\n");
}
```
并在 `main()` 中调用 `test_runtime_limits_and_budgets()`.

- [ ] **Step 2: 编译测试验证失败**

```bash
cd /data/home/quintin/workspace/source/c/Minefolio/backend/build && make test_ai_runtime
```
预期：编译失败，提示缺少 `services/ai/runtime/limits.h`。

- [ ] **Step 3: 实现 `runtime/limits.h` 与 `runtime/limits.c`**

创建 `backend/src/services/ai/runtime/limits.h`：
```c
#pragma once
#include "services/ai/runtime/error.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int     max_iterations;   /**< 最大循环轮数（默认 10） */
    int64_t timeout_ms;       /**< 最大执行耗时毫秒数（0 表示不限） */
    int     token_budget;     /**< 最大累计 Token 预算 (0 表示不限) */
    int     tool_budget;      /**< 最大累计工具执行次数 (0 表示不限) */
    double  cost_budget;      /**< 最大累计费用预算 (0 表示不限) */
} ai_runtime_limits_t;

typedef struct {
    int     iterations_done;  /**< 已完成轮数 */
    int     prompt_tokens;    /**< 累计输入 Token */
    int     completion_tokens;/**< 累计输出 Token */
    int     total_tokens;     /**< 累计总 Token */
    int     tool_calls_count; /**< 累计工具调用次数 */
    double  total_cost;       /**< 累计估计成本 (USD) */
    int64_t elapsed_ms;       /**< 累计已消耗时间 (ms) */
} ai_runtime_stats_t;

ai_runtime_limits_t ai_runtime_limits_default(void);

bool ai_runtime_limits_check_pre_turn(const ai_runtime_limits_t* limits,
                                      const ai_runtime_stats_t*  stats,
                                      const volatile bool*       cancel_token,
                                      int64_t                    current_elapsed_ms,
                                      ai_runtime_status_t*       out_status);

bool ai_runtime_limits_check_tool_budget(const ai_runtime_limits_t* limits,
                                         const ai_runtime_stats_t*  stats,
                                         int                        requested_tools,
                                         ai_runtime_status_t*       out_status);

void ai_runtime_limits_record_tokens(ai_runtime_stats_t* stats,
                                     int                 prompt_tokens,
                                     int                 completion_tokens,
                                     double              cost);

void ai_runtime_limits_record_tool_call(ai_runtime_stats_t* stats, int count);

#ifdef __cplusplus
}
#endif
```

创建 `backend/src/services/ai/runtime/limits.c`：
```c
#include "services/ai/runtime/limits.h"
#include <stdio.h>

ai_runtime_limits_t
ai_runtime_limits_default(void)
{
    return (ai_runtime_limits_t){
        .max_iterations = 10,
        .timeout_ms = 60000,
        .token_budget = 32768,
        .tool_budget = 15,
        .cost_budget = 0.50,
    };
}

bool
ai_runtime_limits_check_pre_turn(const ai_runtime_limits_t* limits,
                                 const ai_runtime_stats_t*  stats,
                                 const volatile bool*       cancel_token,
                                 int64_t                    current_elapsed_ms,
                                 ai_runtime_status_t*       out_status)
{
    if (cancel_token && *cancel_token) {
        ai_runtime_status_set(out_status,
                              AI_RUNTIME_ERR_CANCELLED,
                              "Execution cancelled by client",
                              "Cancellation token signaled");
        return false;
    }

    if (!limits || !stats) {
        return true;
    }

    if (limits->max_iterations > 0 && stats->iterations_done >= limits->max_iterations) {
        char detail[128];
        snprintf(detail, sizeof(detail), "Reached max_iterations limit (%d)", limits->max_iterations);
        ai_runtime_status_set(out_status, AI_RUNTIME_ERR_TIMEOUT, "Max iterations exceeded", detail);
        return false;
    }

    if (limits->timeout_ms > 0 && current_elapsed_ms >= limits->timeout_ms) {
        char detail[128];
        snprintf(detail, sizeof(detail), "Elapsed %lld ms exceeded timeout %lld ms",
                 (long long)current_elapsed_ms, (long long)limits->timeout_ms);
        ai_runtime_status_set(out_status, AI_RUNTIME_ERR_TIMEOUT, "Execution timeout", detail);
        return false;
    }

    if (limits->token_budget > 0 && stats->total_tokens >= limits->token_budget) {
        char detail[128];
        snprintf(detail, sizeof(detail), "Total tokens %d exceeded budget %d",
                 stats->total_tokens, limits->token_budget);
        ai_runtime_status_set(out_status, AI_RUNTIME_ERR_CONTEXT_OVERFLOW, "Token budget exceeded", detail);
        return false;
    }

    if (limits->cost_budget > 0.0 && stats->total_cost >= limits->cost_budget) {
        char detail[128];
        snprintf(detail, sizeof(detail), "Total cost $%.4f exceeded budget $%.4f",
                 stats->total_cost, limits->cost_budget);
        ai_runtime_status_set(out_status, AI_RUNTIME_ERR_CONTEXT_OVERFLOW, "Cost budget exceeded", detail);
        return false;
    }

    if (out_status) {
        ai_runtime_status_set(out_status, AI_RUNTIME_ERR_OK, "OK", NULL);
    }
    return true;
}

bool
ai_runtime_limits_check_tool_budget(const ai_runtime_limits_t* limits,
                                    const ai_runtime_stats_t*  stats,
                                    int                        requested_tools,
                                    ai_runtime_status_t*       out_status)
{
    if (!limits || !stats || limits->tool_budget <= 0) {
        return true;
    }

    if (stats->tool_calls_count + requested_tools > limits->tool_budget) {
        char detail[128];
        snprintf(detail, sizeof(detail), "Tool calls count %d + %d would exceed budget %d",
                 stats->tool_calls_count, requested_tools, limits->tool_budget);
        ai_runtime_status_set(out_status, AI_RUNTIME_ERR_TOOL, "Tool budget exceeded", detail);
        return false;
    }
    return true;
}

void
ai_runtime_limits_record_tokens(ai_runtime_stats_t* stats,
                                int                 prompt_tokens,
                                int                 completion_tokens,
                                double              cost)
{
    if (!stats) {
        return;
    }
    if (prompt_tokens > 0) stats->prompt_tokens += prompt_tokens;
    if (completion_tokens > 0) stats->completion_tokens += completion_tokens;
    stats->total_tokens = stats->prompt_tokens + stats->completion_tokens;
    if (cost > 0.0) stats->total_cost += cost;
}

void
ai_runtime_limits_record_tool_call(ai_runtime_stats_t* stats, int count)
{
    if (!stats || count <= 0) {
        return;
    }
    stats->tool_calls_count += count;
}
```

- [ ] **Step 4: 运行测试验证通过**

```bash
cd /data/home/quintin/workspace/source/c/Minefolio/backend/build && cmake .. && make test_ai_runtime && ./tests/unit/test_ai_runtime
```
预期结果：PASS: test_runtime_limits_and_budgets

- [ ] **Step 5: 提交代码**

```bash
git add backend/src/services/ai/runtime/limits.h backend/src/services/ai/runtime/limits.c backend/tests/unit/test_ai_runtime.c
git commit -m "feat(ai): ✨ implement ai runtime limits and budgets validation"
```

---

### Task 3: 记忆与滑动窗口管理 (`memory/memory.h/.c`)

**Files:**
- Create: `backend/src/services/ai/memory/memory.h`
- Create: `backend/src/services/ai/memory/memory.c`
- Modify: `backend/tests/unit/test_ai_runtime.c`

- [ ] **Step 1: 编写记忆管理测试 (Failing Test)**

在 `backend/tests/unit/test_ai_runtime.c` 添加测试：
```c
#include "services/ai/memory/memory.h"

static void test_runtime_memory_window(void) {
    csilk_json_t* hist = csilk_json_array();
    for (int i = 0; i < 10; i++) {
        csilk_json_t* m = csilk_json_object();
        csilk_json_add_string(m, "role", (i % 2 == 0) ? "user" : "assistant");
        char buf[32];
        snprintf(buf, sizeof(buf), "Message #%d", i);
        csilk_json_add_string(m, "content", buf);
        csilk_json_array_append(hist, m);
    }

    /* 1. 窗口截断为 4 条历史：应包含 1 个 system + 4 个历史消息 + 1 个最新 prompt = 6 条 */
    csilk_json_t* msgs = ai_memory_build_messages("System Prompt", hist, "Latest Input", 4);
    assert(msgs != NULL);
    assert(csilk_json_array_size(msgs) == 6);

    csilk_json_t* first = csilk_json_array_get(msgs, 0);
    assert(strcmp(csilk_json_get_string(first, "role"), "system") == 0);
    assert(strcmp(csilk_json_get_string(first, "content"), "System Prompt") == 0);

    csilk_json_t* last = csilk_json_array_get(msgs, 5);
    assert(strcmp(csilk_json_get_string(last, "role"), "user") == 0);
    assert(strcmp(csilk_json_get_string(last, "content"), "Latest Input") == 0);

    csilk_json_free(msgs);
    csilk_json_free(hist);

    printf("PASS: test_runtime_memory_window\n");
}
```
并在 `main()` 中调用。

- [ ] **Step 2: 编译测试验证失败**

```bash
cd /data/home/quintin/workspace/source/c/Minefolio/backend/build && make test_ai_runtime
```
预期：缺少 `services/ai/memory/memory.h`。

- [ ] **Step 3: 实现 `memory/memory.h` 与 `memory/memory.c`**

创建 `backend/src/services/ai/memory/memory.h`：
```c
#pragma once
#include "csilk/csilk.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 构建送入大模型的受控上下文消息数组
 *
 * 保证：
 * 1. 必定包含 system prompt（居首）；
 * 2. 对历史消息按 max_history 进行尾部滑动窗口截取；
 * 3. 追加当前 user_prompt（若非空）。
 *
 * @param system_prompt 系统设定提示词
 * @param history_messages 会话历史消息数组
 * @param user_prompt 当前轮次用户输入（可为空）
 * @param max_history 最大保留历史条数
 * @return 组装好的 messages JSON 数组（需调用方 csilk_json_free 释放）
 */
csilk_json_t* ai_memory_build_messages(const char*         system_prompt,
                                       const csilk_json_t* history_messages,
                                       const char*         user_prompt,
                                       int                 max_history);

#ifdef __cplusplus
}
#endif
```

创建 `backend/src/services/ai/memory/memory.c`：
```c
#include "services/ai/memory/memory.h"
#include <string.h>

csilk_json_t*
ai_memory_build_messages(const char*         system_prompt,
                         const csilk_json_t* history_messages,
                         const char*         user_prompt,
                         int                 max_history)
{
    csilk_json_t* messages = csilk_json_array();

    /* 1. System message */
    const char* sys = (system_prompt && system_prompt[0])
                          ? system_prompt
                          : "你是一个专业的个人财务与财富管理AI助手。";
    csilk_json_t* sys_msg = csilk_json_object();
    csilk_json_add_string(sys_msg, "role", "system");
    csilk_json_add_string(sys_msg, "content", sys);
    csilk_json_array_append(messages, sys_msg);

    /* 2. Sliding window of history messages */
    int win_size = max_history > 0 ? max_history : 20;
    if (history_messages && csilk_json_is_array(history_messages)) {
        size_t total = csilk_json_array_size(history_messages);
        size_t start = total > (size_t)win_size ? total - (size_t)win_size : 0;
        for (size_t i = start; i < total; i++) {
            csilk_json_t* item = csilk_json_array_get(history_messages, i);
            if (item) {
                csilk_json_t* m = csilk_json_object();
                const char*   role = csilk_json_get_string(item, "role");
                const char*   content = csilk_json_get_string(item, "content");
                csilk_json_add_string(m, "role", role ? role : "user");
                csilk_json_add_string(m, "content", content ? content : "");
                csilk_json_array_append(messages, m);
            }
        }
    }

    /* 3. Latest User Prompt */
    if (user_prompt && user_prompt[0]) {
        csilk_json_t* u_msg = csilk_json_object();
        csilk_json_add_string(u_msg, "role", "user");
        csilk_json_add_string(u_msg, "content", user_prompt);
        csilk_json_array_append(messages, u_msg);
    }

    return messages;
}
```

- [ ] **Step 4: 运行测试验证通过**

```bash
cd /data/home/quintin/workspace/source/c/Minefolio/backend/build && cmake .. && make test_ai_runtime && ./tests/unit/test_ai_runtime
```
预期：PASS: test_runtime_memory_window

- [ ] **Step 5: 提交代码**

```bash
git add backend/src/services/ai/memory/memory.h backend/src/services/ai/memory/memory.c backend/tests/unit/test_ai_runtime.c
git commit -m "feat(ai): ✨ add ai runtime memory sliding window manager"
```

---

### Task 4: 统一 Runtime Context 与 Session (`runtime/context.h/.c` & `runtime/session.h/.c`)

**Files:**
- Modify: `backend/src/services/ai/runtime/context.h`
- Modify: `backend/src/services/ai/runtime/context.c`
- Modify: `backend/src/services/ai/runtime/session.h`
- Modify: `backend/src/services/ai/runtime/session.c`
- Modify: `backend/tests/unit/test_ai_runtime.c`

- [ ] **Step 1: 编写 Context 容器初始化与释放的单元测试 (Failing Test)**

在 `backend/tests/unit/test_ai_runtime.c` 添加：
```c
#include "services/ai/runtime/context.h"

static void test_runtime_context_lifecycle(void) {
    ai_runtime_context_t ctx;
    ai_runtime_context_init(&ctx);

    ctx.user_id = 42;
    ctx.session_id = 100;
    strncpy(ctx.model_name, "gpt-4o-mini", sizeof(ctx.model_name) - 1);
    strncpy(ctx.provider_id, "openai", sizeof(ctx.provider_id) - 1);

    assert(ctx.user_id == 42);
    assert(ctx.session_id == 100);
    assert(ctx.limits.max_iterations == 10);
    assert(ctx.messages != NULL);

    ai_runtime_context_free(&ctx);
    printf("PASS: test_runtime_context_lifecycle\n");
}
```
并在 `main()` 中调用。

- [ ] **Step 2: 编译测试验证失败**

```bash
cd /data/home/quintin/workspace/source/c/Minefolio/backend/build && make test_ai_runtime
```
预期：编译失败，提示缺少 `ai_runtime_context_init`。

- [ ] **Step 3: 完善 `runtime/context.h` 与 `runtime/context.c`**

更新 `backend/src/services/ai/runtime/context.h`：
```c
#pragma once
#include "csilk/csilk.h"
#include "csilk/drivers/ai.h"
#include "common/ai_config.h"
#include "services/ai/runtime/error.h"
#include "services/ai/runtime/limits.h"
#include "services/ai/policy/permission.h"
#include "services/ai/trace/trace.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* 1. 用户与权限 */
    int64_t               user_id;
    ai_permission_level_t perm_level;

    /* 2. 会话标识 */
    int64_t               session_id;
    char                  session_title[128];

    /* 3. 消息队列 */
    csilk_json_t*         messages;

    /* 4. 可用工具 */
    csilk_ai_tool_t*      tools;
    size_t                tool_count;

    /* 5. 模型与超参 */
    char                  provider_id[64];
    char                  model_name[128];
    double                temperature;
    int                   max_tokens;
    double                top_p;

    /* 6. 限制、统计与取消 */
    ai_runtime_limits_t   limits;
    ai_runtime_stats_t    stats;
    volatile bool*        cancel_token;

    /* 7. 扩展元数据与追踪 */
    csilk_json_t*         metadata;
    ai_trace_t*           trace;
} ai_runtime_context_t;

void ai_runtime_context_init(ai_runtime_context_t* ctx);
void ai_runtime_context_free(ai_runtime_context_t* ctx);

/* 兼容旧 context 构建 */
csilk_json_t* ai_context_build_messages(const ai_config_t*  cfg,
                                        const csilk_json_t* history_messages,
                                        const char*         user_prompt);

#ifdef __cplusplus
}
#endif
```

更新 `backend/src/services/ai/runtime/context.c`：
```c
#include "services/ai/runtime/context.h"
#include "services/ai/memory/memory.h"
#include <stdlib.h>
#include <string.h>

void
ai_runtime_context_init(ai_runtime_context_t* ctx)
{
    if (!ctx) {
        return;
    }
    memset(ctx, 0, sizeof(ai_runtime_context_t));
    ctx->limits = ai_runtime_limits_default();
    ctx->temperature = 0.7;
    ctx->messages = csilk_json_array();
    ctx->metadata = csilk_json_object();
}

void
ai_runtime_context_free(ai_runtime_context_t* ctx)
{
    if (!ctx) {
        return;
    }
    if (ctx->messages) {
        csilk_json_free(ctx->messages);
        ctx->messages = NULL;
    }
    if (ctx->metadata) {
        csilk_json_free(ctx->metadata);
        ctx->metadata = NULL;
    }
}

csilk_json_t*
ai_context_build_messages(const ai_config_t*  cfg,
                          const csilk_json_t* history_messages,
                          const char*         user_prompt)
{
    const char* sys = (cfg && cfg->system_prompt[0]) ? cfg->system_prompt : NULL;
    int max_hist = (cfg && cfg->context_size > 0) ? cfg->context_size : 20;
    return ai_memory_build_messages(sys, history_messages, user_prompt, max_hist);
}
```

- [ ] **Step 4: 编译并运行测试**

```bash
cd /data/home/quintin/workspace/source/c/Minefolio/backend/build && cmake .. && make test_ai_runtime && ./tests/unit/test_ai_runtime
```
预期：PASS: test_runtime_context_lifecycle

- [ ] **Step 5: 提交代码**

```bash
git add backend/src/services/ai/runtime/context.h backend/src/services/ai/runtime/context.c backend/tests/unit/test_ai_runtime.c
git commit -m "feat(ai): ✨ define unified ai runtime context container"
```

---

### Task 5: Agent Loop 核心循环驱动 (`runtime/loop.h/.c` & `runtime/runtime.h/.c`)

**Files:**
- Modify: `backend/src/services/ai/runtime/loop.h`
- Modify: `backend/src/services/ai/runtime/loop.c`
- Modify: `backend/src/services/ai/runtime/runtime.h`
- Modify: `backend/src/services/ai/runtime/runtime.c`
- Modify: `backend/tests/unit/test_ai_runtime.c`

- [ ] **Step 1: 编写 Agent Loop 同步调用与取消/限制拦截测试 (Failing Test)**

在 `backend/tests/unit/test_ai_runtime.c` 添加测试用例：
```c
#include "services/ai/runtime/runtime.h"

static void test_runtime_agent_loop_cancel(void) {
    ai_runtime_context_t ctx;
    ai_runtime_context_init(&ctx);
    ctx.user_id = 1;
    ctx.session_id = 1;

    volatile bool cancel = true;
    ctx.cancel_token = &cancel;

    ai_runtime_result_t res = ai_runtime_execute(NULL, &ctx);
    assert(res.status.code == AI_RUNTIME_ERR_CANCELLED);
    assert(res.final_content == NULL);

    ai_runtime_context_free(&ctx);
    printf("PASS: test_runtime_agent_loop_cancel\n");
}
```
并在 `main()` 中调用。

- [ ] **Step 2: 编译测试验证失败**

```bash
cd /data/home/quintin/workspace/source/c/Minefolio/backend/build && make test_ai_runtime
```
预期：编译失败，提示缺少 `ai_runtime_execute`。

- [ ] **Step 3: 完善 `runtime/loop.h` 与 `runtime/loop.c`**

定义 `backend/src/services/ai/runtime/loop.h`：
```c
#pragma once
#include "csilk/csilk.h"
#include "services/ai/runtime/context.h"
#include "services/ai/runtime/error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*on_text_chunk)(const char* chunk, void* udata);
    void (*on_tool_call)(const char* id, const char* name, const char* args_json, void* udata);
    void (*on_tool_result)(const char* id, const char* name, const char* result_json, void* udata);
    void (*on_error)(const ai_runtime_status_t* status, void* udata);
    void (*on_done)(const ai_runtime_stats_t* stats, void* udata);
} ai_runtime_callbacks_t;

typedef struct {
    char*               final_content;  /**< 最终文本输出 (需调用方 free) */
    ai_runtime_status_t status;         /**< 执行状态 */
    ai_runtime_stats_t  stats;          /**< 运行指标 */
} ai_runtime_result_t;

ai_runtime_status_t ai_runtime_execute_stream(csilk_db_pool_t*              pool,
                                              ai_runtime_context_t*         ctx,
                                              const ai_runtime_callbacks_t* cbs,
                                              void*                         user_data);

ai_runtime_result_t ai_runtime_execute(csilk_db_pool_t*      pool,
                                       ai_runtime_context_t* ctx);

#ifdef __cplusplus
}
#endif
```

更新 `backend/src/services/ai/runtime/loop.c`，完整实现 Agent 循环（校验 Limits -> 调用模型 -> 解析 -> 工具过 Policy -> 执行工具 -> 追加结果 -> 检查预算 -> 循环 -> 完成），并在 `runtime/runtime.h/.c` 中导出。

- [ ] **Step 4: 编译并运行测试**

```bash
cd /data/home/quintin/workspace/source/c/Minefolio/backend/build && cmake .. && make test_ai_runtime && ./tests/unit/test_ai_runtime
```
预期：PASS: test_runtime_agent_loop_cancel

- [ ] **Step 5: 提交代码**

```bash
git add backend/src/services/ai/runtime/loop.h backend/src/services/ai/runtime/loop.c backend/src/services/ai/runtime/runtime.h backend/src/services/ai/runtime/runtime.c backend/tests/unit/test_ai_runtime.c
git commit -m "feat(ai): ✨ implement agent execution loop with budget controls and policy evaluation"
```

---

### Task 6: 重构 Controller 与 Service，解耦直接 LLM 调用 (`ai_service.c` & `workflow/executor.c`)

**Files:**
- Modify: `backend/src/services/ai_service.c`
- Modify: `backend/src/services/ai/workflow/executor.c`

- [ ] **Step 1: 重构 `ai_service.c` 中的 `ai_chat_handler`**

将 `ai_chat_handler` 中手写的模型调用、工具循环、SSE 组装全面收敛为调用 `ai_runtime_execute_stream`。设置 SSE 回调桥接：
- `on_text_chunk` -> `send_chunk(c, chunk)`
- `on_tool_call` -> `csilk_sse_send(c, "tool_call", ...)`
- `on_tool_result` -> `csilk_sse_send(c, "tool_result", ...)`
- `on_error` -> `send_error(c, ...)`
- `on_done` -> `send_done(c)`

- [ ] **Step 2: 重构 `ai_service_stream_report` 与 `workflow/executor.c`**

让工作流报告生成统一调用 Runtime 驱动流式生成，不再自建 `csilk_ai_new` 与 `csilk_ai_chat`。

- [ ] **Step 3: 编译并运行全部 CTest 测试**

```bash
cd /data/home/quintin/workspace/source/c/Minefolio/backend/build && cmake .. && make -j && ctest --output-on-failure
```
预期结果：27/27 测试 100% 全部通过。

- [ ] **Step 4: 运行端到端集成测试验证**

```bash
cd /data/home/quintin/workspace/source/c/Minefolio/backend && ./tests/test_ai_trace.sh
```
预期结果：全量链路测试通过。

- [ ] **Step 5: 提交代码**

```bash
git add backend/src/services/ai_service.c backend/src/services/ai/workflow/executor.c
git commit -m "refactor(ai): ♻️ decouple controller and workflow from direct llm invocation using ai runtime"
```

---

### Task 7: 全系统验证与文档更新

**Files:**
- Modify: `backend/tests/unit/test_ai_runtime.c` (添加全量覆盖与边缘测试)
- Modify: `AGENTS.md` (记录统一 AI Runtime 架构规范与规则)

- [ ] **Step 1: 运行完整自动化测试套件**

```bash
cd /data/home/quintin/workspace/source/c/Minefolio/backend/build && ctest --output-on-failure
cd /data/home/quintin/workspace/source/c/Minefolio/backend && ./tests/test_link.sh
```
预期结果：CTest 100% 通过，test_link.sh 38 个用例全部通过。

- [ ] **Step 2: 运行前端构建检查确保无破坏**

```bash
npm --prefix /data/home/quintin/workspace/source/c/Minefolio/frontend run build
```
预期结果：vue-tsc 与 vite build 零错误通过。

- [ ] **Step 3: 更新架构文档与规范**

更新 `AGENTS.md` 中关于 AI Runtime、Agent Loop、Limits 与 Error Taxonomy 的说明。

- [ ] **Step 4: 最终提交**

```bash
git add AGENTS.md backend/tests/unit/test_ai_runtime.c
git commit -m "docs(ai): 📝 update agent architecture documentation for unified ai runtime"
```
