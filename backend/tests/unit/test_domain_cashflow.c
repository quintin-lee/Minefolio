#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "domain/cashflow/entity.h"
#include "domain/cashflow/rules.h"
#include "core/financial/currency.h"

static void test_cashflow_validation(void) {
    currency_t cny = currency_from_str("CNY");
    char err[256];

    /* 1. NULL 检查 */
    assert(mf_cashflow_rule_validate(NULL, err, sizeof(err)) != 0);

    /* 2. 正常合法数据 */
    mf_cashflow_schedule_t s = {0};
    s.user_id = 1;
    s.source_asset_id = 10;
    s.target_asset_id = 20;
    snprintf(s.name, sizeof(s.name), "房屋租金");
    snprintf(s.start_date, sizeof(s.start_date), "2026-01-05");
    money_from_double(3500.0, cny, &s.expected_amount);

    int rc = mf_cashflow_rule_validate(&s, err, sizeof(err));
    assert(rc == 0);

    /* 3. 负数金额非法 */
    s.expected_amount = money_neg(s.expected_amount);
    assert(mf_cashflow_rule_validate(&s, err, sizeof(err)) != 0);

    /* 4. 恢复正数，缺少名称非法 */
    money_from_double(3500.0, cny, &s.expected_amount);
    s.name[0] = '\0';
    assert(mf_cashflow_rule_validate(&s, err, sizeof(err)) != 0);

    printf("PASS: test_cashflow_validation\n");
}

static void test_cashflow_annual_factor(void) {
    double factor = 0.0;
    assert(mf_cashflow_rule_annual_factor("monthly", &factor) == 0 && factor == 12.0);
    assert(mf_cashflow_rule_annual_factor("quarterly", &factor) == 0 && factor == 4.0);
    assert(mf_cashflow_rule_annual_factor("semi_annual", &factor) == 0 && factor == 2.0);
    assert(mf_cashflow_rule_annual_factor("annual", &factor) == 0 && factor == 1.0);
    assert(mf_cashflow_rule_annual_factor("once", &factor) == 0 && factor == 1.0);
    printf("PASS: test_cashflow_annual_factor\n");
}

static void test_cashflow_monthly_matching(void) {
    int day = 0;

    /* 1. 每月计划 */
    bool m = mf_cashflow_rule_matches_month("monthly", "2026-03-15", "", 2026, 8, &day);
    assert(m && day == 15);

    /* 起始月前不发生 */
    m = mf_cashflow_rule_matches_month("monthly", "2026-09-15", "", 2026, 8, &day);
    assert(!m);

    /* 2. 季度计划 (3月、6月、9月、12月) */
    m = mf_cashflow_rule_matches_month("quarterly", "2026-03-10", "", 2026, 6, &day);
    assert(m && day == 10);
    m = mf_cashflow_rule_matches_month("quarterly", "2026-03-10", "", 2026, 7, &day);
    assert(!m);

    /* 3. 截止日期拦截 */
    m = mf_cashflow_rule_matches_month("monthly", "2026-01-10", "2026-05-31", 2026, 6, &day);
    assert(!m);

    /* 4. 日期上限截断至 28 */
    m = mf_cashflow_rule_matches_month("monthly", "2026-01-31", "", 2026, 2, &day);
    assert(m && day == 28);

    printf("PASS: test_cashflow_monthly_matching\n");
}

int main(void) {
    test_cashflow_validation();
    test_cashflow_annual_factor();
    test_cashflow_monthly_matching();
    printf("All domain cashflow tests passed successfully!\n");
    return 0;
}
