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

static void test_model_price_resolution(void) {
    double in_p = 0.0, out_p = 0.0;

    /* 默认价表：无配置覆盖时按模型/服务商回落 */
    assert(mf_ai_rule_model_prices("openai", "gpt-4o", NULL, NULL, &in_p, &out_p) == 0);
    assert(fabs(in_p - 2.50) < 1e-9 && fabs(out_p - 10.00) < 1e-9);

    assert(mf_ai_rule_model_prices("openai", "gpt-4o-mini", NULL, NULL, &in_p, &out_p) == 0);
    assert(fabs(in_p - 0.15) < 1e-9 && fabs(out_p - 0.60) < 1e-9);

    /* "4o-mini" 子串必须先于 "gpt-4o" 判定 */
    assert(mf_ai_rule_model_prices("deepseek", "deepseek-chat", NULL, NULL, &in_p, &out_p) == 0);
    assert(fabs(in_p - 0.14) < 1e-9 && fabs(out_p - 0.28) < 1e-9);

    assert(mf_ai_rule_model_prices("qwen", "qwen-max", NULL, NULL, &in_p, &out_p) == 0);
    assert(fabs(in_p - 0.20) < 1e-9 && fabs(out_p - 0.60) < 1e-9);

    /* 本地模型免费 */
    assert(mf_ai_rule_model_prices("ollama", "llama3", NULL, NULL, &in_p, &out_p) == 0);
    assert(in_p == 0.0 && out_p == 0.0);

    /* 未知模型/服务商回落通用默认价 */
    assert(mf_ai_rule_model_prices("custom", "my-model", NULL, NULL, &in_p, &out_p) == 0);
    assert(fabs(in_p - 0.50) < 1e-9 && fabs(out_p - 1.50) < 1e-9);

    /* 配置价逐侧覆盖（>0 生效，<=0 该侧保持默认表值） */
    double cfg_in = 1.25;
    assert(mf_ai_rule_model_prices("deepseek", "deepseek-chat", &cfg_in, NULL, &in_p, &out_p) == 0);
    assert(fabs(in_p - 1.25) < 1e-9 && fabs(out_p - 0.28) < 1e-9);

    double cfg_out = 3.0;
    assert(mf_ai_rule_model_prices("openai", "gpt-4o", NULL, &cfg_out, &in_p, &out_p) == 0);
    assert(fabs(in_p - 2.50) < 1e-9 && fabs(out_p - 3.00) < 1e-9);

    /* 负数配置价非法 */
    double neg = -1.0;
    assert(mf_ai_rule_model_prices("openai", "gpt-4o", &neg, NULL, &in_p, &out_p) == -1);
    assert(mf_ai_rule_model_prices("openai", "gpt-4o", NULL, &neg, &in_p, &out_p) == -1);

    /* out 参数为 NULL 非法 */
    assert(mf_ai_rule_model_prices("openai", "gpt-4o", NULL, NULL, NULL, &out_p) == -1);

    printf("PASS: test_model_price_resolution\n");
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
    test_model_price_resolution();
    test_safe_provider_validation();
    printf("All domain ai tests passed successfully!\n");
    return 0;
}
