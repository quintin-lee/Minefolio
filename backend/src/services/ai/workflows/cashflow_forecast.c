#include "services/ai/workflows/cashflow_forecast.h"
#include "repositories/asset_repo.h"
#include "repositories/daily_expense_repo.h"
#include "repositories/transaction_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

static void
cf_get_current_month_str(char* out, size_t sz)
{
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    snprintf(out, sz, "%04d-%02d", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1);
}

static double
cf_get_user_avg_monthly_burn(csilk_db_pool_t* pool, int64_t user_id)
{
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char*   params[] = {uid_str, NULL};
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "SELECT AVG(monthly_sum) as avg_burn FROM ("
                                  "  SELECT SUM(amount) as monthly_sum FROM daily_expenses "
                                  "  WHERE user_id = ? AND expense_type = 'expense' "
                                  "  GROUP BY substr(expense_date, 1, 7)"
                                  ")",
                                  params);
    double avg_burn = 0.0;
    if (res && csilk_json_array_size(res) > 0) {
        avg_burn = db_get_num(csilk_json_array_get(res, 0), "avg_burn");
    }
    if (res) {
        csilk_json_free(res);
    }
    return avg_burn;
}

static double
cf_get_asset_val(const csilk_json_t* a)
{
    if (!a) {
        return 0.0;
    }
    double v = db_get_num(a, "current_value");
    if (v == 0.0) {
        v = db_get_num(a, "balance");
    }
    return v;
}

static int
cf_days_in_month(int y, int m)
{
    static const int dm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int              d = dm[m - 1];
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))) {
        d = 29;
    }
    return d;
}

/* ========================================================================= */
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
    csilk_json_t* list = asset_list(pool, user_id, 1, 100, NULL, &total);
    double        liquid_cash = 0.0;
    double        total_liabilities = 0.0;
    if (list) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* a = csilk_json_array_get(list, i);
            const char*   type = csilk_json_get_string(a, "asset_type") ?: "";
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

/* ========================================================================= */
/*  2. Payday Auto-Split (wf_payday_split)                                  */
/* ========================================================================= */

static char*
step_payday_detect(csilk_db_pool_t*    pool,
                   int64_t             user_id,
                   const csilk_json_t* params,
                   const char*         ctx_json)
{
    (void)ctx_json;
    char        month[32];
    const char* m_in = params ? csilk_json_get_string(params, "month") : NULL;
    if (m_in && m_in[0]) {
        strncpy(month, m_in, sizeof(month) - 1);
        month[sizeof(month) - 1] = '\0';
    } else {
        cf_get_current_month_str(month, sizeof(month));
    }
    char pat[64];
    snprintf(pat, sizeof(pat), "%s%%", month);

    double  income_de = 0.0, tx_inflows = 0.0;
    int64_t tx_cnt = 0;

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char*   p[] = {uid_str, pat, NULL};
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT COALESCE(SUM(amount),0) as total FROM daily_expenses WHERE user_id=? AND "
        "expense_type='income' AND expense_date LIKE ?",
        p);
    if (res && csilk_json_array_size(res) > 0) {
        income_de = db_get_num(csilk_json_array_get(res, 0), "total");
    }
    if (res) {
        csilk_json_free(res);
    }

    csilk_json_t* res_tx =
        csilk_db_query_param_json(pool,
                                  "SELECT COALESCE(SUM(amount),0) as total, COUNT(*) as cnt "
                                  "FROM transactions WHERE user_id=? AND transaction_type IN "
                                  "('income','deposit','transfer_in') AND transaction_date LIKE ?",
                                  p);
    if (res_tx && csilk_json_array_size(res_tx) > 0) {
        const csilk_json_t* row = csilk_json_array_get(res_tx, 0);
        tx_inflows = db_get_num(row, "total");
        tx_cnt = db_get_int(row, "cnt");
    }
    if (res_tx) {
        csilk_json_free(res_tx);
    }

    double        liquid_cash = 0.0;
    int64_t       tot = 0;
    csilk_json_t* list = asset_list(pool, user_id, 1, 100, NULL, &tot);
    if (list) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* a = csilk_json_array_get(list, i);
            const char*   t = csilk_json_get_string(a, "asset_type") ?: "";
            if (strcmp(t, "cash") == 0 || strcmp(t, "bank") == 0) {
                liquid_cash += cf_get_asset_val(a);
            }
        }
        csilk_json_free(list);
    }

    double total_income = income_de + tx_inflows;
    if (total_income <= 0.0) {
        total_income = 0.0;
    }

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_string(out, "month", month);
    csilk_json_add_number(out, "income_de", income_de);
    csilk_json_add_number(out, "tx_inflows", tx_inflows);
    csilk_json_add_number(out, "tx_count", (double)tx_cnt);
    csilk_json_add_number(out, "total_income", total_income);
    csilk_json_add_number(out, "liquid_cash", liquid_cash);
    size_t len = 0;
    char*  s = csilk_json_serialize(out, &len);
    csilk_json_free(out);
    return s;
}

static char*
step_payday_allocate(csilk_db_pool_t*    pool,
                     int64_t             user_id,
                     const csilk_json_t* params,
                     const char*         ctx_json)
{
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "payday_detect") : NULL;
    double        total_income = s0 ? db_get_num(s0, "total_income") : 0.0;
    if (root) {
        csilk_json_free(root);
    }

    double r_living = 50.0, r_invest = 20.0, r_debt = 20.0, r_emer = 10.0;
    if (params) {
        double v;
        v = db_get_num(params, "ratio_living");
        if (v > 0.0 && v < 100.0) {
            r_living = v;
        }
        v = db_get_num(params, "ratio_invest");
        if (v > 0.0 && v < 100.0) {
            r_invest = v;
        }
        v = db_get_num(params, "ratio_debt");
        if (v > 0.0 && v < 100.0) {
            r_debt = v;
        }
        v = db_get_num(params, "ratio_emergency");
        if (v > 0.0 && v < 100.0) {
            r_emer = v;
        }
        double sum = r_living + r_invest + r_debt + r_emer;
        if (sum > 0.0 && fabs(sum - 100.0) > 0.5) {
            r_living = r_living / sum * 100.0;
            r_invest = r_invest / sum * 100.0;
            r_debt = r_debt / sum * 100.0;
            r_emer = r_emer / sum * 100.0;
        }
    }

    double amt_living = total_income * r_living / 100.0;
    double amt_invest = total_income * r_invest / 100.0;
    double amt_debt = total_income * r_debt / 100.0;
    double amt_emer = total_income * r_emer / 100.0;

    char          invest_asset[128] = "", debt_asset[128] = "", emer_asset[128] = "";
    int64_t       invest_id = 0, debt_id = 0, emer_id = 0;
    int64_t       tot = 0;
    csilk_json_t* list = asset_list(pool, user_id, 1, 100, NULL, &tot);
    if (list) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* a = csilk_json_array_get(list, i);
            const char*   t = csilk_json_get_string(a, "asset_type") ?: "";
            const char*   nm = csilk_json_get_string(a, "name") ?: "";
            int64_t       aid = db_get_int(a, "id");
            if ((strcmp(t, "fund") == 0 || strcmp(t, "stock") == 0) && invest_id == 0) {
                invest_id = aid;
                strncpy(invest_asset, nm, sizeof(invest_asset) - 1);
            }
            if ((strcmp(t, "loan") == 0 || strcmp(t, "credit_card") == 0) && debt_id == 0) {
                debt_id = aid;
                strncpy(debt_asset, nm, sizeof(debt_asset) - 1);
            }
            if ((strcmp(t, "bank") == 0 || strcmp(t, "cash") == 0) && emer_id == 0) {
                emer_id = aid;
                strncpy(emer_asset, nm, sizeof(emer_asset) - 1);
            }
        }
        csilk_json_free(list);
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "total_income", total_income);
    csilk_json_add_number(res, "ratio_living", r_living);
    csilk_json_add_number(res, "ratio_invest", r_invest);
    csilk_json_add_number(res, "ratio_debt", r_debt);
    csilk_json_add_number(res, "ratio_emergency", r_emer);
    csilk_json_add_number(res, "amt_living", amt_living);
    csilk_json_add_number(res, "amt_invest", amt_invest);
    csilk_json_add_number(res, "amt_debt", amt_debt);
    csilk_json_add_number(res, "amt_emergency", amt_emer);
    csilk_json_add_string(res, "invest_asset", invest_asset);
    csilk_json_add_number(res, "invest_asset_id", (double)invest_id);
    csilk_json_add_string(res, "debt_asset", debt_asset);
    csilk_json_add_number(res, "debt_asset_id", (double)debt_id);
    csilk_json_add_string(res, "emer_asset", emer_asset);
    csilk_json_add_number(res, "emer_asset_id", (double)emer_id);
    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
step_payday_report(csilk_db_pool_t*    pool,
                   int64_t             user_id,
                   const csilk_json_t* params,
                   const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "payday_detect") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "payday_allocate") : NULL;
    const char*   month = s0 ? csilk_json_get_string(s0, "month") : NULL;
    char          month_fb[32];
    if (!month || !month[0]) {
        cf_get_current_month_str(month_fb, sizeof(month_fb));
        month = month_fb;
    }
    double      total = s0 ? db_get_num(s0, "total_income") : 0.0;
    double      liquid = s0 ? db_get_num(s0, "liquid_cash") : 0.0;
    double      r_l = s1 ? db_get_num(s1, "ratio_living") : 50.0;
    double      r_i = s1 ? db_get_num(s1, "ratio_invest") : 20.0;
    double      r_d = s1 ? db_get_num(s1, "ratio_debt") : 20.0;
    double      r_e = s1 ? db_get_num(s1, "ratio_emergency") : 10.0;
    double      a_l = s1 ? db_get_num(s1, "amt_living") : 0.0;
    double      a_i = s1 ? db_get_num(s1, "amt_invest") : 0.0;
    double      a_d = s1 ? db_get_num(s1, "amt_debt") : 0.0;
    double      a_e = s1 ? db_get_num(s1, "amt_emergency") : 0.0;
    const char* inv_name = s1 ? csilk_json_get_string(s1, "invest_asset") : "";
    const char* debt_name = s1 ? csilk_json_get_string(s1, "debt_asset") : "";
    if (!inv_name) {
        inv_name = "";
    }
    if (!debt_name) {
        debt_name = "";
    }

    char mermaid[2048] = {0};
    if (total > 0.0) {
        snprintf(mermaid,
                 sizeof(mermaid),
                 "```mermaid\npie showData\n    title %s 工资分配方案\n    \"生活开销 (%.0f%%)\" : "
                 "%.2f\n    \"定投理财 (%.0f%%)\" : %.2f\n    \"还贷去杠杆 (%.0f%%)\" : %.2f\n    "
                 "\"应急储备 (%.0f%%)\" : %.2f\n```\n\n",
                 month,
                 r_l,
                 a_l,
                 r_i,
                 a_i,
                 r_d,
                 a_d,
                 r_e,
                 a_e);
    } else {
        snprintf(mermaid,
                 sizeof(mermaid),
                 "> 💡 **提示**：%s "
                 "暂无工资/收入入账记录，本方案为演示比例，入账后将自动按真实金额计算。\n\n",
                 month);
    }

    char buf[8192];
    snprintf(buf,
             sizeof(buf),
             "### 💸 %s 工资到账自动分配方案\n\n"
             "**本月可分配收入**：`￥%.2f`（日常收入 ￥%.2f + 交易入账 ￥%.2f）｜ 当前流动现金 "
             "`￥%.2f`\n\n"
             "#### 一、分配方案总览\n"
             "| 用途 | 比例 | 金额 | 去向建议 |\n"
             "| :--- | :--- | :--- | :--- |\n"
             "| 🏠 生活开销 | %.0f%% | ￥%.2f | 日常支出账户，覆盖本月刚性+弹性消费 |\n"
             "| 📈 定投理财 | %.0f%% | ￥%.2f | %s |\n"
             "| 🏦 还贷去杠杆 | %.0f%% | ￥%.2f | %s |\n"
             "| 🛡️ 应急储备 | %.0f%% | ￥%.2f | 活期/货币基金，补足 3-6 月备用金 |\n\n"
             "#### 二、分配可视化\n"
             "%s"
             "#### 三、待确认操作草案\n"
             "点击下方卡片可直接生成转账/记账草案（需二次确认才会动账）：\n"
             "```action\n"
             "{\n"
             "  \"action_type\": \"payday_split\",\n"
             "  \"month\": \"%s\",\n"
             "  \"total_income\": %.2f,\n"
             "  \"allocations\": "
             "[{\"name\":\"生活开销\",\"amount\":%.2f},{\"name\":\"定投理财\",\"amount\":%.2f},{"
             "\"name\":\"还贷\",\"amount\":%.2f},{\"name\":\"应急储备\",\"amount\":%.2f}]\n"
             "}\n"
             "```\n\n"
             "> ℹ️ "
             "比例可在工作流参数中自定义：`ratio_living/ratio_invest/ratio_debt/"
             "ratio_emergency`（自动归一化到 100%%）。",
             month,
             total,
             s0 ? db_get_num(s0, "income_de") : 0.0,
             s0 ? db_get_num(s0, "tx_inflows") : 0.0,
             liquid,
             r_l,
             a_l,
             r_i,
             a_i,
             inv_name[0] ? inv_name : "定投账户（建议选宽基/债券基金）",
             r_d,
             a_d,
             debt_name[0] ? debt_name : "优先偿还利率最高的负债",
             r_e,
             a_e,
             mermaid,
             month,
             total,
             a_l,
             a_i,
             a_d,
             a_e);

    if (root) {
        csilk_json_free(root);
    }
    return strdup(buf);
}

static const ai_workflow_graph_t g_payday_graph = {
    .id = "wf_payday_split",
    .title = "工资到账自动分配",
    .description = "根据当月入账收入与真实账户余额，按刚需/投资/还贷/储蓄比例生成智能分配草案。",
    .icon = "ph:coins",
    .node_count = 3,
    .nodes =
        {
                {"payday_detect", "收入入账检测", "汇总当月工资薪金与各类入账流水", step_payday_detect},
                {"payday_allocate",
             "四象限资金分配",
             "按生活/投资/还债/备用金测算各去向金额",
             step_payday_allocate},
                {"generate_report",
             "分配方案与转账草案",
             "生成分配饼图与待确认转账动账卡片",
             step_payday_report},
                },
};

const ai_workflow_graph_t*
ai_workflow_payday_split_get_graph(void)
{
    return &g_payday_graph;
}

/* ========================================================================= */
/*  3. Budget Guard (wf_budget_guard)                                       */
/* ========================================================================= */

