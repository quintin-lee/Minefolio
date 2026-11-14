#include "domain/dca/rules.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("=== Starting Domain DCA Rules Tests ===\n");

    // 1. validate_plan_required
    assert(mf_dca_rule_validate_plan_required(1, 2, "定投计划", 500.0));
    assert(!mf_dca_rule_validate_plan_required(0, 2, "定投计划", 500.0));
    assert(!mf_dca_rule_validate_plan_required(1, 0, "定投计划", 500.0));
    assert(!mf_dca_rule_validate_plan_required(1, 2, NULL, 500.0));
    assert(!mf_dca_rule_validate_plan_required(1, 2, "", 500.0));
    assert(!mf_dca_rule_validate_plan_required(1, 2, "定投计划", 0.0));
    assert(!mf_dca_rule_validate_plan_required(1, 2, "定投计划", -10.0));
    printf("  [OK] mf_dca_rule_validate_plan_required\n");

    // 2. validate_status
    assert(!mf_dca_rule_validate_status(NULL));
    assert(!mf_dca_rule_validate_status(""));
    assert(!mf_dca_rule_validate_status("pending"));
    assert(mf_dca_rule_validate_status("active"));
    assert(mf_dca_rule_validate_status("paused"));
    assert(mf_dca_rule_validate_status("completed"));
    printf("  [OK] mf_dca_rule_validate_status\n");

    // 3. calc_profit_rate
    assert(fabs(mf_dca_rule_calc_profit_rate(1200.0, 1000.0) - 0.20) < 1e-6);
    assert(fabs(mf_dca_rule_calc_profit_rate(800.0, 1000.0) - (-0.20)) < 1e-6);
    assert(fabs(mf_dca_rule_calc_profit_rate(1000.0, 0.0)) < 1e-6);
    assert(fabs(mf_dca_rule_calc_profit_rate(1000.0, -100.0)) < 1e-6);
    printf("  [OK] mf_dca_rule_calc_profit_rate\n");

    // 4. check_profit_target
    assert(mf_dca_rule_check_profit_target(0.25, 0.20));
    assert(mf_dca_rule_check_profit_target(0.20, 0.20));
    assert(!mf_dca_rule_check_profit_target(0.15, 0.20));
    assert(!mf_dca_rule_check_profit_target(0.25, 0.0));
    assert(!mf_dca_rule_check_profit_target(0.25, -0.1));
    printf("  [OK] mf_dca_rule_check_profit_target\n");

    printf("=== Domain DCA Rules Tests Passed ===\n");
    return 0;
}
