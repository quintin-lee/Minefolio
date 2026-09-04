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

static void test_runtime_validation_rejection(void) {
    ai_runtime_context_t ctx;
    ai_runtime_context_init(&ctx);
    ctx.user_id = 0; /* Invalid user_id */

    ai_runtime_result_t res = ai_runtime_execute(NULL, &ctx);
    assert(res.status.code == AI_RUNTIME_ERR_VALIDATION);
    assert(res.final_content == NULL);

    ai_runtime_context_free(&ctx);
    printf("PASS: test_runtime_validation_rejection\n");
}

static void test_runtime_elapsed_timeout(void) {
    ai_runtime_limits_t limits = ai_runtime_limits_default();
    limits.timeout_ms = 5000;
    ai_runtime_stats_t stats = {0};
    ai_runtime_status_t status = {0};

    /* 4000ms elapsed -> OK */
    assert(ai_runtime_limits_check_pre_turn(&limits, &stats, NULL, 4000, &status) == true);
    assert(status.code == AI_RUNTIME_ERR_OK);

    /* 5001ms elapsed -> TIMEOUT */
    assert(ai_runtime_limits_check_pre_turn(&limits, &stats, NULL, 5001, &status) == false);
    assert(status.code == AI_RUNTIME_ERR_TIMEOUT);
    assert(strstr(status.detail, "exceeded timeout") != NULL);

    printf("PASS: test_runtime_elapsed_timeout\n");
}

int main(void) {
    test_runtime_error_taxonomy();
    test_runtime_limits_and_budgets();
    test_runtime_memory_window();
    test_runtime_context_lifecycle();
    test_runtime_agent_loop_cancel();
    test_runtime_validation_rejection();
    test_runtime_elapsed_timeout();
    printf("All runtime unit and edge tests passed!\n");
    return 0;
}