static char*
step_bg_collect(csilk_db_pool_t*    pool,
                int64_t             user_id,
                const csilk_json_t* params,
                const char*         ctx_json)
{
    (void)ctx_json;
    char        month[32];
    const char* m_in = params ? csilk_json_get_string(params, "month") : NULL;
    if (m_in && m_in[0]) {
        strncpy(month, m_in, sizeof(month) - 1);
        month[sizeof(month) - 1] = '\0';
    } else {
        cf_get_current_month_str(month, sizeof(month));
    }
    char pat[64];
    snprintf(pat, sizeof(pat), "%s%%", month);

    csilk_json_t* cur_cats = de_monthly_by_category(pool, user_id, pat);
    csilk_json_t* cur_expense_cats = csilk_json_array();
    double        cur_total = 0.0;
    if (cur_cats && csilk_json_is_array(cur_cats)) {
        size_t n = csilk_json_array_size(cur_cats);
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* it = csilk_json_array_get(cur_cats, i);
            const char*         et = csilk_json_get_string(it, "expense_type");
            if (!et || strcmp(et, "expense") == 0) {
                csilk_json_array_append(cur_expense_cats, csilk_json_copy(it));
                cur_total += db_get_num(it, "amount");
            }
        }
        csilk_json_free(cur_cats);
    }

    csilk_json_t* hist_avg_arr = csilk_json_array();
    char          uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char*   p[] = {uid_str, pat, NULL};
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT category_id, COALESCE(category_name,'未分类') as category_name, "
        "AVG(m_sum) as avg_amount FROM ("
        "  SELECT daily_expenses.category_id, categories.name as category_name, "
        "substr(daily_expenses.expense_date,1,7) as ym, SUM(daily_expenses.amount) as m_sum "
        "FROM daily_expenses "
        "  LEFT JOIN categories ON categories.id = daily_expenses.category_id "
        "  WHERE daily_expenses.user_id=? AND daily_expenses.expense_type='expense' AND "
        "daily_expenses.expense_date NOT LIKE ? "
        "  GROUP BY daily_expenses.category_id, ym"
        ") GROUP BY category_id",
        p);
    if (res && csilk_json_is_array(res)) {
        size_t n = csilk_json_array_size(res);
        for (size_t i = 0; i < n; i++) {
            csilk_json_array_append(hist_avg_arr, csilk_json_copy(csilk_json_array_get(res, i)));
        }
    }
    if (res) {
        csilk_json_free(res);
    }

    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    int    cur_y = tm_buf.tm_year + 1900;
    int    cur_m = tm_buf.tm_mon + 1;
    int    cur_d = tm_buf.tm_mday;
    int    dim = cf_days_in_month(cur_y, cur_m);
    double progress = dim > 0 ? (double)cur_d / (double)dim : 1.0;
    if (progress < 0.05) {
        progress = 0.05;
    }
    if (progress > 1.0) {
        progress = 1.0;
    }

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_string(out, "month", month);
    csilk_json_add_number(out, "day", (double)cur_d);
    csilk_json_add_number(out, "days_in_month", (double)dim);
    csilk_json_add_number(out, "progress", progress);
    csilk_json_add_number(out, "cur_total", cur_total);
    csilk_json_add_array(out, "cur_cats", cur_expense_cats);
    csilk_json_add_array(out, "hist_avg", hist_avg_arr);
    size_t len = 0;
    char*  s = csilk_json_serialize(out, &len);
    csilk_json_free(out);
    return s;
}

static char*
step_bg_forecast(csilk_db_pool_t*    pool,
                 int64_t             user_id,
                 const csilk_json_t* params,
                 const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "bg_collect") : NULL;
    double        progress = s0 ? db_get_num(s0, "progress") : 0.5;
    if (progress < 0.05) {
        progress = 0.05;
    }
    csilk_json_t* cur_cats = s0 ? csilk_json_get(s0, "cur_cats") : NULL;
    csilk_json_t* hist_avg = s0 ? csilk_json_get(s0, "hist_avg") : NULL;
    double        cur_total = s0 ? db_get_num(s0, "cur_total") : 0.0;

    csilk_json_t* forecast_arr = csilk_json_array();
    double        total_budget = 0.0;
    double        total_projected = 0.0;
    int           danger_cnt = 0, warn_cnt = 0;

    size_t n = (cur_cats && csilk_json_is_array(cur_cats)) ? csilk_json_array_size(cur_cats) : 0;
    for (size_t i = 0; i < n; i++) {
        const csilk_json_t* row = csilk_json_array_get(cur_cats, i);
        int64_t             cid = db_get_int(row, "category_id");
        const char*         cname = csilk_json_get_string(row, "category_name") ?: "未分类";
        double              cur_amt = db_get_num(row, "amount");
        double              avg = 0.0;
        if (hist_avg && csilk_json_is_array(hist_avg)) {
            size_t hn = csilk_json_array_size(hist_avg);
            for (size_t j = 0; j < hn; j++) {
                const csilk_json_t* h = csilk_json_array_get(hist_avg, j);
                if (db_get_int(h, "category_id") == cid) {
                    avg = db_get_num(h, "avg_amount");
                    break;
                }
            }
        }
        double budget = 0.0;
        if (avg > 0.0) {
            budget = avg * 1.2;
        } else {
            budget = cur_amt > 0 ? cur_amt / progress * 1.0 : 0.0;
            if (budget < cur_amt) {
                budget = cur_amt * 1.5;
            }
        }
        double      projected = progress > 0 ? cur_amt / progress : cur_amt;
        double      usage_pct = budget > 0 ? (cur_amt / budget) * 100.0 : 0.0;
        double      proj_pct = budget > 0 ? (projected / budget) * 100.0 : 0.0;
        const char* risk = "safe";
        if (proj_pct >= 100.0) {
            risk = "danger";
            danger_cnt++;
        } else if (proj_pct >= 80.0) {
            risk = "warning";
            warn_cnt++;
        }
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_number(item, "category_id", (double)cid);
        csilk_json_add_string(item, "category_name", cname);
        csilk_json_add_number(item, "cur_amount", cur_amt);
        csilk_json_add_number(item, "avg_amount", avg);
        csilk_json_add_number(item, "budget", budget);
        csilk_json_add_number(item, "projected", projected);
        csilk_json_add_number(item, "usage_pct", usage_pct);
        csilk_json_add_number(item, "proj_pct", proj_pct);
        csilk_json_add_string(item, "risk", risk);
        csilk_json_array_append(forecast_arr, item);
        total_budget += budget;
        total_projected += projected;
    }

    double      total_usage_pct = total_budget > 0 ? (cur_total / total_budget) * 100.0 : 0.0;
    double      total_proj_pct = total_budget > 0 ? (total_projected / total_budget) * 100.0 : 0.0;
    const char* overall_risk = "safe";
    if (total_proj_pct >= 100.0 || danger_cnt > 0) {
        overall_risk = "danger";
    } else if (total_proj_pct >= 80.0 || warn_cnt > 0) {
        overall_risk = "warning";
    }

    if (root) {
        csilk_json_free(root);
    }
    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "cur_total", cur_total);
    csilk_json_add_number(out, "total_budget", total_budget);
    csilk_json_add_number(out, "total_projected", total_projected);
    csilk_json_add_number(out, "total_usage_pct", total_usage_pct);
    csilk_json_add_number(out, "total_proj_pct", total_proj_pct);
    csilk_json_add_string(out, "overall_risk", overall_risk);
    csilk_json_add_number(out, "danger_cnt", (double)danger_cnt);
    csilk_json_add_number(out, "warning_cnt", (double)warn_cnt);
    csilk_json_add_number(out, "progress", progress);
    csilk_json_add_array(out, "forecast", forecast_arr);
    size_t len = 0;
    char*  s = csilk_json_serialize(out, &len);
    csilk_json_free(out);
    return s;
}

static char*
step_bg_report(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "bg_collect") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "bg_forecast") : NULL;
    const char*   month = s0 ? csilk_json_get_string(s0, "month") : NULL;
    char          month_fb[32];
    if (!month || !month[0]) {
        cf_get_current_month_str(month_fb, sizeof(month_fb));
        month = month_fb;
    }
    double      cur_total = s1 ? db_get_num(s1, "cur_total") : 0.0;
    double      total_budget = s1 ? db_get_num(s1, "total_budget") : 0.0;
    double      total_proj = s1 ? db_get_num(s1, "total_projected") : 0.0;
    double      progress = s1 ? db_get_num(s1, "progress") : 0.5;
    const char* overall_risk = s1 ? csilk_json_get_string(s1, "overall_risk") : "safe";
    if (!overall_risk) {
        overall_risk = "safe";
    }
    int           danger_cnt = s1 ? (int)db_get_num(s1, "danger_cnt") : 0;
    int           warn_cnt = s1 ? (int)db_get_num(s1, "warning_cnt") : 0;
    csilk_json_t* forecast = s1 ? csilk_json_get(s1, "forecast") : NULL;
    size_t fc_n = (forecast && csilk_json_is_array(forecast)) ? csilk_json_array_size(forecast) : 0;

    char mermaid[4096] = {0};
    if (fc_n > 0) {
        snprintf(mermaid,
                 sizeof(mermaid),
                 "```mermaid\nxychart-beta\n    title \"%s 各分类预算执行进度\"\n"
                 "    x-axis [",
                 month);
        for (size_t i = 0; i < fc_n && i < 6; i++) {
            const csilk_json_t* it = csilk_json_array_get(forecast, i);
            const char*         nm = csilk_json_get_string(it, "category_name") ?: "未分类";
            char                seg[64];
            snprintf(seg, sizeof(seg), "%s\"%s\"", i ? "," : "", nm);
            strncat(mermaid, seg, sizeof(mermaid) - strlen(mermaid) - 1);
        }
        strncat(
            mermaid, "]\n    y-axis \"金额(￥)\" 0 --> ", sizeof(mermaid) - strlen(mermaid) - 1);
        {
            double max_v = 0;
            for (size_t i = 0; i < fc_n && i < 6; i++) {
                double b = db_get_num(csilk_json_array_get(forecast, i), "budget");
                double p = db_get_num(csilk_json_array_get(forecast, i), "projected");
                if (b > max_v) {
                    max_v = b;
                }
                if (p > max_v) {
                    max_v = p;
                }
            }
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "%.0f\n", max_v * 1.2 + 100);
            strncat(mermaid, tmp, sizeof(mermaid) - strlen(mermaid) - 1);
        }
        strncat(mermaid, "    bar [", sizeof(mermaid) - strlen(mermaid) - 1);
        for (size_t i = 0; i < fc_n && i < 6; i++) {
            char tmp[32];
            snprintf(tmp,
                     sizeof(tmp),
                     "%s%.0f",
                     i ? "," : "",
                     db_get_num(csilk_json_array_get(forecast, i), "cur_amount"));
            strncat(mermaid, tmp, sizeof(mermaid) - strlen(mermaid) - 1);
        }
        strncat(mermaid, "]\n    bar [", sizeof(mermaid) - strlen(mermaid) - 1);
        for (size_t i = 0; i < fc_n && i < 6; i++) {
            char tmp[32];
            snprintf(tmp,
                     sizeof(tmp),
                     "%s%.0f",
                     i ? "," : "",
                     db_get_num(csilk_json_array_get(forecast, i), "budget"));
            strncat(mermaid, tmp, sizeof(mermaid) - strlen(mermaid) - 1);
        }
        strncat(mermaid, "]\n```\n\n", sizeof(mermaid) - strlen(mermaid) - 1);
    } else {
        snprintf(mermaid, sizeof(mermaid), "> 💡 **提示**：%s 暂无分类支出数据。\n\n", month);
    }

    char table_rows[4096] = {0};
    for (size_t i = 0; i < fc_n && i < 10; i++) {
        const csilk_json_t* it = csilk_json_array_get(forecast, i);
        const char*         nm = csilk_json_get_string(it, "category_name") ?: "未分类";
        double              cur = db_get_num(it, "cur_amount");
        double              budget = db_get_num(it, "budget");
        double              proj = db_get_num(it, "projected");
        const char*         risk = csilk_json_get_string(it, "risk") ?: "safe";
        const char* badge = strcmp(risk, "danger") == 0
                                ? "🔴 超支预警"
                                : (strcmp(risk, "warning") == 0 ? "🟡 接近预算" : "🟢 安全");
        char        line[256];
        snprintf(line,
                 sizeof(line),
                 "| %s | ￥%.0f | ￥%.0f | ￥%.0f | %s |\n",
                 nm,
                 cur,
                 budget,
                 proj,
                 badge);
        strncat(table_rows, line, sizeof(table_rows) - strlen(table_rows) - 1);
    }
    if (!table_rows[0]) {
        snprintf(table_rows, sizeof(table_rows), "| — | — | — | — | — |\n");
    }

    const char* risk_label =
        strcmp(overall_risk, "danger") == 0
            ? "🔴 **高风险：预计月底将超预算**"
            : (strcmp(overall_risk, "warning") == 0 ? "🟡 **中风险：部分分类接近预算**"
                                                    : "🟢 **整体安全：预算执行良好**");

    char buf[12288];
    snprintf(buf,
             sizeof(buf),
             "### 🚨 %s 预算超支预警报告\n\n"
             "**本月进度**：`%.0f%%`（已过 %d / %d 天）｜ **已支出** `￥%.2f` / 预算 `￥%.2f` ｜ "
             "**预计月底** `￥%.2f`\n\n"
             "**综合风险**：%s（🔴 %d 类超支 / 🟡 %d 类预警）\n\n"
             "#### 一、分分类预算执行明细\n"
             "| 分类 | 已支出 | 预算(历史均值×1.2) | 预计月底 | 状态 |\n"
             "| :--- | :--- | :--- | :--- | :--- |\n"
             "%s\n"
             "#### 二、预算执行可视化\n"
             "%s"
             "#### 三、节流建议\n"
             "1. **优先管控超支分类**：对 🔴 标记分类立即收紧非必要消费，必要时设置日限额；\n"
             "2. **预警分类提前规划**：🟡 分类未来 10 天内按 70%% 强度执行，预留缓冲；\n"
             "3. **整体节奏校准**：按当前进度外推，若超支风险持续，建议本月剩余时间日均支出控制在 "
             "`￥%.0f` 以内。\n",
             month,
             progress * 100.0,
             s0 ? (int)db_get_num(s0, "day") : 0,
             s0 ? (int)db_get_num(s0, "days_in_month") : 30,
             cur_total,
             total_budget,
             total_proj,
             risk_label,
             danger_cnt,
             warn_cnt,
             table_rows,
             mermaid,
             total_budget > 0 && total_proj > total_budget
                 ? (total_proj - cur_total) / ((1.0 - progress) > 0.05 ? (1.0 - progress) * 30 : 10)
                 : (total_budget > 0 ? (total_budget - cur_total) / 15.0 : 200.0));

    if (root) {
        csilk_json_free(root);
    }
    return strdup(buf);
}

static const ai_workflow_graph_t g_bg_graph = {
    .id = "wf_budget_guard",
    .title = "预算超支预警",
    .description = "按当前日期进度线性外推月度总开销，分分类识别超支风险并给出节流限额建议。",
    .icon = "ph:warning-octagon",
    .node_count = 3,
    .nodes =
        {
                {"bg_collect",
             "当月开销与历史基线汇总",
             "汇聚本月分类支出与历史 6 个月月均消费",
             step_bg_collect},
                {"bg_forecast",
             "月末支出预测与超支识别",
             "按天数进度外推各分类预计总额并标记红黄牌",
             step_bg_forecast},
                {"generate_report",
             "预警仪表盘与节流建议",
             "生成分类预算柱状图与日限额管控草案",
             step_bg_report},
                },
};

const ai_workflow_graph_t*
ai_workflow_budget_guard_get_graph(void)
{
    return &g_bg_graph;
}

/* ========================================================================= */
/*  4. Anomaly Detect (wf_anomaly_detect)                                   */
/* ========================================================================= */

