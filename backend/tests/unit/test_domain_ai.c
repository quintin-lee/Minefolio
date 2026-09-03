#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "domain/ai/entity.h"
#include "domain/ai/rules.h"

static void test_session_title_validation(void) {
    char title_buf[256];
    char err_buf[256];

    /* 1. NULL / 空字符串自动推导为默认标题 "新对话" */
    assert(mf_ai_rule_validate_session_title(NULL, title_buf, sizeof(title_buf), err_buf, sizeof(err_buf)) == 0);
    assert(strcmp(title_buf, "新对话") == 0);

    assert(mf_ai_rule_validate_session_title("", title_buf, sizeof(title_buf), err_buf, sizeof(err_buf)) == 0);
    assert(strcmp(title_buf, "新对话") == 0);

    /* 2. 正常标题透传 */
    assert(mf_ai_rule_validate_session_title("我的财务分析会话", title_buf, sizeof(title_buf), err_buf, sizeof(err_buf)) == 0);
    assert(strcmp(title_buf, "我的财务分析会话") == 0);

    /* 3. 超过 128 字符拦截 */
    char long_title[200];
    memset(long_title, 'a', 150);
    long_title[150] = '\0';
    assert(mf_ai_rule_validate_session_title(long_title, title_buf, sizeof(title_buf), err_buf, sizeof(err_buf)) != 0);

    printf("PASS: test_session_title_validation\n");
}

static void test_context_size_clamping(void) {
    assert(mf_ai_rule_clamp_context_size(1) == 5);
    assert(mf_ai_rule_clamp_context_size(4) == 5);
    assert(mf_ai_rule_clamp_context_size(20) == 20);
    assert(mf_ai_rule_clamp_context_size(100) == 100);
    assert(mf_ai_rule_clamp_context_size(200) == 100);

    printf("PASS: test_context_size_clamping\n");
}

static void test_token_cost_calculation(void) {
    double cost = 0.0;
    /* 1000 prompt tokens @ $2.5/M, 500 completion tokens @ $10.0/M -> 0.0075 */
    int rc = mf_ai_rule_calculate_token_cost(1000, 500, 2.5, 10.0, &cost);
    assert(rc == 0);
    assert(fabs(cost - 0.0075) < 1e-6);

    /* 非法负数 token 拦截 */
    rc = mf_ai_rule_calculate_token_cost(-10, 500, 2.5, 10.0, &cost);
    assert(rc != 0);

    printf("PASS: test_token_cost_calculation\n");
}

static void test_safe_provider_validation(void) {
    assert(mf_ai_rule_is_safe_provider("openai") == true);
    assert(mf_ai_rule_is_safe_provider("deepseek-ai") == true);
    assert(mf_ai_rule_is_safe_provider("qwen_2") == true);

    assert(mf_ai_rule_is_safe_provider(NULL) == false);
    assert(mf_ai_rule_is_safe_provider("") == false);
    assert(mf_ai_rule_is_safe_provider("openai;rm -rf /") == false);
    assert(mf_ai_rule_is_safe_provider("provider with space") == false);

    printf("PASS: test_safe_provider_validation\n");
}

int main(void) {
    test_session_title_validation();
    test_context_size_clamping();
    test_token_cost_calculation();
    test_safe_provider_validation();
    printf("All domain ai tests passed successfully!\n");
    return 0;
}
