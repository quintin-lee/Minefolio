#include "services/ai/workflows/cashflow_forecast.h"
#include "services/ai/workflows/cashflow_forecast_common.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/*  1. Expense Decision (wf_expense_decision)                               */
/* ========================================================================= */

static char*
step_ed_assess(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    (void)ctx_json;
    double target_amount = db_get_num(params, "amount");
    if (target_amount <= 0.0) {
        target_amount = 5000.0;
    }

    int64_t       total = 0;
    csilk_json_t* list = mf_ai_repo_asset_list(pool, user_id, 1, 100, NULL, &total);
    double        liquid_cash = 0.0;
    double        total_liabilities = 0.0;
    if (list) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* a = csilk_json_array_get(list, i);
            const char*   type = csilk_json_get_string(a, "asset_type")
                                     ? csilk_json_get_string(a, "asset_type")
                                     : "";
            double        bal = db_get_num(a, "balance");
            if (bal == 0.0) {
                bal = db_get_num(a, "current_value");
            }

            if (strcmp(type, "cash") == 0 || strcmp(type, "bank") == 0) {
                liquid_cash += bal;
            } else if (strcmp(type, "credit_card") == 0 || strcmp(type, "loan") == 0) {
                total_liabilities += bal;
            }
        }
        csilk_json_free(list);
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "target_amount", target_amount);
    csilk_json_add_number(res, "liquid_cash", liquid_cash);
    csilk_json_add_number(res, "total_liabilities", total_liabilities);

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
step_ed_stress_test(csilk_db_pool_t*    pool,
                    int64_t             user_id,
                    const csilk_json_t* params,
                    const char*         ctx_json)
{
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "assess_liquidity") : NULL;

    double target = s0 ? db_get_num(s0, "target_amount") : 5000.0;
    double liquid = s0 ? db_get_num(s0, "liquid_cash") : 0.0;
    if (root) {
        csilk_json_free(root);
    }

    double remaining_cash = liquid - target;
    double avg_monthly_burn = cf_get_user_avg_monthly_burn(pool, user_id);
    double runway_months = (avg_monthly_burn > 0.0) ? (remaining_cash / avg_monthly_burn)
                                                    : (remaining_cash > 0.0 ? 12.0 : 0.0);
    if (remaining_cash < 0.0) {
        runway_months = 0.0;
    }

    int is_safe = (remaining_cash >= 0.0 && (avg_monthly_burn <= 0.0 || runway_months >= 3.0));

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "remaining_cash", remaining_cash);
    csilk_json_add_number(res, "avg_monthly_burn", avg_monthly_burn);
    csilk_json_add_number(res, "runway_months", runway_months);
    csilk_json_add_bool(res, "is_safe", is_safe);

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
step_ed_report(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "assess_liquidity") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "stress_test") : NULL;

    double target = s0 ? db_get_num(s0, "target_amount") : 5000.0;
    double liquid = s0 ? db_get_num(s0, "liquid_cash") : 0.0;
    double remaining = s1 ? db_get_num(s1, "remaining_cash") : 0.0;
    double monthly_burn = s1 ? db_get_num(s1, "avg_monthly_burn") : 0.0;
    double runway = s1 ? db_get_num(s1, "runway_months") : 0.0;
    int    is_safe = s1 ? csilk_json_get_bool(s1, "is_safe") : 0;

    double installment_3_fee = target * 0.02;
    double installment_3_monthly = (target + installment_3_fee) / 3.0;
    double installment_6_fee = target * 0.042;
    double installment_6_monthly = (target + installment_6_fee) / 6.0;
    double installment_12_fee = target * 0.075;
    double installment_12_monthly = (target + installment_12_fee) / 12.0;

    char burn_str[256];
    if (monthly_burn > 0.0) {
        snprintf(burn_str,
                 sizeof(burn_str),
                 "- 📊 **历史月均刚性开销**：`￥%.2f / 月`\n"
                 "- 🛡️ **支出后可维持保障时长**：`%.1f 个月`（安全边际底线：3.0 个月）\n",
                 monthly_burn,
                 runway);
    } else {
        snprintf(burn_str,
                 sizeof(burn_str),
                 "- 📊 **历史月均刚性开销**：`暂无历史支出记录`\n"
                 "- 🛡️ **支出后资金状态**：`支出后剩余现金 ￥%.2f`\n",
                 remaining);
    }

    char buf[8192];
    snprintf(buf,
             sizeof(buf),
             "### ⚖️ 大额消费智能决策评估报告\n\n"
             "**拟计划支出金额**：`￥%.2f`\n\n"
             "#### 一、流动性压力测试（基于真实账户余额）\n"
             "- 🏦 **当前可用流动现金（现金+银行存款）**：`￥%.2f`\n"
             "- 📉 **支出后剩余备用金**：`%s￥%.2f`\n"
             "%s"
             "- 🚦 **智能决策等级**：%s\n\n"
             "#### 二、支付方式成本量化对比\n"
             "| 支付方案 | 资金占用形式 | 息费成本 | 月均还款压力 | 综合建议 |\n"
             "| :--- | :--- | :--- | :--- | :--- |\n"
             "| **全款一次性支付** | 立即扣除 ￥%.2f | **0 元（无利息）** | 0 元/月 | %s |\n"
             "| **信用卡分期 (3期)** | 分 3 个月扣除 | 预计手续费 ￥%.2f | 约 ￥%.2f / 月 | "
             "适合短期平滑现金流 |\n"
             "| **信用卡分期 (6期)** | 分 6 个月扣除 | 预计手续费 ￥%.2f | 约 ￥%.2f / 月 | "
             "费率与期限较均衡 |\n"
             "| **长期分期 (12期)** | 分 12 个月扣除 | 预计手续费 ￥%.2f | 约 ￥%.2f / 月 | "
             "息费较高，非必要不推荐 |\n\n"
             "#### 三、执行与备忘草案\n"
             "若确定执行该笔支出，点击下方卡片可直接生成记账草案：\n"
             "```action\n"
             "{\n"
             "  \"action_type\": \"daily_expense\",\n"
             "  \"amount\": %.2f,\n"
             "  \"category_name\": \"大额支出\",\n"
             "  \"note\": \"大额决策消费评估\"\n"
             "}\n"
             "```",
             target,
             liquid,
             (remaining >= 0.0 ? "" : "-"),
             fabs(remaining),
             burn_str,
             remaining < 0.0 ? "🔴 **强烈不建议（超出当前可用流动资金）**"
                             : (is_safe ? "🟢 **建议执行（现金流与备用金充裕）**"
                                        : "🟡 **谨慎考虑（将明显削弱紧急备用金）**"),
             target,
             is_safe ? "首选推荐：免除一切手续费，且剩余流动资金充足"
                     : "不建议一次性付清，宜保留流动性防止资金链紧绷",
             installment_3_fee,
             installment_3_monthly,
             installment_6_fee,
             installment_6_monthly,
             installment_12_fee,
             installment_12_monthly,
             target);

    if (root) {
        csilk_json_free(root);
    }
    return strdup(buf);
}

static const ai_workflow_graph_t g_ed_graph = {
    .id = "wf_expense_decision",
    .title = "大额消费智能决策评估",
    .description = "评估大额支出对当前流动性与紧急备用金的冲击，量化全款与分期成本，输出支付建议。",
    .icon = "ph:scales",
    .node_count = 3,
    .nodes =
        {
                {"assess_liquidity",
             "可用现金与负债盘点",
             "拉取现金与银行存款，评估即时可用流动性",
             step_ed_assess},
                {"stress_test",
             "流动性压力测试",
             "模拟大额扣除后剩余资金可维持正常生活月数",
             step_ed_stress_test},
                {"generate_report",
             "支付方式成本对比与建议",
             "全款与各档分期费率量化及行动草案生成",
             step_ed_report},
                },
};

const ai_workflow_graph_t*
ai_workflow_expense_decision_get_graph(void)
{
    return &g_ed_graph;
}