static char*
step_ad_collect(csilk_db_pool_t*    pool,
                int64_t             user_id,
                const csilk_json_t* params,
                const char*         ctx_json)
{
    (void)ctx_json;
    int lookback_days = (int)db_get_num(params, "lookback_days");
    if (lookback_days <= 0) {
        lookback_days = 60;
    }
    if (lookback_days > 180) {
        lookback_days = 180;
    }
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    tm_buf.tm_mday -= lookback_days;
    mktime(&tm_buf);
    char since_date[32];
    snprintf(since_date,
             sizeof(since_date),
             "%04d-%02d-%02d",
             tm_buf.tm_year + 1900,
             tm_buf.tm_mon + 1,
             tm_buf.tm_mday);

    csilk_json_t* recent = csilk_json_array();
    double        total = 0.0;
    int64_t       cnt = 0;
    char          uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char*   p[] = {uid_str, since_date, NULL};
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT de.id, de.amount, de.expense_date, de.note, "
        "COALESCE(c.name,'未分类') as category_name, de.category_id "
        "FROM daily_expenses de LEFT JOIN categories c ON c.id=de.category_id "
        "WHERE de.user_id=? AND de.expense_type='expense' AND de.expense_date >= ? "
        "ORDER BY de.expense_date DESC, de.id DESC LIMIT 500",
        p);
    if (res && csilk_json_is_array(res)) {
        size_t n = csilk_json_array_size(res);
        for (size_t i = 0; i < n; i++) {
            csilk_json_array_append(recent, csilk_json_copy(csilk_json_array_get(res, i)));
            total += db_get_num(csilk_json_array_get(res, i), "amount");
            cnt++;
        }
    }
    if (res) {
        csilk_json_free(res);
    }

    csilk_json_t* cat_stats = csilk_json_array();
    csilk_json_t* res_stats = csilk_db_query_param_json(
        pool,
        "SELECT category_id, COALESCE(c.name,'未分类') as category_name, "
        "COUNT(*) as cnt, AVG(amount) as avg_amt, "
        "AVG(amount*amount) as avg_sq "
        "FROM daily_expenses de LEFT JOIN categories c ON c.id=de.category_id "
        "WHERE de.user_id=? AND de.expense_type='expense' AND de.expense_date >= ? "
        "GROUP BY category_id",
        p);
    if (res_stats && csilk_json_is_array(res_stats)) {
        size_t n = csilk_json_array_size(res_stats);
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* r = csilk_json_array_get(res_stats, i);
            double              avg = db_get_num(r, "avg_amt");
            double              avg_sq = db_get_num(r, "avg_sq");
            double              var = avg_sq - avg * avg;
            if (var < 0) {
                var = 0;
            }
            double        std = sqrt(var);
            csilk_json_t* o = csilk_json_object();
            csilk_json_add_number(o, "category_id", (double)db_get_int(r, "category_id"));
            csilk_json_add_string(
                o, "category_name", csilk_json_get_string(r, "category_name") ?: "未分类");
            csilk_json_add_number(o, "cnt", db_get_num(r, "cnt"));
            csilk_json_add_number(o, "avg", avg);
            csilk_json_add_number(o, "std", std);
            csilk_json_array_append(cat_stats, o);
        }
    }
    if (res_stats) {
        csilk_json_free(res_stats);
    }

    csilk_json_t* tx_recent = csilk_json_array();
    csilk_json_t* res_tx = csilk_db_query_param_json(
        pool,
        "SELECT id, amount, transaction_type, note, transaction_date FROM transactions "
        "WHERE user_id=? AND transaction_date >= ? ORDER BY transaction_date DESC LIMIT 500",
        p);
    if (res_tx && csilk_json_is_array(res_tx)) {
        size_t n = csilk_json_array_size(res_tx);
        for (size_t i = 0; i < n; i++) {
            csilk_json_array_append(tx_recent, csilk_json_copy(csilk_json_array_get(res_tx, i)));
        }
    }
    if (res_tx) {
        csilk_json_free(res_tx);
    }

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_string(out, "since_date", since_date);
    csilk_json_add_number(out, "lookback_days", (double)lookback_days);
    csilk_json_add_number(out, "total_amount", total);
    csilk_json_add_number(out, "count", (double)cnt);
    csilk_json_add_array(out, "recent", recent);
    csilk_json_add_array(out, "cat_stats", cat_stats);
    csilk_json_add_array(out, "tx_recent", tx_recent);
    size_t len = 0;
    char*  s = csilk_json_serialize(out, &len);
    csilk_json_free(out);
    return s;
}

static char*
step_ad_score(csilk_db_pool_t*    pool,
              int64_t             user_id,
              const csilk_json_t* params,
              const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "ad_collect") : NULL;
    csilk_json_t* recent = s0 ? csilk_json_get(s0, "recent") : NULL;
    csilk_json_t* cat_stats = s0 ? csilk_json_get(s0, "cat_stats") : NULL;
    csilk_json_t* tx_recent = s0 ? csilk_json_get(s0, "tx_recent") : NULL;

    csilk_json_t* anomalies = csilk_json_array();
    int           cnt_3sigma = 0, cnt_dup = 0, cnt_midnight = 0, cnt_freq = 0;

    size_t rn = (recent && csilk_json_is_array(recent)) ? csilk_json_array_size(recent) : 0;
    for (size_t i = 0; i < rn; i++) {
        const csilk_json_t* row = csilk_json_array_get(recent, i);
        int64_t             cid = db_get_int(row, "category_id");
        double              amt = db_get_num(row, "amount");
        const char*         cname = csilk_json_get_string(row, "category_name") ?: "未分类";
        const char*         note = csilk_json_get_string(row, "note") ?: "";
        const char*         date = csilk_json_get_string(row, "expense_date") ?: "";
        double              avg = 0, std = 0, cnt = 0;
        if (cat_stats && csilk_json_is_array(cat_stats)) {
            size_t sn = csilk_json_array_size(cat_stats);
            for (size_t j = 0; j < sn; j++) {
                const csilk_json_t* st = csilk_json_array_get(cat_stats, j);
                if (db_get_int(st, "category_id") == cid) {
                    avg = db_get_num(st, "avg");
                    std = db_get_num(st, "std");
                    cnt = db_get_num(st, "cnt");
                    break;
                }
            }
        }
        int  is_anomaly = 0;
        char reason[256] = {0};
        if (cnt >= 5 && std > 1e-6) {
            double thr = avg + 3.0 * std;
            if (amt > thr && amt > avg * 2.0) {
                is_anomaly = 1;
                snprintf(reason,
                         sizeof(reason),
                         "金额 %.0f 超过 3σ 阈值 %.0f（均值%.0f±%.0f）",
                         amt,
                         thr,
                         avg,
                         std);
            }
        }
        if (is_anomaly) {
            csilk_json_t* an = csilk_json_object();
            csilk_json_add_string(an, "type", "3sigma");
            csilk_json_add_string(an, "category_name", cname);
            csilk_json_add_number(an, "amount", amt);
            csilk_json_add_string(an, "date", date);
            csilk_json_add_string(an, "note", note);
            csilk_json_add_string(an, "reason", reason);
            csilk_json_array_append(anomalies, an);
            cnt_3sigma++;
        }
    }

    if (recent && csilk_json_is_array(recent)) {
        for (size_t i = 0; i < rn; i++) {
            const csilk_json_t* a = csilk_json_array_get(recent, i);
            double              amta = db_get_num(a, "amount");
            int64_t             ida = db_get_int(a, "id");
            const char*         da = csilk_json_get_string(a, "expense_date") ?: "";
            for (size_t j = i + 1; j < rn; j++) {
                const csilk_json_t* b = csilk_json_array_get(recent, j);
                double              amtb = db_get_num(b, "amount");
                int64_t             idb = db_get_int(b, "id");
                const char*         db_str = csilk_json_get_string(b, "expense_date") ?: "";
                if (ida != idb && fabs(amta - amtb) < 0.01 && strcmp(da, db_str) == 0 &&
                    amta >= 10.0) {
                    csilk_json_t* an = csilk_json_object();
                    csilk_json_add_string(an, "type", "duplicate");
                    csilk_json_add_string(
                        an, "category_name", csilk_json_get_string(a, "category_name") ?: "未分类");
                    csilk_json_add_number(an, "amount", amta);
                    csilk_json_add_string(an, "date", da);
                    csilk_json_add_string(an, "note", csilk_json_get_string(a, "note") ?: "");
                    csilk_json_add_string(
                        an, "reason", "同一天出现相同金额扣款，疑似重复记账/扣款");
                    csilk_json_array_append(anomalies, an);
                    cnt_dup++;
                    break;
                }
            }
        }
    }

    if (tx_recent && csilk_json_is_array(tx_recent)) {
        size_t txn = csilk_json_array_size(tx_recent);
        for (size_t i = 0; i < txn; i++) {
            const csilk_json_t* t = csilk_json_array_get(tx_recent, i);
            const char*         td = csilk_json_get_string(t, "transaction_date") ?: "";
            double              amt = db_get_num(t, "amount");
            int                 hh = -1;
            if (strlen(td) >= 13 && (td[10] == ' ' || td[10] == 'T')) {
                hh = (td[11] - '0') * 10 + (td[12] - '0');
            }
            if (hh >= 0 && hh <= 5 && amt >= 500.0) {
                csilk_json_t* an = csilk_json_object();
                csilk_json_add_string(an, "type", "midnight");
                csilk_json_add_string(an, "category_name", "交易/转账");
                csilk_json_add_number(an, "amount", amt);
                csilk_json_add_string(an, "date", td);
                csilk_json_add_string(an, "note", csilk_json_get_string(t, "note") ?: "");
                csilk_json_add_string(an, "reason", "凌晨 0:00-5:00 期间发生大额交易");
                csilk_json_array_append(anomalies, an);
                cnt_midnight++;
            }
        }
    }

    if (root) {
        csilk_json_free(root);
    }
    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "total_anomalies", (double)csilk_json_array_size(anomalies));
    csilk_json_add_number(out, "cnt_3sigma", (double)cnt_3sigma);
    csilk_json_add_number(out, "cnt_dup", (double)cnt_dup);
    csilk_json_add_number(out, "cnt_midnight", (double)cnt_midnight);
    csilk_json_add_number(out, "cnt_freq", (double)cnt_freq);
    csilk_json_add_array(out, "anomalies", anomalies);
    size_t len = 0;
    char*  s = csilk_json_serialize(out, &len);
    csilk_json_free(out);
    return s;
}

static char*
step_ad_report(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "ad_collect") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "ad_score") : NULL;
    const char*   since = s0 ? csilk_json_get_string(s0, "since_date") : "";
    if (!since) {
        since = "";
    }
    double        total = s1 ? db_get_num(s1, "total_anomalies") : 0;
    csilk_json_t* anomalies = s1 ? csilk_json_get(s1, "anomalies") : NULL;
    size_t        an =
        (anomalies && csilk_json_is_array(anomalies)) ? csilk_json_array_size(anomalies) : 0;
    int c3 = s1 ? (int)db_get_num(s1, "cnt_3sigma") : 0;
    int cd = s1 ? (int)db_get_num(s1, "cnt_dup") : 0;
    int cm = s1 ? (int)db_get_num(s1, "cnt_midnight") : 0;
    int cf = s1 ? (int)db_get_num(s1, "cnt_freq") : 0;

    char mermaid[2048] = {0};
    if (total > 0) {
        snprintf(mermaid,
                 sizeof(mermaid),
                 "```mermaid\npie showData\n    title 异常类型分布\n    \"金额异常 3σ\" : %d\n    "
                 "\"重复扣款\" : "
                 "%d\n    \"凌晨大额\" : %d\n    \"高频小额\" : %d\n```\n\n",
                 c3,
                 cd,
                 cm,
                 cf);
    } else {
        snprintf(
            mermaid, sizeof(mermaid), "> ✅ **未发现异常**：近 %s 以来交易表现正常。\n\n", since);
    }

    char rows[8192] = {0};
    for (size_t i = 0; i < an && i < 20; i++) {
        const csilk_json_t* it = csilk_json_array_get(anomalies, i);
        const char*         tp = csilk_json_get_string(it, "type") ?: "-";
        const char*         badge =
            strcmp(tp, "3sigma") == 0
                ? "🔴 金额异常"
                : (strcmp(tp, "duplicate") == 0
                       ? "🟡 重复扣款"
                       : (strcmp(tp, "midnight") == 0 ? "🟣 凌晨大额" : "🔵 高频小额"));
        const char* cname = csilk_json_get_string(it, "category_name") ?: "-";
        double      amt = db_get_num(it, "amount");
        const char* date = csilk_json_get_string(it, "date") ?: "-";
        const char* reason = csilk_json_get_string(it, "reason") ?: "-";
        char        line[512];
        snprintf(line,
                 sizeof(line),
                 "| %s | %s | ￥%.0f | %s | %s |\n",
                 badge,
                 cname,
                 amt,
                 date,
                 reason);
        strncat(rows, line, sizeof(rows) - strlen(rows) - 1);
    }
    if (!rows[0]) {
        snprintf(rows, sizeof(rows), "| — | — | — | — | — |\n");
    }

    const char* level =
        total == 0 ? "🟢 **安全**" : (total >= 5 ? "🔴 **需重点核查**" : "🟡 **轻度关注**");

    char buf[16384];
    snprintf(buf,
             sizeof(buf),
             "### 🔍 异常交易检测报告（%s 至今）\n\n"
             "**检测区间**：`%s` 至今｜ **检出异常** `%.0f` 项｜ **风险定级**：%s\n\n"
             "**分类型统计**：金额异常 %d｜重复扣款 %d｜凌晨大额 %d｜高频小额 %d\n\n"
             "#### 一、异常清单\n"
             "| 类型 | 分类 | 金额 | 日期 | 原因 |\n"
             "| :--- | :--- | :--- | :--- | :--- |\n"
             "%s\n"
             "#### 二、异常分布\n"
             "%s"
             "#### 三、处理建议\n"
             "1. **金额异常**：核对发票/小票，确认是否为一次性大额消费误分类；\n"
             "2. **重复扣款**：联系商户/银行核实是否重复扣款，及时申诉；\n"
             "3. **凌晨大额**：确认是否为本人操作，非本人请立即冻结相关账户；\n"
             "4. **高频小额**：多为订阅/自动扣费累积，建议进入订阅审计工作流进一步梳理。\n",
             since,
             since,
             total,
             level,
             c3,
             cd,
             cm,
             cf,
             rows,
             mermaid);

    if (root) {
        csilk_json_free(root);
    }
    return strdup(buf);
}

static const ai_workflow_graph_t g_ad_graph = {
    .id = "wf_anomaly_detect",
    .title = "异常交易与重复扣款检测",
    .description =
        "结合 3σ 偏离、同日同额重复扣款与凌晨异动等多维特征，智能发现可疑流水并给出核查清单。",
    .icon = "ph:detective",
    .node_count = 3,
    .nodes =
        {
                {"ad_collect",
             "近期流水与统计基线提取",
             "提取近 60-180 天支出记录并计算分类均值方差",
             step_ad_collect},
                {"ad_score",
             "多维异常特征检测与打分",
             "扫描 3σ 偏离、重复扣款、凌晨异动",
             step_ad_score},
                {"generate_report",
             "异常清单与排查建议",
             "输出可疑交易列表与分类核查指引",
             step_ad_report},
                },
};

const ai_workflow_graph_t*
ai_workflow_anomaly_detect_get_graph(void)
{
    return &g_ad_graph;
}

/* ========================================================================= */
/*  5. Subscription Audit (wf_subscription_audit)                           */
/* ========================================================================= */

static char*
step_sa_collect(csilk_db_pool_t*    pool,
                int64_t             user_id,
                const csilk_json_t* params,
                const char*         ctx_json)
{
    (void)ctx_json;
    int lookback_days = (int)db_get_num(params, "lookback_days");
    if (lookback_days <= 0) {
        lookback_days = 180;
    }
    if (lookback_days > 365) {
        lookback_days = 365;
    }
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    tm_buf.tm_mday -= lookback_days;
    mktime(&tm_buf);
    char since_date[32];
    snprintf(since_date,
             sizeof(since_date),
             "%04d-%02d-%02d",
             tm_buf.tm_year + 1900,
             tm_buf.tm_mon + 1,
             tm_buf.tm_mday);

    csilk_json_t* recent = csilk_json_array();
    double        total_expense = 0.0;
    int64_t       cnt = 0;
    char          uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char*   p[] = {uid_str, since_date, NULL};
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT de.id, de.amount, de.expense_date, de.note, "
        "COALESCE(c.name,'未分类') as category_name, de.category_id "
        "FROM daily_expenses de LEFT JOIN categories c ON c.id=de.category_id "
        "WHERE de.user_id=? AND de.expense_type='expense' AND de.expense_date >= ? "
        "ORDER BY de.expense_date DESC, de.id DESC LIMIT 800",
        p);
    if (res && csilk_json_is_array(res)) {
        size_t n = csilk_json_array_size(res);
        for (size_t i = 0; i < n; i++) {
            csilk_json_array_append(recent, csilk_json_copy(csilk_json_array_get(res, i)));
            total_expense += db_get_num(csilk_json_array_get(res, i), "amount");
            cnt++;
        }
    }
    if (res) {
        csilk_json_free(res);
    }

    csilk_json_t* grouped = csilk_json_array();
    csilk_json_t* res_grp = csilk_db_query_param_json(
        pool,
        "SELECT category_id, COALESCE(c.name,'未分类') as category_name, "
        "amount, COUNT(*) as cnt, MIN(expense_date) as first_date, MAX(expense_date) as "
        "last_date, GROUP_CONCAT(note, ' | ') as notes "
        "FROM daily_expenses de LEFT JOIN categories c ON c.id=de.category_id "
        "WHERE de.user_id=? AND de.expense_type='expense' AND de.expense_date >= ? "
        "GROUP BY category_id, amount HAVING cnt >= 2 ORDER BY cnt DESC LIMIT 50",
        p);
    if (res_grp && csilk_json_is_array(res_grp)) {
        size_t n = csilk_json_array_size(res_grp);
        for (size_t i = 0; i < n; i++) {
            csilk_json_array_append(grouped, csilk_json_copy(csilk_json_array_get(res_grp, i)));
        }
    }
    if (res_grp) {
        csilk_json_free(res_grp);
    }

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_string(out, "since_date", since_date);
    csilk_json_add_number(out, "lookback_days", (double)lookback_days);
    csilk_json_add_number(out, "total_expense", total_expense);
    csilk_json_add_number(out, "count", (double)cnt);
    csilk_json_add_array(out, "recent", recent);
    csilk_json_add_array(out, "grouped", grouped);
    size_t len = 0;
    char*  s = csilk_json_serialize(out, &len);
    csilk_json_free(out);
    return s;
}

