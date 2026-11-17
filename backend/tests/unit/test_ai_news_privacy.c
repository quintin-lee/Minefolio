#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "csilk/csilk.h"
#include "services/ai/ai_news_service.h"

int main(void) {
    printf("Running test_ai_news_privacy...\n");

    /* 1. 测试正常多资产的脱敏与比例计算 */
    csilk_json_t* assets = csilk_json_array();
    
    csilk_json_t* a1 = csilk_json_object();
    csilk_json_add_string(a1, "asset_name", "贵州茅台");
    csilk_json_add_string(a1, "asset_type", "stock");
    csilk_json_add_string(a1, "current_value", "60000.00");
    csilk_json_add_string(a1, "cost_basis", "50000.00");
    csilk_json_add_string(a1, "quantity", "30");
    csilk_json_array_append(assets, a1);

    csilk_json_t* a2 = csilk_json_object();
    csilk_json_add_string(a2, "asset_name", "纳指ETF");
    csilk_json_add_string(a2, "asset_type", "fund");
    csilk_json_add_string(a2, "current_value", "40000.00");
    csilk_json_add_string(a2, "cost_basis", "38000.00");
    csilk_json_add_string(a2, "quantity", "20000");
    csilk_json_array_append(assets, a2);

    csilk_json_t* masked = NULL;
    double total_val = 0.0;
    int rc = ai_news_mask_portfolio(assets, &masked, &total_val);
    assert(rc == 0);
    assert(masked != NULL);
    assert(csilk_json_array_size(masked) == 2);
    assert(total_val == 100000.0);

    /* 验证第一项 */
    csilk_json_t* m1 = csilk_json_array_get(masked, 0);
    const char* n1 = csilk_json_get_string(m1, "name");
    const char* c1 = csilk_json_get_string(m1, "category");
    const char* w1 = csilk_json_get_string(m1, "weight");
    assert(strcmp(n1, "贵州茅台") == 0);
    assert(strcmp(c1, "stock") == 0);
    assert(strcmp(w1, "60.0%") == 0);
    /* 关键安全断言：绝对不能泄露金额与数量 */
    assert(csilk_json_get(m1, "current_value") == NULL);
    assert(csilk_json_get(m1, "cost_basis") == NULL);
    assert(csilk_json_get(m1, "quantity") == NULL);

    /* 验证第二项 */
    csilk_json_t* m2 = csilk_json_array_get(masked, 1);
    const char* n2 = csilk_json_get_string(m2, "name");
    const char* c2 = csilk_json_get_string(m2, "category");
    const char* w2 = csilk_json_get_string(m2, "weight");
    assert(strcmp(n2, "纳指ETF") == 0);
    assert(strcmp(c2, "fund") == 0);
    assert(strcmp(w2, "40.0%") == 0);
    assert(csilk_json_get(m2, "current_value") == NULL);

    csilk_json_free(assets);
    csilk_json_free(masked);

    /* 2. 测试单资产场景 (占比 100.0%) */
    csilk_json_t* single_arr = csilk_json_array();
    csilk_json_t* sa = csilk_json_object();
    csilk_json_add_string(sa, "asset_name", "比特币");
    csilk_json_add_string(sa, "asset_type", "crypto");
    csilk_json_add_string(sa, "current_value", "12345.67");
    csilk_json_array_append(single_arr, sa);

    masked = NULL;
    rc = ai_news_mask_portfolio(single_arr, &masked, &total_val);
    assert(rc == 0);
    assert(masked != NULL);
    assert(csilk_json_array_size(masked) == 1);
    csilk_json_t* sm = csilk_json_array_get(masked, 0);
    assert(strcmp(csilk_json_get_string(sm, "weight"), "100.0%") == 0);
    csilk_json_free(single_arr);
    csilk_json_free(masked);

    /* 3. 测试空持仓场景 */
    csilk_json_t* empty_arr = csilk_json_array();
    masked = NULL;
    rc = ai_news_mask_portfolio(empty_arr, &masked, &total_val);
    assert(rc == 0);
    assert(masked != NULL);
    assert(csilk_json_array_size(masked) == 0);
    assert(total_val == 0.0);
    csilk_json_free(empty_arr);
    csilk_json_free(masked);

    /* 4. 测试零市值场景 (防止除以零) */
    csilk_json_t* zero_arr = csilk_json_array();
    csilk_json_t* za = csilk_json_object();
    csilk_json_add_string(za, "asset_name", "已清仓股票");
    csilk_json_add_string(za, "asset_type", "stock");
    csilk_json_add_string(za, "current_value", "0.00");
    csilk_json_array_append(zero_arr, za);

    masked = NULL;
    rc = ai_news_mask_portfolio(zero_arr, &masked, &total_val);
    assert(rc == 0);
    assert(masked != NULL);
    assert(csilk_json_array_size(masked) == 1);
    csilk_json_t* zm = csilk_json_array_get(masked, 0);
    assert(strcmp(csilk_json_get_string(zm, "weight"), "0.0%") == 0);
    csilk_json_free(zero_arr);
    csilk_json_free(masked);

    printf("PASS: test_ai_news_privacy completed successfully\n");
    return 0;
}
