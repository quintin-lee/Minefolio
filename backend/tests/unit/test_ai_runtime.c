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

int main(void) {
    test_runtime_error_taxonomy();
    test_runtime_limits_and_budgets();
    printf("All runtime initial tests passed!\n");
    return 0;
}