static char*
step_sa_analyze(csilk_db_pool_t*    pool,
                int64_t             user_id,
                const csilk_json_t* params,
                const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "sa_collect") : NULL;
    csilk_json_t* recent = s0 ? csilk_json_get(s0, "recent") : NULL;
    csilk_json_t* grouped = s0 ? csilk_json_get(s0, "grouped") : NULL;
    const char*   since = s0 ? csilk_json_get_string(s0, "since_date") : "";
    if (!since) {
        since = "";
    }

    csilk_json_t* subs = csilk_json_array();
    double        total_monthly = 0.0;
    double        stale_monthly = 0.0;
    double        hiked_extra_monthly = 0.0;
    int           cnt_hiked = 0, cnt_stale = 0;

    time_t    now = time(NULL);
    struct tm now_tm;
    localtime_r(&now, &now_tm);
    char now_str[32];
    snprintf(now_str,
             sizeof(now_str),
             "%04d-%02d-%02d",
             now_tm.tm_year + 1900,
             now_tm.tm_mon + 1,
             now_tm.tm_mday);

    size_t gn = (grouped && csilk_json_is_array(grouped)) ? csilk_json_array_size(grouped) : 0;
    for (size_t i = 0; i < gn; i++) {
        const csilk_json_t* g = csilk_json_array_get(grouped, i);
        int64_t             cid = db_get_int(g, "category_id");
        const char*         cname = csilk_json_get_string(g, "category_name") ?: "未分类";
        double              amt = db_get_num(g, "amount");
        int                 cnt = (int)db_get_num(g, "cnt");
        const char*         first_date = csilk_json_get_string(g, "first_date") ?: "";
        const char*         last_date = csilk_json_get_string(g, "last_date") ?: "";
        const char*         notes = csilk_json_get_string(g, "notes") ?: "";

        if (cnt < 3 && !(cnt == 2 && amt >= 50)) {
            continue;
        }

        int    distinct_months = 0;
        char   months_seen[12][8] = {0};
        size_t rn = (recent && csilk_json_is_array(recent)) ? csilk_json_array_size(recent) : 0;
        for (size_t r = 0; r < rn; r++) {
            const csilk_json_t* row = csilk_json_array_get(recent, r);
            if (db_get_int(row, "category_id") != cid) {
                continue;
            }
            if (fabs(db_get_num(row, "amount") - amt) > 0.01) {
                continue;
            }
            const char* d = csilk_json_get_string(row, "expense_date") ?: "";
            char        ym[8] = {0};
            strncpy(ym, d, 7);
            int found = 0;
            for (int k = 0; k < distinct_months; k++) {
                if (strcmp(months_seen[k], ym) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found && distinct_months < 12) {
                strncpy(months_seen[distinct_months], ym, sizeof(months_seen[0]) - 1);
                distinct_months++;
            }
        }
        if (distinct_months < 2) {
            continue;
        }

        char        display_name[128] = {0};
        const char* sep = strstr(notes, " | ");
        size_t      nlen = sep ? (size_t)(sep - notes) : strlen(notes);
        if (nlen > 0 && nlen < sizeof(display_name)) {
            strncpy(display_name, notes, nlen);
            display_name[nlen] = '\0';
        }
        if (!display_name[0]) {
            snprintf(display_name, sizeof(display_name), "%s", cname);
        }
        for (size_t k = strlen(display_name); k > 0 && display_name[k - 1] == ' '; k--) {
            display_name[k - 1] = '\0';
        }

        int is_stale = 0;
        int y1, m1, d1, y2, m2, d2;
        if (sscanf(last_date, "%d-%d-%d", &y1, &m1, &d1) == 3 &&
            sscanf(now_str, "%d-%d-%d", &y2, &m2, &d2) == 3) {
            struct tm ta = {0}, tb = {0};
            ta.tm_year = y1 - 1900;
            ta.tm_mon = m1 - 1;
            ta.tm_mday = d1;
            tb.tm_year = y2 - 1900;
            tb.tm_mon = m2 - 1;
            tb.tm_mday = d2;
            time_t t1 = mktime(&ta), t2 = mktime(&tb);
            double diff = difftime(t2, t1) / 86400.0;
            if (diff > 45) {
                is_stale = 1;
            }
        }

        int    is_hiked = 0;
        double alt_amt = 0;
        if (recent && csilk_json_is_array(recent)) {
            for (size_t r = 0; r < rn; r++) {
                const csilk_json_t* row = csilk_json_array_get(recent, r);
                if (db_get_int(row, "category_id") != cid) {
                    continue;
                }
                double a = db_get_num(row, "amount");
                if (fabs(a - amt) > 0.01 && fabs(a - amt) / amt < 0.30 && a > amt) {
                    alt_amt = a;
                }
            }
            if (alt_amt > amt * 1.10) {
                is_hiked = 1;
                hiked_extra_monthly += (alt_amt - amt);
                cnt_hiked++;
            }
        }

        double monthly = amt;
        double annual = monthly * 12.0;
        total_monthly += monthly;
        if (is_stale) {
            stale_monthly += monthly;
            cnt_stale++;
        }

        csilk_json_t* o = csilk_json_object();
        csilk_json_add_string(o, "name", display_name);
        csilk_json_add_string(o, "category_name", cname);
        csilk_json_add_number(o, "category_id", (double)cid);
        csilk_json_add_number(o, "amount", amt);
        csilk_json_add_number(o, "alt_amount", alt_amt);
        csilk_json_add_number(o, "cnt", (double)cnt);
        csilk_json_add_number(o, "distinct_months", (double)distinct_months);
        csilk_json_add_string(o, "first_date", first_date);
        csilk_json_add_string(o, "last_date", last_date);
        csilk_json_add_number(o, "monthly", monthly);
        csilk_json_add_number(o, "annual", annual);
        csilk_json_add_bool(o, "is_stale", is_stale);
        csilk_json_add_bool(o, "is_hiked", is_hiked);
        csilk_json_add_string(o, "notes", notes);
        csilk_json_array_append(subs, o);
    }

    if (root) {
        csilk_json_free(root);
    }
    csilk_json_t* out = csilk_json_object();
    csilk_json_add_string(out, "since_date", since);
    csilk_json_add_number(out, "sub_count", (double)csilk_json_array_size(subs));
    csilk_json_add_number(out, "total_monthly", total_monthly);
    csilk_json_add_number(out, "total_annual", total_monthly * 12.0);
    csilk_json_add_number(out, "stale_monthly", stale_monthly);
    csilk_json_add_number(out, "stale_annual", stale_monthly * 12.0);
    csilk_json_add_number(out, "stale_cnt", (double)cnt_stale);
    csilk_json_add_number(out, "hiked_cnt", (double)cnt_hiked);
    csilk_json_add_number(out, "hiked_extra_monthly", hiked_extra_monthly);
    csilk_json_add_number(out, "hiked_extra_annual", hiked_extra_monthly * 12.0);
    csilk_json_add_array(out, "subs", subs);
    size_t len = 0;
    char*  s = csilk_json_serialize(out, &len);
    csilk_json_free(out);
    return s;
}

static char*
step_sa_report(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "sa_collect") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "sa_analyze") : NULL;
    const char*   since = s0 ? csilk_json_get_string(s0, "since_date") : "";
    if (!since) {
        since = "";
    }
    double        sub_cnt = s1 ? db_get_num(s1, "sub_count") : 0;
    double        total_m = s1 ? db_get_num(s1, "total_monthly") : 0;
    double        total_a = s1 ? db_get_num(s1, "total_annual") : 0;
    double        stale_m = s1 ? db_get_num(s1, "stale_monthly") : 0;
    double        stale_a = s1 ? db_get_num(s1, "stale_annual") : 0;
    double        stale_cnt = s1 ? db_get_num(s1, "stale_cnt") : 0;
    double        hiked_cnt = s1 ? db_get_num(s1, "hiked_cnt") : 0;
    double        hiked_extra_a = s1 ? db_get_num(s1, "hiked_extra_annual") : 0;
    csilk_json_t* subs = s1 ? csilk_json_get(s1, "subs") : NULL;
    size_t        sn = (subs && csilk_json_is_array(subs)) ? csilk_json_array_size(subs) : 0;

    char mermaid[4096] = {0};
    if (sn > 0) {
        snprintf(mermaid,
                 sizeof(mermaid),
                 "```mermaid\npie showData\n    title 订阅/固定支出月度构成\n");
        for (size_t i = 0; i < sn && i < 8; i++) {
            const csilk_json_t* it = csilk_json_array_get(subs, i);
            const char*         nm = csilk_json_get_string(it, "name") ?: csilk_json_get_string(it, "category_name") ?: "未命名";
            double amt = db_get_num(it, "amount");
            char   line[256];
            char   safe_nm[128] = {0};
            strncpy(safe_nm, nm, sizeof(safe_nm) - 1);
            for (char* p = safe_nm; *p; p++) {
                if (*p == '"') {
                    *p = '\'';
                }
            }
            snprintf(line, sizeof(line), "    \"%s\" : %.2f\n", safe_nm, amt);
            strncat(mermaid, line, sizeof(mermaid) - strlen(mermaid) - 1);
        }
        strncat(mermaid, "```\n\n", sizeof(mermaid) - strlen(mermaid) - 1);
    } else {
        snprintf(mermaid,
                 sizeof(mermaid),
                 "> ✅ **未识别到固定订阅**：近 %s 以来未发现规律性重复支出。\n\n",
                 since);
    }

    char rows[8192] = {0};
    for (size_t i = 0; i < sn && i < 15; i++) {
        const csilk_json_t* it = csilk_json_array_get(subs, i);
        const char*         nm = csilk_json_get_string(it, "name") ?: "-";
        const char*         cname = csilk_json_get_string(it, "category_name") ?: "-";
        double              amt = db_get_num(it, "amount");
        double              annual = db_get_num(it, "annual");
        int                 cnt = (int)db_get_num(it, "cnt");
        int                 dmonths = (int)db_get_num(it, "distinct_months");
        const char*         last = csilk_json_get_string(it, "last_date") ?: "-";
        int                 stale = csilk_json_get_bool(it, "is_stale");
        int                 hiked = csilk_json_get_bool(it, "is_hiked");
        const char*         badge = stale ? "⚫ 疑似闲置" : (hiked ? "🔴 已涨价" : "🟢 正常扣费");
        char                line[512];
        char                safe_nm[96] = {0};
        strncpy(safe_nm, nm, sizeof(safe_nm) - 1);
        for (char* p = safe_nm; *p; p++) {
            if (*p == '|') {
                *p = '/';
            }
        }
        snprintf(line,
                 sizeof(line),
                 "| %s | %s | ￥%.0f | ￥%.0f | %d次/%d月 | %s | %s |\n",
                 safe_nm,
                 cname,
                 amt,
                 annual,
                 cnt,
                 dmonths,
                 last,
                 badge);
        strncat(rows, line, sizeof(rows) - strlen(rows) - 1);
    }
    if (!rows[0]) {
        snprintf(rows, sizeof(rows), "| — | — | — | — | — | — | — |\n");
    }

    char buf[16384];
    snprintf(
        buf,
        sizeof(buf),
        "### 🔁 订阅/固定支出审计报告（%s 至今）\n\n"
        "**识别订阅** `%.0f` 项｜ **月度合计** `￥%.2f` ｜ **年化合计** `￥%.2f`\n\n"
        "**闲置/久未扣费** `%.0f` 项（月省 ￥%.0f / 年省 ￥%.0f）｜ **疑似涨价** `%.0f` "
        "项（年多付约 ￥%.0f）\n\n"
        "#### 一、订阅清单明细\n"
        "| 订阅/固定项 | 分类 | 月费 | 年化 | 频次 | 最近扣费 | 状态 |\n"
        "| :--- | :--- | :--- | :--- | :--- | :--- | :--- |\n"
        "%s\n"
        "#### 二、订阅构成占比\n"
        "%s"
        "#### 三、省钱建议与操作草案\n"
        "1. **闲置订阅**：对 ⚫ 标记项若近 45 天未扣费且不再使用，建议取消，年化可省 `￥%.0f`；\n"
        "2. **涨价订阅**：对 🔴 标记项核对账单，若为无感知涨价可考虑降档或换替代方案；\n"
        "3. **年度视角**：全部订阅年化 `￥%.0f`，占月均支出约 "
        "`%.0f%%`，建议将高频低感知订阅合并或年付优惠；\n"
        "4. "
        "**执行草案**"
        "：确认取消后可在「日常记账」中标记对应分类为预算管控重点，下月复盘验证是否生效。\n"
        "```action\n"
        "{\n"
        "  \"action_type\": \"subscription_audit\",\n"
        "  \"since_date\": \"%s\",\n"
        "  \"sub_count\": %.0f,\n"
        "  \"total_monthly\": %.2f,\n"
        "  \"total_annual\": %.2f,\n"
        "  \"stale_annual_saving\": %.2f\n"
        "}\n"
        "```\n",
        since,
        sub_cnt,
        total_m,
        total_a,
        stale_cnt,
        stale_m,
        stale_a,
        hiked_cnt,
        hiked_extra_a,
        rows,
        mermaid,
        stale_a,
        total_a,
        total_a > 0 ? (total_m / (total_m + 1) * 100) : 0,
        since,
        sub_cnt,
        total_m,
        total_a,
        stale_a);

    if (root) {
        csilk_json_free(root);
    }
    return strdup(buf);
}

static const ai_workflow_graph_t g_sa_graph = {
    .id = "wf_subscription_audit",
    .title = "周期订阅与固定开销审计",
    .description =
        "自动挖掘连续扣费项目与流媒体/SaaS订阅，核查静默涨价与闲置扣费，输出退订节流方案。",
    .icon = "ph:repeat",
    .node_count = 3,
    .nodes =
        {
                {"sa_collect",
             "周期性重复扣费扫描",
             "汇总近 180-365 天内按周期重复出现的消费",
             step_sa_collect},
                {"sa_analyze",
             "闲置与涨价特征识别",
             "比对扣费频次、近 45 天活跃度与单价变动",
             step_sa_analyze},
                {"generate_report",
             "订阅大屏与退订建议",
             "生成订阅年化成本图与退订节流方案",
             step_sa_report},
                },
};

const ai_workflow_graph_t*
ai_workflow_subscription_audit_get_graph(void)
{
    return &g_sa_graph;
}

/* ========================================================================= */
/*  6. Goal Tracker (wf_goal_tracker)                                       */
/* ========================================================================= */

static char*
step_gt_collect(csilk_db_pool_t*    pool,
                int64_t             user_id,
                const csilk_json_t* params,
                const char*         ctx_json)
{
    (void)params;
    (void)ctx_json;
    if (!pool) {
        return strdup("{\"error\":\"db not ready\"}");
    }
    csilk_db_exec(pool,
                  "CREATE TABLE IF NOT EXISTS savings_goals ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "user_id INTEGER NOT NULL, "
                  "name TEXT NOT NULL, "
                  "target_amount REAL NOT NULL, "
                  "current_amount REAL NOT NULL DEFAULT 0, "
                  "deadline TEXT, "
                  "note TEXT, "
                  "created_at TEXT DEFAULT (strftime('%Y-%m-%d %H:%M:%S','now','localtime'))"
                  ")");

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char*   p[] = {uid_str, NULL};
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "SELECT id, name, target_amount, current_amount, deadline, note "
                                  "FROM savings_goals WHERE user_id=? "
                                  "ORDER BY (deadline IS NULL), deadline ASC, id ASC",
                                  p);

    csilk_json_t* goals = csilk_json_array();
    double        total_target = 0, total_current = 0;
    int           overdue_cnt = 0, completed_cnt = 0;

    time_t    now = time(NULL);
    struct tm now_tm;
    localtime_r(&now, &now_tm);
    char today_str[32];
    snprintf(today_str,
             sizeof(today_str),
             "%04d-%02d-%02d",
             now_tm.tm_year + 1900,
             now_tm.tm_mon + 1,
             now_tm.tm_mday);

    if (res && csilk_json_is_array(res)) {
        size_t n = csilk_json_array_size(res);
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* r = csilk_json_array_get(res, i);
            double              tgt = db_get_num(r, "target_amount");
            double              cur = db_get_num(r, "current_amount");
            if (tgt < 0) {
                tgt = 0;
            }
            if (cur < 0) {
                cur = 0;
            }
            double prog = tgt > 0 ? cur / tgt * 100.0 : 0;
            if (prog > 100) {
                prog = 100;
            }
            const char* deadline = csilk_json_get_string(r, "deadline") ?: "";
            int         is_overdue = 0, is_completed = prog >= 100 ? 1 : 0;
            int         days_left = -1;
            if (deadline[0] && !is_completed) {
                int y, mo, d;
                if (sscanf(deadline, "%d-%d-%d", &y, &mo, &d) == 3) {
                    struct tm dl = {0};
                    dl.tm_year = y - 1900;
                    dl.tm_mon = mo - 1;
                    dl.tm_mday = d;
                    time_t    tdl = mktime(&dl);
                    struct tm td = {0};
                    td.tm_year = now_tm.tm_year;
                    td.tm_mon = now_tm.tm_mon;
                    td.tm_mday = now_tm.tm_mday;
                    time_t tnow = mktime(&td);
                    double diff = difftime(tdl, tnow) / 86400.0;
                    days_left = (int)diff;
                    if (diff < 0) {
                        is_overdue = 1;
                    }
                }
            }
            if (is_overdue) {
                overdue_cnt++;
            }
            if (is_completed) {
                completed_cnt++;
            }
            total_target += tgt;
            total_current += cur;

            csilk_json_t* o = csilk_json_object();
            csilk_json_add_number(o, "id", (double)db_get_int(r, "id"));
            csilk_json_add_string(o, "name", csilk_json_get_string(r, "name") ?: "未命名目标");
            csilk_json_add_number(o, "target_amount", tgt);
            csilk_json_add_number(o, "current_amount", cur);
            csilk_json_add_number(o, "progress", prog);
            csilk_json_add_number(o, "remaining", tgt > cur ? tgt - cur : 0);
            csilk_json_add_string(o, "deadline", deadline);
            csilk_json_add_number(o, "days_left", (double)days_left);
            csilk_json_add_bool(o, "is_overdue", is_overdue);
            csilk_json_add_bool(o, "is_completed", is_completed);
            csilk_json_add_string(o, "note", csilk_json_get_string(r, "note") ?: "");
            csilk_json_array_append(goals, o);
        }
    }
    if (res) {
        csilk_json_free(res);
    }

    double total_remaining = total_target > total_current ? total_target - total_current : 0;
    double avg_progress = total_target > 0 ? total_current / total_target * 100.0 : 0;
    if (avg_progress > 100) {
        avg_progress = 100;
    }

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "goal_count", (double)csilk_json_array_size(goals));
    csilk_json_add_number(out, "total_target", total_target);
    csilk_json_add_number(out, "total_current", total_current);
    csilk_json_add_number(out, "total_remaining", total_remaining);
    csilk_json_add_number(out, "avg_progress", avg_progress);
    csilk_json_add_number(out, "overdue_cnt", (double)overdue_cnt);
    csilk_json_add_number(out, "completed_cnt", (double)completed_cnt);
    csilk_json_add_string(out, "today", today_str);
    csilk_json_add_array(out, "goals", goals);
    size_t slen = 0;
    char*  s = csilk_json_serialize(out, &slen);
    csilk_json_free(out);
    char* ret = s ? strdup(s) : strdup("{}");
    free(s);
    return ret;
}

static char*
step_gt_plan(csilk_db_pool_t*    pool,
             int64_t             user_id,
             const csilk_json_t* params,
             const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "gt_collect") : NULL;
    csilk_json_t* goals = s0 ? csilk_json_get(s0, "goals") : NULL;
    size_t        n = (goals && csilk_json_is_array(goals)) ? csilk_json_array_size(goals) : 0;

    csilk_json_t* plans = csilk_json_array();
    double        total_monthly_needed = 0;
    int           urgent_cnt = 0;

    for (size_t i = 0; i < n; i++) {
        const csilk_json_t* g = csilk_json_array_get(goals, i);
        double              tgt = db_get_num(g, "target_amount");
        double              cur = db_get_num(g, "current_amount");
        double              remaining = tgt > cur ? tgt - cur : 0;
        int                 is_completed = csilk_json_get_bool(g, "is_completed");
        const char*         deadline = csilk_json_get_string(g, "deadline") ?: "";
        int                 days_left = (int)db_get_num(g, "days_left");

        double months_left = 12;
        if (deadline[0] && days_left >= 0) {
            months_left = days_left / 30.0;
            if (months_left < 1) {
                months_left = 1;
            }
        } else if (deadline[0] && days_left < 0 && !is_completed) {
            months_left = 1;
        }
        double monthly_needed = 0;
        if (!is_completed && remaining > 0) {
            monthly_needed = remaining / months_left;
        }
        if (!is_completed) {
            total_monthly_needed += monthly_needed;
            if ((days_left >= 0 && days_left <= 60 && remaining > 0) ||
                (days_left < 0 && remaining > 0)) {
                urgent_cnt++;
            }
        }

        csilk_json_t* o = csilk_json_object();
        csilk_json_add_number(o, "id", db_get_num(g, "id"));
        csilk_json_add_string(o, "name", csilk_json_get_string(g, "name") ?: "未命名");
        csilk_json_add_number(o, "remaining", remaining);
        csilk_json_add_number(o, "months_left", months_left);
        csilk_json_add_number(o, "monthly_needed", monthly_needed);
        csilk_json_add_number(o, "progress", db_get_num(g, "progress"));
        csilk_json_add_bool(o, "is_completed", is_completed);
        csilk_json_add_string(o, "deadline", deadline);
        csilk_json_add_number(o, "days_left", (double)days_left);
        csilk_json_array_append(plans, o);
    }

    if (root) {
        csilk_json_free(root);
    }
    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "goal_count", (double)n);
    csilk_json_add_number(out, "total_monthly_needed", total_monthly_needed);
    csilk_json_add_number(out, "urgent_cnt", (double)urgent_cnt);
    csilk_json_add_array(out, "plans", plans);
    size_t slen = 0;
    char*  s = csilk_json_serialize(out, &slen);
    csilk_json_free(out);
    char* ret = s ? strdup(s) : strdup("{}");
    free(s);
    return ret;
}

static char*
step_gt_report(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "gt_collect") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "gt_plan") : NULL;
    double        goal_cnt = s0 ? db_get_num(s0, "goal_count") : 0;
    double        total_tgt = s0 ? db_get_num(s0, "total_target") : 0;
    double        total_cur = s0 ? db_get_num(s0, "total_current") : 0;
    double        total_rem = s0 ? db_get_num(s0, "total_remaining") : 0;
    double        avg_prog = s0 ? db_get_num(s0, "avg_progress") : 0;
    double        overdue = s0 ? db_get_num(s0, "overdue_cnt") : 0;
    double        completed = s0 ? db_get_num(s0, "completed_cnt") : 0;
    double        total_need = s1 ? db_get_num(s1, "total_monthly_needed") : 0;
    csilk_json_t* goals = s0 ? csilk_json_get(s0, "goals") : NULL;
    csilk_json_t* plans = s1 ? csilk_json_get(s1, "plans") : NULL;
    size_t        n = (goals && csilk_json_is_array(goals)) ? csilk_json_array_size(goals) : 0;

    char mermaid[4096] = {0};
    if (n == 0) {
        snprintf(mermaid,
                 sizeof(mermaid),
                 "> ℹ️ 暂无储蓄目标，前往「资产」或调用 `POST /api/goals` 创建首个目标后重试。\n\n");
    } else if (total_tgt > 0) {
        double filled = total_cur > total_tgt ? total_tgt : total_cur;
        double missing = total_rem;
        snprintf(mermaid,
                 sizeof(mermaid),
                 "```mermaid\npie showData\n    title 目标储蓄总进度 %.0f%%\n"
                 "    \"已存入\" : %.2f\n"
                 "    \"待存入\" : %.2f\n```\n\n",
                 avg_prog,
                 filled,
                 missing);
        char bar[2048] = {0};
        strncat(mermaid,
                "```mermaid\nxychart-beta\n    title \"各目标达成率 %%\"\n    x-axis [",
                sizeof(mermaid) - strlen(mermaid) - 1);
        for (size_t i = 0; i < n && i < 6; i++) {
            const csilk_json_t* g = csilk_json_array_get(goals, i);
            const char*         nm = csilk_json_get_string(g, "name") ?: "目标";
            char                safe[64] = {0};
            strncpy(safe, nm, sizeof(safe) - 1);
            for (char* p = safe; *p; p++) {
                if (*p == '"' || *p == '\'') {
                    *p = ' ';
                }
            }
            char seg[128];
            snprintf(seg, sizeof(seg), "\"%s\"%s", safe, (i + 1 < n && i + 1 < 6) ? ", " : "");
            strncat(bar, seg, sizeof(bar) - strlen(bar) - 1);
        }
        strncat(mermaid, bar, sizeof(mermaid) - strlen(mermaid) - 1);
        strncat(mermaid,
                "]\n    y-axis \"进度 %%\" 0 --> 100\n    bar [",
                sizeof(mermaid) - strlen(mermaid) - 1);
        char vals[512] = {0};
        for (size_t i = 0; i < n && i < 6; i++) {
            char seg[64];
            snprintf(seg,
                     sizeof(seg),
                     "%.0f%s",
                     db_get_num(csilk_json_array_get(goals, i), "progress"),
                     (i + 1 < n && i + 1 < 6) ? ", " : "");
            strncat(vals, seg, sizeof(vals) - strlen(vals) - 1);
        }
        strncat(mermaid, vals, sizeof(mermaid) - strlen(mermaid) - 1);
        strncat(mermaid, "]\n```\n\n", sizeof(mermaid) - strlen(mermaid) - 1);
    }

    char rows[8192] = {0};
    for (size_t i = 0; i < n && i < 20; i++) {
        const csilk_json_t* g = csilk_json_array_get(goals, i);
        const char*         nm = csilk_json_get_string(g, "name") ?: "-";
        double              tgt = db_get_num(g, "target_amount");
        double              cur = db_get_num(g, "current_amount");
        double              prog = db_get_num(g, "progress");
        const char*         dl = csilk_json_get_string(g, "deadline") ?: "—";
        if (!dl[0]) {
            dl = "—";
        }
        int    days_left = (int)db_get_num(g, "days_left");
        int    is_completed = csilk_json_get_bool(g, "is_completed");
        int    is_overdue = csilk_json_get_bool(g, "is_overdue");
        double monthly_needed = 0;
        if (plans && csilk_json_is_array(plans) && i < csilk_json_array_size(plans)) {
            monthly_needed = db_get_num(csilk_json_array_get(plans, i), "monthly_needed");
        }
        const char* badge =
            is_completed
                ? "✅ 已达成"
                : (is_overdue ? "🔴 已逾期"
                              : (days_left >= 0 && days_left <= 30 ? "🟡 紧迫" : "🟢 进行中"));
        char bar_txt[64] = {0};
        int  filled = (int)(prog / 10);
        if (filled > 10) {
            filled = 10;
        }
        for (int k = 0; k < filled; k++) {
            bar_txt[k] = '#';
        }
        for (int k = filled; k < 10; k++) {
            bar_txt[k] = '-';
        }
        bar_txt[10] = '\0';
        char safe_nm[80] = {0};
        strncpy(safe_nm, nm, sizeof(safe_nm) - 1);
        for (char* p = safe_nm; *p; p++) {
            if (*p == '|') {
                *p = '/';
            }
        }
        char line[512];
        snprintf(line,
                 sizeof(line),
                 "| %s | ￥%.0f | ￥%.0f | %.0f%% %s | %s | ￥%.0f/月 | %s |\n",
                 safe_nm,
                 tgt,
                 cur,
                 prog,
                 bar_txt,
                 dl,
                 monthly_needed,
                 badge);
        strncat(rows, line, sizeof(rows) - strlen(rows) - 1);
    }
    if (!rows[0]) {
        snprintf(rows, sizeof(rows), "| — | — | — | — | — | — | — |\n");
    }

    char buf[16384];
    snprintf(
        buf,
        sizeof(buf),
        "### 🎯 目标储蓄追踪报告\n\n"
        "**目标数** `%.0f` ｜ **总目标** `￥%.2f` ｜ **已存** `￥%.2f` ｜ **待存** `￥%.2f` ｜ "
        "**平均进度** `%.0f%%`\n\n"
        "**已达成** `%.0f` 项 ｜ **已逾期** `%.0f` 项 ｜ **月需追加合计** `￥%.0f/月`\n\n"
        "#### 一、总进度构成\n"
        "%s"
        "#### 二、目标明细与追踪\n"
        "| 目标 | 目标金额 | 已存 | 进度 | 截止日 | 月需追加 | 状态 |\n"
        "| :--- | :--- | :--- | :--- | :--- | :--- | :--- |\n"
        "%s\n"
        "#### 三、行动建议\n"
        "1. **逾期目标**：🔴 标记项已过截止日，建议优先追加或调整截止日；\n"
        "2. **紧迫目标（≤30 天）**：🟡 标记项需加速，建议当月优先保障；\n"
        "3. **月供压力**：合计月需 `￥%.0f`，若超过月结余 50%% 建议延长部分目标期限；\n"
        "4. **已达成**：✅ 标记项可转为应急基金或投资账户。\n"
        "```action\n"
        "{\n"
        "  \"action_type\": \"goal_tracker\",\n"
        "  \"goal_count\": %.0f,\n"
        "  \"total_target\": %.2f,\n"
        "  \"total_remaining\": %.2f,\n"
        "  \"total_monthly_needed\": %.2f\n"
        "}\n"
        "```\n",
        goal_cnt,
        total_tgt,
        total_cur,
        total_rem,
        avg_prog,
        completed,
        overdue,
        total_need,
        mermaid,
        rows,
        total_need,
        goal_cnt,
        total_tgt,
        total_rem,
        total_need);

    if (root) {
        csilk_json_free(root);
    }
    return strdup(buf);
}

static const ai_workflow_graph_t g_gt_graph = {
    .id = "wf_goal_tracker",
    .title = "储蓄目标达成率与进度追踪",
    .description = "管理中长期储蓄目标与心愿单，测算月供缺口并按截止日倒排进度生成储蓄规划。",
    .icon = "ph:target",
    .node_count = 3,
    .nodes =
        {
                {"gt_collect",
             "目标盘点与全量拉取",
             "拉取所有储蓄目标、已存入资金及截止期限",
             step_gt_collect},
                {"gt_plan", "进度测算与月供规划", "倒排截止日并测算各目标每月需存入金额", step_gt_plan},
                {"generate_report",
             "目标达成大屏与行动方案",
             "生成目标达成度柱状图与行动计划表",
             step_gt_report},
                },
};

const ai_workflow_graph_t*
ai_workflow_goal_tracker_get_graph(void)
{
    return &g_gt_graph;
}

/* ========================================================================= */
/*  7. Debt Payoff (wf_debt_payoff)                                         */
/* ========================================================================= */

static double
dp_rate_from_note(const char* note, const char* asset_type)
{
    double def = 6.0;
    if (asset_type) {
        if (strcmp(asset_type, "credit_card") == 0) {
            def = 18.0;
        } else if (strcmp(asset_type, "loan") == 0) {
            def = 4.9;
        } else if (strcmp(asset_type, "other_liability") == 0) {
            def = 6.0;
        }
    }
    if (!note || !note[0]) {
        return def;
    }
    const char* pct = strchr(note, '%');
    if (!pct) {
        return def;
    }
    const char* p = pct - 1;
    while (p > note && (*p == ' ' || *p == '\t')) {
        p--;
    }
    const char* num_end = p + 1;
    while (p >= note && ((*p >= '0' && *p <= '9') || *p == '.')) {
        p--;
    }
    const char* num_start = p + 1;
    if (num_start < num_end) {
        char   buf[32] = {0};
        size_t len = (size_t)(num_end - num_start);
        if (len < sizeof(buf)) {
            memcpy(buf, num_start, len);
            buf[len] = '\0';
            double v = atof(buf);
            if (v > 0 && v < 100) {
                return v;
            }
        }
    }
    return def;
}

static char*
step_dp_collect(csilk_db_pool_t*    pool,
                int64_t             user_id,
                const csilk_json_t* params,
                const char*         ctx_json)
{
    (void)params;
    (void)ctx_json;
    int64_t       tot = 0;
    csilk_json_t* list = asset_list(pool, user_id, 1, 200, NULL, &tot);
    csilk_json_t* debts = csilk_json_array();
    double        total_debt = 0;
    double        max_rate = 0;
    double        min_bal = 1e18;
    if (list) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* a = csilk_json_array_get(list, i);
            const char*   t = csilk_json_get_string(a, "asset_type") ?: "";
            if (strcmp(t, "loan") != 0 && strcmp(t, "credit_card") != 0 &&
                strcmp(t, "other_liability") != 0) {
                continue;
            }
            double bal = db_get_num(a, "current_value");
            if (bal == 0.0) {
                bal = db_get_num(a, "balance");
            }
            if (bal <= 0.0) {
                continue;
            }
            const char*   note = csilk_json_get_string(a, "note") ?: "";
            double        rate = dp_rate_from_note(note, t);
            int64_t       aid = db_get_int(a, "id");
            const char*   nm = csilk_json_get_string(a, "name") ?: "未命名负债";
            csilk_json_t* o = csilk_json_object();
            csilk_json_add_number(o, "id", (double)aid);
            csilk_json_add_string(o, "name", nm);
            csilk_json_add_string(o, "asset_type", t);
            csilk_json_add_number(o, "balance", bal);
            csilk_json_add_number(o, "annual_rate", rate);
            csilk_json_add_number(o, "monthly_rate", rate / 12.0 / 100.0);
            csilk_json_add_string(o, "note", note);
            csilk_json_array_append(debts, o);
            total_debt += bal;
            if (rate > max_rate) {
                max_rate = rate;
            }
            if (bal < min_bal) {
                min_bal = bal;
            }
        }
        csilk_json_free(list);
    }
    if (min_bal > 1e17) {
        min_bal = 0;
    }
    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "debt_count", (double)csilk_json_array_size(debts));
    csilk_json_add_number(out, "total_debt", total_debt);
    csilk_json_add_number(out, "max_rate", max_rate);
    csilk_json_add_number(out, "min_balance", min_bal);
    csilk_json_add_array(out, "debts", debts);
    size_t len = 0;
    char*  s = csilk_json_serialize(out, &len);
    csilk_json_free(out);
    char* ret = s ? strdup(s) : strdup("{}");
    free(s);
    return ret;
}

typedef struct {
    double balance;
    double annual_rate;
    double monthly_rate;
    char   name[128];
    char   type[32];
} dp_item_t;

static int
cmp_avalanche(const void* a, const void* b)
{
    const dp_item_t* x = (const dp_item_t*)a;
    const dp_item_t* y = (const dp_item_t*)b;
    if (y->annual_rate > x->annual_rate) {
        return 1;
    }
    if (y->annual_rate < x->annual_rate) {
        return -1;
    }
    return 0;
}

static int
cmp_snowball(const void* a, const void* b)
{
    const dp_item_t* x = (const dp_item_t*)a;
    const dp_item_t* y = (const dp_item_t*)b;
    if (x->balance < y->balance) {
        return -1;
    }
    if (x->balance > y->balance) {
        return 1;
    }
    return 0;
}

static double
dp_simulate(dp_item_t* items, size_t n, double monthly_payment, int months, double* out_interest)
{
    double total_interest = 0;
    for (int m = 0; m < months; m++) {
        for (size_t i = 0; i < n; i++) {
            if (items[i].balance <= 0.01) {
                continue;
            }
            double interest = items[i].balance * items[i].monthly_rate;
            items[i].balance += interest;
            total_interest += interest;
        }
        double remaining = monthly_payment;
        for (size_t i = 0; i < n; i++) {
            if (items[i].balance <= 0.01) {
                continue;
            }
            double min_pay = items[i].balance * 0.02;
            if (min_pay < 100) {
                min_pay = 100;
            }
            if (min_pay > items[i].balance) {
                min_pay = items[i].balance;
            }
            double take = remaining >= min_pay ? min_pay : remaining;
            if (take > items[i].balance) {
                take = items[i].balance;
            }
            items[i].balance -= take;
            remaining -= take;
            if (remaining <= 0.01) {
                break;
            }
        }
        for (size_t i = 0; i < n && remaining > 0.01; i++) {
            if (items[i].balance <= 0.01) {
                continue;
            }
            double take = remaining > items[i].balance ? items[i].balance : remaining;
            items[i].balance -= take;
            remaining -= take;
        }
        int all_zero = 1;
        for (size_t i = 0; i < n; i++) {
            if (items[i].balance > 0.01) {
                all_zero = 0;
                break;
            }
        }
        if (all_zero) {
            break;
        }
    }
    double remaining_debt = 0;
    for (size_t i = 0; i < n; i++) {
        if (items[i].balance > 0.01) {
            remaining_debt += items[i].balance;
        }
    }
    if (out_interest) {
        *out_interest = total_interest;
    }
    return remaining_debt;
}

static char*
step_dp_simulate(csilk_db_pool_t*    pool,
                 int64_t             user_id,
                 const csilk_json_t* params,
                 const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "dp_collect") : NULL;
    csilk_json_t* debts = s0 ? csilk_json_get(s0, "debts") : NULL;
    size_t        n = (debts && csilk_json_is_array(debts)) ? csilk_json_array_size(debts) : 0;

    double monthly_payment = 0;
    if (params) {
        monthly_payment = db_get_num(params, "monthly_payment");
        if (monthly_payment <= 0) {
            monthly_payment = db_get_num(params, "amount");
        }
    }
    if (monthly_payment <= 0) {
        double total_debt = s0 ? db_get_num(s0, "total_debt") : 0;
        monthly_payment = total_debt / 12.0;
        if (monthly_payment < 2000) {
            monthly_payment = 2000;
        }
    }

    dp_item_t* items = NULL;
    if (n > 0) {
        items = (dp_item_t*)calloc(n, sizeof(dp_item_t));
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* d = csilk_json_array_get(debts, i);
            items[i].balance = db_get_num(d, "balance");
            items[i].annual_rate = db_get_num(d, "annual_rate");
            items[i].monthly_rate = items[i].annual_rate / 12.0 / 100.0;
            const char* nm = csilk_json_get_string(d, "name") ?: "";
            const char* tp = csilk_json_get_string(d, "asset_type") ?: "";
            strncpy(items[i].name, nm, sizeof(items[i].name) - 1);
            strncpy(items[i].type, tp, sizeof(items[i].type) - 1);
        }
    }

    double interest_av = 0, interest_sb = 0;
    double remain_av = 0, remain_sb = 0;

    if (n > 0 && items) {
        dp_item_t* av = (dp_item_t*)calloc(n, sizeof(dp_item_t));
        dp_item_t* sb = (dp_item_t*)calloc(n, sizeof(dp_item_t));
        memcpy(av, items, n * sizeof(dp_item_t));
        memcpy(sb, items, n * sizeof(dp_item_t));
        qsort(av, n, sizeof(dp_item_t), cmp_avalanche);
        qsort(sb, n, sizeof(dp_item_t), cmp_snowball);
        remain_av = dp_simulate(av, n, monthly_payment, 12, &interest_av);
        remain_sb = dp_simulate(sb, n, monthly_payment, 12, &interest_sb);
        free(av);
        free(sb);
    }

    double      saved = interest_sb - interest_av;
    const char* recommended = (interest_av <= interest_sb) ? "avalanche" : "snowball";

    if (root) {
        csilk_json_free(root);
    }
    if (items) {
        free(items);
    }

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "debt_count", (double)n);
    csilk_json_add_number(out, "monthly_payment", monthly_payment);
    csilk_json_add_number(out, "avalanche_interest", interest_av);
    csilk_json_add_number(out, "avalanche_remaining", remain_av);
    csilk_json_add_number(out, "snowball_interest", interest_sb);
    csilk_json_add_number(out, "snowball_remaining", remain_sb);
    csilk_json_add_number(out, "interest_saved", saved > 0 ? saved : 0);
    csilk_json_add_string(out, "recommended", recommended);
    size_t len = 0;
    char*  s = csilk_json_serialize(out, &len);
    csilk_json_free(out);
    char* ret = s ? strdup(s) : strdup("{}");
    free(s);
    return ret;
}

static char*
step_dp_report(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "dp_collect") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "dp_simulate") : NULL;
    double        total_debt = s0 ? db_get_num(s0, "total_debt") : 0;
    double        debt_cnt = s0 ? db_get_num(s0, "debt_count") : 0;
    csilk_json_t* debts = s0 ? csilk_json_get(s0, "debts") : NULL;
    size_t        n = (debts && csilk_json_is_array(debts)) ? csilk_json_array_size(debts) : 0;
    double        mp = s1 ? db_get_num(s1, "monthly_payment") : 2000;
    double        av_i = s1 ? db_get_num(s1, "avalanche_interest") : 0;
    double        av_r = s1 ? db_get_num(s1, "avalanche_remaining") : 0;
    double        sb_i = s1 ? db_get_num(s1, "snowball_interest") : 0;
    double        sb_r = s1 ? db_get_num(s1, "snowball_remaining") : 0;
    double        saved = s1 ? db_get_num(s1, "interest_saved") : 0;
    const char*   rec = s1 ? csilk_json_get_string(s1, "recommended") : "avalanche";
    if (!rec) {
        rec = "avalanche";
    }

    char mermaid[4096] = {0};
    if (n == 0) {
        snprintf(mermaid,
                 sizeof(mermaid),
                 "> ✅ **无负债**：当前未检测到贷款/信用卡类负债，无需偿还规划。\n\n");
    } else {
        snprintf(mermaid,
                 sizeof(mermaid),
                 "```mermaid\npie showData\n    title 债务余额构成 共￥%.0f\n",
                 total_debt);
        for (size_t i = 0; i < n && i < 8; i++) {
            const csilk_json_t* d = csilk_json_array_get(debts, i);
            const char*         nm = csilk_json_get_string(d, "name") ?: "负债";
            double              bal = db_get_num(d, "balance");
            char                safe[64] = {0};
            strncpy(safe, nm, sizeof(safe) - 1);
            for (char* p = safe; *p; p++) {
                if (*p == '"' || *p == '\'' || *p == '|') {
                    *p = ' ';
                }
            }
            char line[160];
            snprintf(line, sizeof(line), "    \"%s\" : %.2f\n", safe, bal);
            strncat(mermaid, line, sizeof(mermaid) - strlen(mermaid) - 1);
        }
        strncat(mermaid, "```\n\n", sizeof(mermaid) - strlen(mermaid) - 1);
        char bar[1024] = {0};
        snprintf(bar,
                 sizeof(bar),
                 "```mermaid\nxychart-beta\n    title \"12期利息对比 (越低越好)\"\n"
                 "    x-axis [\"雪崩\",\"雪球\"]\n"
                 "    y-axis \"利息 ￥\" 0 --> %.0f\n"
                 "    bar [%.0f, %.0f]\n```\n\n",
                 (av_i > sb_i ? av_i : sb_i) * 1.2 + 10,
                 av_i,
                 sb_i);
        strncat(mermaid, bar, sizeof(mermaid) - strlen(mermaid) - 1);
    }

    char rows[8192] = {0};
    for (size_t i = 0; i < n && i < 15; i++) {
        const csilk_json_t* d = csilk_json_array_get(debts, i);
        const char*         nm = csilk_json_get_string(d, "name") ?: "-";
        const char*         tp = csilk_json_get_string(d, "asset_type") ?: "-";
        double              bal = db_get_num(d, "balance");
        double              rate = db_get_num(d, "annual_rate");
        const char*         type_label = (strcmp(tp, "credit_card") == 0)
                                             ? "信用卡"
                                             : (strcmp(tp, "loan") == 0 ? "贷款" : "其他负债");
        char                safe[64] = {0};
        strncpy(safe, nm, sizeof(safe) - 1);
        for (char* p = safe; *p; p++) {
            if (*p == '|') {
                *p = '/';
            }
        }
        char line[256];
        snprintf(line,
                 sizeof(line),
                 "| %s | %s | ￥%.0f | %.1f%% | %.0f |\n",
                 safe,
                 type_label,
                 bal,
                 rate,
                 bal * rate / 100.0 / 12.0);
        strncat(rows, line, sizeof(rows) - strlen(rows) - 1);
    }
    if (!rows[0]) {
        snprintf(rows, sizeof(rows), "| — | — | — | — | — |\n");
    }

    const char* rec_label =
        (strcmp(rec, "avalanche") == 0) ? "❄️ 雪崩法（优先高利率）" : "⛄ 雪球法（优先小余额）";
    const char* rec_reason = (strcmp(rec, "avalanche") == 0) ? "利息最省，适合理性省钱优先"
                                                             : "成就感强、易坚持，适合需要正反馈";

    char buf[16384];
    snprintf(buf,
             sizeof(buf),
             "### 💳 债务加速偿还规划报告\n\n"
             "**负债笔数** `%d` ｜ **总余额** `￥%.2f` ｜ **月供假设** `￥%.0f/月` ｜ **推荐策略** "
             "`%s`\n\n"
             "#### 一、债务构成\n"
             "%s"
             "| 名称 | 类型 | 余额 | 年利率 | 月利息估算 |\n"
             "| :--- | :--- | :--- | :--- | :--- |\n"
             "%s\n"
             "#### 二、12 期模拟对比（月供 ￥%.0f）\n"
             "| 策略 | 12期总利息 | 12期后剩余 | 特点 |\n"
             "| :--- | :--- | :--- | :--- |\n"
             "| ❄️ 雪崩法（高利率优先） | ￥%.0f | ￥%.0f | 利息最省 |\n"
             "| ⛄ 雪球法（小余额优先） | ￥%.0f | ￥%.0f | 成就感强 |\n\n"
             "**利息差额**：雪崩法 12 期可比雪球法节省 **￥%.0f**\n\n"
             "#### 三、执行建议\n"
             "1. **推荐**：%s — %s；\n"
             "2. **最低还款**：每笔至少还 `max(100, 余额×2%%)`，剩余集中攻克目标债务；\n"
             "3. **利率陷阱**：信用卡 18%% 年化月息约 1.5%%，务必优先清偿；\n"
             "4. **额外还款**：任意额外收入（奖金/退税）直接追加到目标债务，可显著缩短周期。\n"
             "```action\n"
             "{\n"
             "  \"action_type\": \"debt_payoff\",\n"
             "  \"debt_count\": %d,\n"
             "  \"total_debt\": %.2f,\n"
             "  \"monthly_payment\": %.2f,\n"
             "  \"recommended\": \"%s\",\n"
             "  \"interest_saved\": %.2f\n"
             "}\n"
             "```\n",
             (int)debt_cnt,
             total_debt,
             mp,
             rec_label,
             mermaid,
             rows,
             mp,
             av_i,
             av_r,
             sb_i,
             sb_r,
             saved,
             rec_label,
             rec_reason,
             (int)debt_cnt,
             total_debt,
             mp,
             rec,
             saved);

    if (root) {
        csilk_json_free(root);
    }
    return strdup(buf);
}

static const ai_workflow_graph_t g_dp_graph = {
    .id = "wf_debt_payoff",
    .title = "债务加速偿还规划",
    .description =
        "汇总房贷/车贷/信用卡等多头负债，测算雪崩法（利息最省）与雪球法（阻力最小）最优清偿路径。",
    .icon = "ph:credit-card",
    .node_count = 3,
    .nodes =
        {
                {"dp_collect",
             "负债明细与利率盘点",
             "汇总所有负债账户余额与对应年化利率",
             step_dp_collect},
                {"dp_simulate",
             "雪崩 vs 雪球策略模拟",
             "多期动态模拟两种策略利息总额与偿清周期",
             step_dp_simulate},
                {"generate_report",
             "清偿大屏与最优方案推荐",
             "输出利息对比柱状图与分步还款规划表",
             step_dp_report},
                },
};

const ai_workflow_graph_t*
ai_workflow_debt_payoff_get_graph(void)
{
    return &g_dp_graph;
}

/* ========================================================================= */
/*  8. Cashflow Forecast (wf_cashflow_forecast)                             */
/* ========================================================================= */

static char*
step_cf_collect(csilk_db_pool_t*    pool,
                int64_t             user_id,
                const csilk_json_t* params,
                const char*         ctx_json)
{
    (void)params;
    (void)ctx_json;
    double avg_burn = cf_get_user_avg_monthly_burn(pool, user_id);
    char   uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char*   p[] = {uid_str, NULL};
    csilk_json_t* inc_res =
        csilk_db_query_param_json(pool,
                                  "SELECT AVG(monthly_sum) as avg_income FROM ("
                                  "  SELECT SUM(amount) as monthly_sum FROM daily_expenses "
                                  "  WHERE user_id = ? AND expense_type = 'income' "
                                  "  GROUP BY substr(expense_date,1,7))",
                                  p);
    double avg_income = 0;
    if (inc_res && csilk_json_array_size(inc_res) > 0) {
        avg_income = db_get_num(csilk_json_array_get(inc_res, 0), "avg_income");
    }
    if (inc_res) {
        csilk_json_free(inc_res);
    }

    int64_t       tot = 0;
    csilk_json_t* asset_arr = asset_list(pool, user_id, 1, 100, NULL, &tot);
    double        liquid = 0;
    if (asset_arr) {
        for (size_t i = 0; i < csilk_json_array_size(asset_arr); i++) {
            const csilk_json_t* a = csilk_json_array_get(asset_arr, i);
            const char*         tp = csilk_json_get_string(a, "asset_type") ?: "";
            int64_t             pid = (int64_t)db_get_num(a, "parent_id");
            if (pid != 0) {
                continue;
            }
            if (strcmp(tp, "cash") == 0 || strcmp(tp, "bank") == 0 ||
                strcmp(tp, "other_asset") == 0) {
                liquid += cf_get_asset_val(a);
            }
        }
        csilk_json_free(asset_arr);
    }
    int horizon = (int)db_get_num((csilk_json_t*)params, "horizon");
    if (horizon < 3) {
        horizon = 6;
    }
    if (horizon > 12) {
        horizon = 12;
    }
    double net_monthly = avg_income - avg_burn;

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "avg_burn", avg_burn);
    csilk_json_add_number(out, "avg_income", avg_income);
    csilk_json_add_number(out, "net_monthly", net_monthly);
    csilk_json_add_number(out, "liquid_cash", liquid);
    csilk_json_add_number(out, "horizon", (double)horizon);
    size_t len = 0;
    char*  s = csilk_json_serialize(out, &len);
    csilk_json_free(out);
    return s;
}

static char*
step_cf_forecast(csilk_db_pool_t*    pool,
                 int64_t             user_id,
                 const csilk_json_t* params,
                 const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* ctx = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    double        avg_burn = 0, avg_income = 0, liquid = 0;
    int           horizon = 6;
    if (ctx) {
        const csilk_json_t* prev = csilk_json_get(ctx, "cf_collect");
        if (prev) {
            avg_burn = db_get_num((csilk_json_t*)prev, "avg_burn");
            avg_income = db_get_num((csilk_json_t*)prev, "avg_income");
            liquid = db_get_num((csilk_json_t*)prev, "liquid_cash");
            horizon = (int)db_get_num((csilk_json_t*)prev, "horizon");
            if (horizon < 3) {
                horizon = 6;
            }
        }
        csilk_json_free(ctx);
    }
    double        net = avg_income - avg_burn;
    csilk_json_t* months = csilk_json_array();
    double        cur = liquid;
    double        min_cash = cur;
    int           min_idx = 0;
    for (int i = 1; i <= horizon; i++) {
        cur += net;
        if (cur < min_cash) {
            min_cash = cur;
            min_idx = i;
        }
        csilk_json_t* m = csilk_json_object();
        csilk_json_add_number(m, "month_idx", (double)i);
        csilk_json_add_number(m, "projected_cash", cur);
        csilk_json_add_number(m, "net", net);
        csilk_json_array_append(months, m);
    }
    int runway = -1;
    if (net < 0 && avg_burn > 0) {
        runway = (int)(liquid / avg_burn);
        if (liquid <= 0) {
            runway = 0;
        }
    } else if (net >= 0) {
        runway = 99;
    }
    int danger = (min_cash < 0 || (runway >= 0 && runway < 3)) ? 1 : 0;

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "avg_burn", avg_burn);
    csilk_json_add_number(out, "avg_income", avg_income);
    csilk_json_add_number(out, "net_monthly", net);
    csilk_json_add_number(out, "liquid_cash", liquid);
    csilk_json_add_number(out, "horizon", (double)horizon);
    csilk_json_add_number(out, "min_cash", min_cash);
    csilk_json_add_number(out, "min_month", (double)min_idx);
    csilk_json_add_number(out, "runway_months", (double)runway);
    csilk_json_add_number(out, "danger", (double)danger);
    csilk_json_add_array(out, "months", months);
    size_t len = 0;
    char*  s = csilk_json_serialize(out, &len);
    csilk_json_free(out);
    return s;
}

static char*
step_cf_report(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t*       ctx = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    const csilk_json_t* fc = ctx ? csilk_json_get(ctx, "cf_forecast") : NULL;
    if (!fc) {
        fc = ctx ? csilk_json_get(ctx, "cf_collect") : NULL;
    }
    double              liquid = fc ? db_get_num((csilk_json_t*)fc, "liquid_cash") : 0;
    double              net = fc ? db_get_num((csilk_json_t*)fc, "net_monthly") : 0;
    double              avg_burn = fc ? db_get_num((csilk_json_t*)fc, "avg_burn") : 0;
    double              avg_income = fc ? db_get_num((csilk_json_t*)fc, "avg_income") : 0;
    double              min_cash = fc ? db_get_num((csilk_json_t*)fc, "min_cash") : liquid;
    int                 runway = fc ? (int)db_get_num((csilk_json_t*)fc, "runway_months") : -1;
    int                 danger = fc ? (int)db_get_num((csilk_json_t*)fc, "danger") : 0;
    int                 horizon = fc ? (int)db_get_num((csilk_json_t*)fc, "horizon") : 6;
    const csilk_json_t* months = fc ? csilk_json_get(fc, "months") : NULL;

    char xychart[4096] = {0};
    if (months && csilk_json_array_size(months) > 0) {
        snprintf(xychart,
                 sizeof(xychart),
                 "```mermaid\nxychart-beta\n"
                 "    title \"未来%d月现金余额预测（￥）\"\n"
                 "    x-axis [",
                 horizon);
        for (size_t i = 0; i < csilk_json_array_size(months); i++) {
            char b[32];
            snprintf(b,
                     sizeof(b),
                     "\"M%zu\"%s",
                     i + 1,
                     i + 1 < csilk_json_array_size(months) ? ", " : "");
            strncat(xychart, b, sizeof(xychart) - strlen(xychart) - 1);
        }
        strncat(xychart,
                "]\n    y-axis \"现金余额\"\n    bar [",
                sizeof(xychart) - strlen(xychart) - 1);
        for (size_t i = 0; i < csilk_json_array_size(months); i++) {
            char b[32];
            snprintf(b,
                     sizeof(b),
                     "%.0f%s",
                     db_get_num((csilk_json_t*)csilk_json_array_get(months, i), "projected_cash"),
                     i + 1 < csilk_json_array_size(months) ? ", " : "");
            strncat(xychart, b, sizeof(xychart) - strlen(xychart) - 1);
        }
        strncat(xychart, "]\n```\n", sizeof(xychart) - strlen(xychart) - 1);
    }

    char rows[4096] = {0};
    if (months) {
        for (size_t i = 0; i < csilk_json_array_size(months); i++) {
            double pc =
                db_get_num((csilk_json_t*)csilk_json_array_get(months, i), "projected_cash");
            char line[128];
            snprintf(line,
                     sizeof(line),
                     "| M%zu | ￥%.0f | %s |\n",
                     i + 1,
                     pc,
                     pc < 0 ? "⚠️ 透支" : (pc < avg_burn ? "偏低" : "稳健"));
            strncat(rows, line, sizeof(rows) - strlen(rows) - 1);
        }
    }
    if (!rows[0]) {
        snprintf(rows, sizeof(rows), "| — | — | — |\n");
    }

    const char* risk_label = danger ? "🔴 现金流告急" : (net < 0 ? "🟡 缓慢消耗" : "🟢 健康盈余");
    const char* advice = danger ? "立即压缩非必要支出，暂停大额消费，考虑补充流动资金"
                                : (net < 0 ? "关注月度赤字，若持续 3 月为负建议调整预算"
                                           : "现金流稳健，可按计划执行储蓄/投资");

    char runway_str[32];
    if (runway == 99) {
        snprintf(runway_str, sizeof(runway_str), "∞（盈余）");
    } else if (runway >= 0) {
        snprintf(runway_str, sizeof(runway_str), "%d 个月", runway);
    } else {
        snprintf(runway_str, sizeof(runway_str), "—");
    }

    char buf[16384];
    snprintf(buf,
             sizeof(buf),
             "### 📈 未来现金流预测报告（%d 个月滚动）\n\n"
             "**当前流动现金** `￥%.2f` ｜ **月均收入** `￥%.0f` ｜ **月均支出** `￥%.0f` ｜ "
             "**月净额** `￥%.0f` ｜ **可持续** `%s` ｜ **风险** `%s`\n\n"
             "%s\n"
             "| 月份 | 预测余额 | 状态 |\n"
             "| :--- | :--- | :--- |\n"
             "%s\n"
             "**最低点**：`￥%.0f`（第 %.0f 月）\n\n"
             "#### 建议\n"
             "1. %s；\n"
             "2. 若月净额为负，缺口 `￥%.0f/月` 需通过增收或节流弥补；\n"
             "3. 建议保持至少 3 个月支出的流动备用金（当前 `￥%.0f`，目标 `￥%.0f`）。\n"
             "```action\n"
             "{\n"
             "  \"action_type\": \"cashflow_forecast\",\n"
             "  \"horizon\": %d,\n"
             "  \"liquid_cash\": %.2f,\n"
             "  \"net_monthly\": %.2f,\n"
             "  \"runway_months\": %d,\n"
             "  \"danger\": %s\n"
             "}\n"
             "```\n",
             horizon,
             liquid,
             avg_income,
             avg_burn,
             net,
             runway_str,
             risk_label,
             xychart,
             rows,
             min_cash,
             fc ? db_get_num((csilk_json_t*)fc, "min_month") : 0,
             advice,
             net < 0 ? -net : 0,
             liquid,
             avg_burn * 3,
             horizon,
             liquid,
             net,
             runway,
             danger ? "true" : "false");
    if (ctx) {
        csilk_json_free(ctx);
    }
    return strdup(buf);
}

static const ai_workflow_graph_t g_cf_graph = {
    .id = "wf_cashflow_forecast",
    .title = "未来现金流滚动预测",
    .description = "基于月均收支与活期现金，模拟未来 3-12 个月资金余额走势与透支风险点。",
    .icon = "ph:trend-up",
    .node_count = 3,
    .nodes =
        {
                {"cf_collect", "收支基线与活期现金汇聚", "汇聚月均收支与活期流动资金", step_cf_collect},
                {"cf_forecast",
             "6个月滚动余额模拟",
             "逐月推演现金余额并识别透支低点",
             step_cf_forecast},
                {"generate_report",
             "预测图表与预警报告",
             "生成现金走势柱状图与应对建议",
             step_cf_report},
                },
};

const ai_workflow_graph_t*
ai_workflow_cashflow_forecast_get_graph(void)
{
    return &g_cf_graph;
}

/* ========================================================================= */
/*  9. Bill Calendar (wf_bill_calendar)                                     */
/* ========================================================================= */

static char*
step_bc_collect(csilk_db_pool_t*    pool,
                int64_t             user_id,
                const csilk_json_t* params,
                const char*         ctx_json)
{
    (void)ctx_json;
    (void)params;
    if (!pool) {
        return strdup("{\"error\":\"db not ready\"}");
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* p_uid[] = {uid_str, NULL};

    csilk_json_t* bills = csilk_json_array();
    double        total_monthly_bill = 0.0;
    int           bill_count = 0;

    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "SELECT id, name, expected_amount, flow_type, start_date, note "
                                  "FROM cashflow_schedules WHERE user_id=? AND status='active'",
                                  p_uid);
    if (res && csilk_json_is_array(res)) {
        size_t n = csilk_json_array_size(res);
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* r = csilk_json_array_get(res, i);
            double              amt = db_get_num(r, "expected_amount");
            if (amt <= 0.0) {
                continue;
            }
            const char* sdate = csilk_json_get_string(r, "start_date") ?: "";
            int         due_day = 1;
            int         y, m, d;
            if (sscanf(sdate, "%d-%d-%d", &y, &m, &d) == 3 && d >= 1 && d <= 31) {
                due_day = d;
            }
            csilk_json_t* item = csilk_json_object();
            csilk_json_add_number(item, "id", (double)db_get_int(r, "id"));
            csilk_json_add_string(item, "name", csilk_json_get_string(r, "name") ?: "现金流排期");
            csilk_json_add_number(item, "amount", amt);
            csilk_json_add_string(item, "kind", "schedule");
            csilk_json_add_string(
                item, "type", csilk_json_get_string(r, "flow_type") ?: "dividend");
            csilk_json_add_number(item, "day", (double)due_day);
            csilk_json_add_string(item, "note", csilk_json_get_string(r, "note") ?: "");
            csilk_json_array_append(bills, item);
            total_monthly_bill += amt;
            bill_count++;
        }
    }
    if (res) {
        csilk_json_free(res);
    }

    int64_t       tot_assets = 0;
    csilk_json_t* alist = asset_list(pool, user_id, 1, 100, NULL, &tot_assets);
    if (alist && csilk_json_is_array(alist)) {
        size_t n = csilk_json_array_size(alist);
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* a = csilk_json_array_get(alist, i);
            const char*         atype = csilk_json_get_string(a, "asset_type") ?: "";
            if (strcmp(atype, "loan") != 0 && strcmp(atype, "credit_card") != 0 &&
                strcmp(atype, "other_liability") != 0) {
                continue;
            }
            double bal = cf_get_asset_val(a);
            if (bal <= 0.0) {
                continue;
            }

            const char* aname = csilk_json_get_string(a, "name") ?: "负债账单";
            const char* anote = csilk_json_get_string(a, "note") ?: "";
            int         due_day = 10;
            const char* p_day = strstr(anote, "日");
            if (!p_day) {
                p_day = strstr(anote, "号");
            }
            if (p_day && p_day > anote) {
                int parsed_d = 0;
                if (sscanf(p_day - 2, "%d", &parsed_d) == 1 && parsed_d >= 1 && parsed_d <= 31) {
                    due_day = parsed_d;
                } else if (sscanf(p_day - 1, "%d", &parsed_d) == 1 && parsed_d >= 1 &&
                           parsed_d <= 31) {
                    due_day = parsed_d;
                }
            }

            csilk_json_t* item = csilk_json_object();
            csilk_json_add_number(item, "id", (double)db_get_int(a, "id"));
            csilk_json_add_string(item, "name", aname);
            csilk_json_add_number(item, "amount", bal);
            csilk_json_add_string(item, "kind", "liability");
            csilk_json_add_string(item, "type", atype);
            csilk_json_add_number(item, "day", (double)due_day);
            csilk_json_add_string(item, "note", anote);
            csilk_json_array_append(bills, item);
            total_monthly_bill += bal;
            bill_count++;
        }
    }
    if (alist) {
        csilk_json_free(alist);
    }

    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    tm_buf.tm_mday -= 90;
    mktime(&tm_buf);
    char since_d[32];
    snprintf(since_d,
             sizeof(since_d),
             "%04d-%02d-%02d",
             tm_buf.tm_year + 1900,
             tm_buf.tm_mon + 1,
             tm_buf.tm_mday);
    const char* p_since[] = {uid_str, since_d, NULL};

    csilk_json_t* res_rec = csilk_db_query_param_json(
        pool,
        "SELECT de.category_id, COALESCE(c.name,'固定支出') as category_name, "
        "de.amount, COUNT(*) as cnt, MAX(de.expense_date) as last_date, "
        "GROUP_CONCAT(de.note, ' | ') as notes "
        "FROM daily_expenses de LEFT JOIN categories c ON c.id=de.category_id "
        "WHERE de.user_id=? AND de.expense_type='expense' AND de.expense_date >= ? "
        "GROUP BY de.category_id, de.amount HAVING cnt >= 2 ORDER BY cnt DESC LIMIT 10",
        p_since);
    if (res_rec && csilk_json_is_array(res_rec)) {
        size_t n = csilk_json_array_size(res_rec);
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* r = csilk_json_array_get(res_rec, i);
            double              amt = db_get_num(r, "amount");
            if (amt <= 0.0) {
                continue;
            }
            const char* last_d = csilk_json_get_string(r, "last_date") ?: "";
            int         due_day = 1;
            int         y, m, d;
            if (sscanf(last_d, "%d-%d-%d", &y, &m, &d) == 3 && d >= 1 && d <= 31) {
                due_day = d;
            }
            const char* cname = csilk_json_get_string(r, "category_name") ?: "固定支出";
            const char* notes = csilk_json_get_string(r, "notes") ?: "";
            char        bill_title[128];
            if (notes && notes[0] && strlen(notes) < 40) {
                snprintf(bill_title, sizeof(bill_title), "%s (%s)", cname, notes);
            } else {
                snprintf(bill_title, sizeof(bill_title), "%s (月度固定)", cname);
            }

            csilk_json_t* item = csilk_json_object();
            csilk_json_add_number(item, "id", (double)db_get_int(r, "category_id"));
            csilk_json_add_string(item, "name", bill_title);
            csilk_json_add_number(item, "amount", amt);
            csilk_json_add_string(item, "kind", "recurring");
            csilk_json_add_string(item, "type", "expense");
            csilk_json_add_number(item, "day", (double)due_day);
            csilk_json_add_string(item, "note", notes);
            csilk_json_array_append(bills, item);
            total_monthly_bill += amt;
            bill_count++;
        }
    }
    if (res_rec) {
        csilk_json_free(res_rec);
    }

    double avg_burn = cf_get_user_avg_monthly_burn(pool, user_id);
    double pressure_ratio = (avg_burn > 0.0) ? (total_monthly_bill / avg_burn) * 100.0 : 0.0;

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_array(out, "bills", bills);
    csilk_json_add_number(out, "total_monthly_bill", total_monthly_bill);
    csilk_json_add_number(out, "bill_count", (double)bill_count);
    csilk_json_add_number(out, "avg_burn", avg_burn);
    csilk_json_add_number(out, "pressure_ratio", pressure_ratio);

    size_t slen = 0;
    char*  s = csilk_json_serialize(out, &slen);
    csilk_json_free(out);
    return s ? s : strdup("{}");
}

static char*
step_bc_calendar(csilk_db_pool_t*    pool,
                 int64_t             user_id,
                 const csilk_json_t* params,
                 const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* ctx = NULL;
    if (ctx_json && ctx_json[0]) {
        ctx = csilk_json_parse(ctx_json);
    }
    csilk_json_t* bills = NULL;
    if (ctx) {
        csilk_json_t* prev = csilk_json_get(ctx, "bc_collect");
        if (prev) {
            bills = csilk_json_get(prev, "bills");
        }
    }
    double        daily[32] = {0};
    csilk_json_t* calendar = csilk_json_array();
    if (bills && csilk_json_is_array(bills)) {
        size_t n = csilk_json_array_size(bills);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* b = csilk_json_array_get(bills, (int)i);
            if (!b) {
                continue;
            }
            const char* name = csilk_json_get_string(b, "name");
            double      amt = db_get_num(b, "amount");
            int         day = (int)db_get_num(b, "day");
            if (day < 1) {
                day = 1;
            }
            if (day > 30) {
                day = 30;
            }
            daily[day] += amt;

            csilk_json_t* ent = csilk_json_object();
            csilk_json_add_string(ent, "name", name ? name : "账单");
            csilk_json_add_number(ent, "amount", amt);
            csilk_json_add_number(ent, "day", (double)day);
            const char* kind = csilk_json_get_string(b, "kind");
            if (kind) {
                csilk_json_add_string(ent, "kind", kind);
            }
            const char* tp = csilk_json_get_string(b, "type");
            if (tp) {
                csilk_json_add_string(ent, "type", tp);
            }
            csilk_json_array_append(calendar, ent);
        }
    }

    double avg_burn = 0.0;
    if (ctx) {
        csilk_json_t* prev = csilk_json_get(ctx, "bc_collect");
        if (prev) {
            avg_burn = db_get_num(prev, "avg_burn");
        }
    }
    double threshold = (avg_burn > 0.0) ? (avg_burn * 0.3) : 1500.0;
    if (threshold < 1000.0) {
        threshold = 1000.0;
    }

    int    pressure_days = 0;
    double max_day_amt = 0;
    int    max_day = 1;
    for (int d = 1; d <= 30; d++) {
        if (daily[d] > threshold) {
            pressure_days++;
        }
        if (daily[d] > max_day_amt) {
            max_day_amt = daily[d];
            max_day = d;
        }
    }

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_array(out, "calendar", calendar);
    csilk_json_t* darr = csilk_json_array();
    for (int d = 1; d <= 30; d++) {
        csilk_json_array_append(darr, csilk_json_number(daily[d]));
    }
    csilk_json_add_array(out, "daily_totals", darr);
    csilk_json_add_number(out, "pressure_days", (double)pressure_days);
    csilk_json_add_number(out, "max_day", (double)max_day);
    csilk_json_add_number(out, "max_day_amount", max_day_amt);
    csilk_json_add_number(out, "threshold", threshold);
    size_t sl2 = 0;
    char*  s = csilk_json_serialize(out, &sl2);
    csilk_json_free(out);
    if (ctx) {
        csilk_json_free(ctx);
    }
    return s ? s : strdup("{}");
}

static char*
step_bc_report(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* ctx = NULL;
    if (ctx_json && ctx_json[0]) {
        ctx = csilk_json_parse(ctx_json);
    }
    if (!ctx) {
        return strdup("{\"markdown\":\"暂无账单数据。\"}");
    }

    csilk_json_t* collect = csilk_json_get(ctx, "bc_collect");
    csilk_json_t* cal = csilk_json_get(ctx, "bc_calendar");
    double        total_bill = collect ? db_get_num(collect, "total_monthly_bill") : 0;
    double        avg_burn = collect ? db_get_num(collect, "avg_burn") : 0;
    double        pressure_ratio = collect ? db_get_num(collect, "pressure_ratio") : 0;
    int           bill_cnt = collect ? (int)db_get_num(collect, "bill_count") : 0;

    int    pressure_days = cal ? (int)db_get_num(cal, "pressure_days") : 0;
    int    max_day = cal ? (int)db_get_num(cal, "max_day") : 1;
    double max_day_amt = cal ? db_get_num(cal, "max_day_amount") : 0;

    char xychart[8192] = {0};
    if (bill_cnt > 0 && max_day_amt > 0) {
        csilk_json_t* daily = cal ? csilk_json_get(cal, "daily_totals") : NULL;
        char          cats[4096] = {0};
        char          vals[4096] = {0};
        if (daily) {
            size_t n = csilk_json_array_size(daily);
            for (size_t i = 0; i < n && i < 30; i++) {
                char tmp[32];
                snprintf(tmp, sizeof(tmp), "\"%zu日\"", i + 1);
                strcat(cats, tmp);
                if (i + 1 < n && i + 1 < 30) {
                    strcat(cats, ",");
                }
                csilk_json_t* v = csilk_json_array_get(daily, (int)i);
                double        amt = db_get_num(v, "");
                snprintf(tmp, sizeof(tmp), "%.0f", amt);
                strcat(vals, tmp);
                if (i + 1 < n && i + 1 < 30) {
                    strcat(vals, ",");
                }
            }
        }
        snprintf(xychart,
                 sizeof(xychart),
                 "```mermaid\n"
                 "xychart-beta\n"
                 "    title \"未来30日账单压力分布\"\n"
                 "    x-axis [%s]\n"
                 "    y-axis \"金额(￥)\" 0 --> %.0f\n"
                 "    bar [%s]\n"
                 "```\n",
                 cats[0] ? cats : "\"1日\",\"30日\"",
                 max_day_amt > 0 ? max_day_amt * 1.2 : 2000,
                 vals[0] ? vals : "0,0");
    } else {
        snprintf(
            xychart, sizeof(xychart), "> 💡 **提示**：当前尚未录入周期性账单或待还款负债。\n\n");
    }

    char rows[8192] = {0};
    if (cal) {
        csilk_json_t* cal_arr = csilk_json_get(cal, "calendar");
        if (cal_arr && csilk_json_is_array(cal_arr)) {
            size_t n = csilk_json_array_size(cal_arr);
            for (size_t i = 0; i < n; i++) {
                csilk_json_t* e = csilk_json_array_get(cal_arr, (int)i);
                if (!e) {
                    continue;
                }
                const char* name = csilk_json_get_string(e, "name");
                const char* kind = csilk_json_get_string(e, "kind");
                int         day = (int)db_get_num(e, "day");
                double      amt = db_get_num(e, "amount");
                const char* kind_label = "其他";
                if (kind && (strcmp(kind, "debt") == 0 || strcmp(kind, "liability") == 0)) {
                    kind_label = "负债/信用卡";
                } else if (kind &&
                           (strcmp(kind, "subscription") == 0 || strcmp(kind, "recurring") == 0)) {
                    kind_label = "固定/订阅";
                } else if (kind && strcmp(kind, "schedule") == 0) {
                    kind_label = "现金流排期";
                }
                char line[512];
                snprintf(line,
                         sizeof(line),
                         "| %02d日 | %s | %s | ￥%.2f |\n",
                         day,
                         name ? name : "-",
                         kind_label,
                         amt);
                if (strlen(rows) + strlen(line) < sizeof(rows) - 1) {
                    strcat(rows, line);
                }
            }
        }
    }
    if (rows[0] == '\0') {
        snprintf(rows, sizeof(rows), "| - | 暂无待还账单 | - | - |\n");
    }

    const char* risk = "低";
    if (pressure_days >= 3 || pressure_ratio >= 80) {
        risk = "高";
    } else if (pressure_days >= 1 || pressure_ratio >= 50) {
        risk = "中";
    }

    char advice[512];
    if (bill_cnt == 0) {
        snprintf(advice, sizeof(advice), "当前无待缴账单，保持良好的无负债或按时还款状态");
    } else if (pressure_days >= 3) {
        snprintf(advice,
                 sizeof(advice),
                 "未来30日有 %d 个高压日，建议将部分账单错峰至空档日",
                 pressure_days);
    } else if (pressure_days >= 1) {
        snprintf(advice,
                 sizeof(advice),
                 "最高压日为 %02d日（￥%.2f），建议提前预留资金",
                 max_day,
                 max_day_amt);
    } else {
        snprintf(advice, sizeof(advice), "账单分布较均衡，无明显高压日");
    }

    char peak_str[128];
    if (bill_cnt > 0 && max_day_amt > 0) {
        snprintf(peak_str, sizeof(peak_str), "%02d日 ￥%.2f", max_day, max_day_amt);
    } else {
        snprintf(peak_str, sizeof(peak_str), "暂无高压日");
    }

    char buf[16384];
    snprintf(buf,
             sizeof(buf),
             "### 📅 未来30日账单日历\n\n"
             "**账单总数** `%d` 笔 ｜ **月账单总额** `￥%.2f` ｜ **月均支出** `￥%.2f` ｜ "
             "**账单/支出比** `%.0f%%` ｜ **高压日** `%d` 天 ｜ **风险** `%s`\n\n"
             "%s\n"
             "| 日期 | 账单 | 类型 | 金额 |\n"
             "| :--- | :--- | :--- | :--- |\n"
             "%s\n"
             "**最高压日**：%s\n\n"
             "#### 建议\n"
             "1. %s；\n"
             "2. 建议在账单日前 3 日确保活期资金充裕；\n"
             "3. 可将订阅或固定类账单统一调整至发薪日后 2 日内自动扣款。\n\n"
             "```action\n"
             "{\n"
             "  \"action_type\": \"bill_calendar\",\n"
             "  \"total_bill\": %.2f,\n"
             "  \"pressure_days\": %d,\n"
             "  \"risk\": \"%s\"\n"
             "}\n"
             "```\n",
             bill_cnt,
             total_bill,
             avg_burn,
             pressure_ratio,
             pressure_days,
             risk,
             xychart,
             rows,
             peak_str,
             advice,
             total_bill,
             pressure_days,
             risk);

    if (ctx) {
        csilk_json_free(ctx);
    }
    return strdup(buf);
}

static const ai_workflow_graph_t g_bc_graph = {
    .id = "wf_bill_calendar",
    .title = "账单日历与高压日检测",
    .description = "汇聚负债与订阅账单，生成未来30日账单日历，识别高压日并给出错峰建议。",
    .icon = "ph:calendar-blank",
    .node_count = 3,
    .nodes =
        {
                {"bc_collect", "账单汇聚", "汇聚负债与订阅类周期账单及总额", step_bc_collect},
                {"bc_calendar", "30日日历排期", "按日期分布账单并测算每日压力", step_bc_calendar},
                {"generate_report",
             "日历图表与错峰建议",
             "生成账单压力分布图与错峰方案",
             step_bc_report},
                },
};

const ai_workflow_graph_t*
ai_workflow_bill_calendar_get_graph(void)
{
    return &g_bc_graph;
}
