#include "services/ai_workflow_service.h"
#include "repositories/asset_repo.h"
#include "repositories/daily_expense_repo.h"
#include "repositories/transaction_repo.h"
#include "repositories/category_repo.h"
#include "repositories/ai_session_repo.h"
#include "common/db.h"
#include "common/ctx.h"
#include "common/response.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ========================================================================= */
/*  Helpers                                                                  */
/* ========================================================================= */

static void
get_current_month_str(char* out, size_t sz)
{
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    snprintf(out, sz, "%04d-%02d", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1);
}

static void
get_prev_month_str(const char* cur_month, char* out, size_t sz)
{
    int year = 0, month = 0;
    if (sscanf(cur_month, "%d-%d", &year, &month) == 2) {
        month--;
        if (month <= 0) {
            month = 12;
            year--;
        }
        snprintf(out, sz, "%04d-%02d", year, month);
    } else {
        snprintf(out, sz, "2026-07");
    }
}

/**
 * @brief Compute real historical monthly average expense for a user.
 */
static double
get_user_avg_monthly_burn(csilk_db_pool_t* pool, int64_t user_id)
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

/* ========================================================================= */
/*  Workflow 1: Monthly Financial Review (月末财务深度复盘 - 真实数据驱动)  */
/* ========================================================================= */

static char*
step_mr_aggregate(csilk_db_pool_t*    pool,
                  int64_t             user_id,
                  const csilk_json_t* params,
                  const char*         ctx_json)
{
    char        month[32];
    const char* m_in = csilk_json_get_string(params, "month");
    if (m_in && m_in[0]) {
        strncpy(month, m_in, sizeof(month) - 1);
        month[sizeof(month) - 1] = '\0';
    } else {
        get_current_month_str(month, sizeof(month));
    }

    char date_pattern[64];
    snprintf(date_pattern, sizeof(date_pattern), "%s%%", month);

    /* 1. Real daily expenses for the month */
    csilk_json_t* exp_stat = de_monthly_totals(pool, user_id, date_pattern);
    double        total_expense = 0.0;
    double        total_daily_income = 0.0;
    if (exp_stat && csilk_json_array_size(exp_stat) > 0) {
        const csilk_json_t* row = csilk_json_array_get(exp_stat, 0);
        total_expense = db_get_num(row, "total_expense");
        total_daily_income = db_get_num(row, "total_income");
    }
    if (exp_stat) {
        csilk_json_free(exp_stat);
    }

    /* 2. Real transactions inflows/outflows for the month */
    csilk_json_t* tx_stat = tx_monthly(pool, user_id, date_pattern);
    double        total_inflows = 0.0;
    double        total_outflows = 0.0;
    int64_t       tx_count = 0;
    if (tx_stat && csilk_json_array_size(tx_stat) > 0) {
        const csilk_json_t* row = csilk_json_array_get(tx_stat, 0);
        total_inflows = db_get_num(row, "inflows");
        total_outflows = db_get_num(row, "outflows");
        tx_count = db_get_int(row, "count");
    }
    if (tx_stat) {
        csilk_json_free(tx_stat);
    }

    /* Combine total income */
    double total_income = total_inflows > 0.0 ? total_inflows : total_daily_income;

    /* 3. Real assets and net worth */
    int64_t       total_assets_count = 0;
    csilk_json_t* assets = asset_list(pool, user_id, 1, 100, NULL, &total_assets_count);
    double        total_assets = 0.0;
    double        total_liabilities = 0.0;
    double        liquid_cash = 0.0;

    if (assets) {
        size_t sz = csilk_json_array_size(assets);
        for (size_t i = 0; i < sz; i++) {
            csilk_json_t* a = csilk_json_array_get(assets, i);
            const char*   type = csilk_json_get_string(a, "asset_type") ?: "";
            double        bal = db_get_num(a, "current_value");
            if (bal == 0.0) {
                bal = db_get_num(a, "balance");
            }
            if (strcmp(type, "credit_card") == 0 || strcmp(type, "loan") == 0 ||
                strcmp(type, "other_liability") == 0) {
                total_liabilities += bal;
            } else {
                total_assets += bal;
                if (strcmp(type, "cash") == 0 || strcmp(type, "bank") == 0) {
                    liquid_cash += bal;
                }
            }
        }
        csilk_json_free(assets);
    }
    double net_worth = total_assets - total_liabilities;

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "month", month);
    csilk_json_add_number(res, "total_expense", total_expense);
    csilk_json_add_number(res, "total_income", total_income);
    csilk_json_add_number(res, "total_inflows", total_inflows);
    csilk_json_add_number(res, "total_outflows", total_outflows);
    csilk_json_add_number(res, "tx_count", (double)tx_count);
    csilk_json_add_number(res, "total_assets", total_assets);
    csilk_json_add_number(res, "total_liabilities", total_liabilities);
    csilk_json_add_number(res, "liquid_cash", liquid_cash);
    csilk_json_add_number(res, "net_worth", net_worth);

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
step_mr_trends(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    char        month[32];
    const char* m_in = csilk_json_get_string(params, "month");
    if (m_in && m_in[0]) {
        strncpy(month, m_in, sizeof(month) - 1);
        month[sizeof(month) - 1] = '\0';
    } else {
        get_current_month_str(month, sizeof(month));
    }

    char prev_month[32];
    get_prev_month_str(month, prev_month, sizeof(prev_month));

    char cur_pat[64], prev_pat[64];
    snprintf(cur_pat, sizeof(cur_pat), "%s%%", month);
    snprintf(prev_pat, sizeof(prev_pat), "%s%%", prev_month);

    csilk_json_t* cur_stat = de_monthly_totals(pool, user_id, cur_pat);
    csilk_json_t* prev_stat = de_monthly_totals(pool, user_id, prev_pat);

    double cur_exp = 0.0, prev_exp = 0.0;
    if (cur_stat && csilk_json_array_size(cur_stat) > 0) {
        cur_exp = db_get_num(csilk_json_array_get(cur_stat, 0), "total_expense");
    }
    if (prev_stat && csilk_json_array_size(prev_stat) > 0) {
        prev_exp = db_get_num(csilk_json_array_get(prev_stat, 0), "total_expense");
    }
    if (cur_stat) {
        csilk_json_free(cur_stat);
    }
    if (prev_stat) {
        csilk_json_free(prev_stat);
    }

    double mom_diff = cur_exp - prev_exp;
    double mom_rate = 0.0;
    if (prev_exp > 0.0) {
        mom_rate = (mom_diff / prev_exp) * 100.0;
    }

    /* Real Category Breakdown */
    csilk_json_t* cat_stats = de_monthly_by_category(pool, user_id, cur_pat);
    csilk_json_t* top_cats = csilk_json_array();
    if (cat_stats && csilk_json_is_array(cat_stats)) {
        size_t n = csilk_json_array_size(cat_stats);
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* item = csilk_json_array_get(cat_stats, i);
            const char*         exp_type = csilk_json_get_string(item, "expense_type");
            if (!exp_type || strcmp(exp_type, "expense") == 0) {
                csilk_json_array_append(top_cats, csilk_json_copy(item));
            }
        }
        csilk_json_free(cat_stats);
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "current_month", month);
    csilk_json_add_string(res, "prev_month", prev_month);
    csilk_json_add_number(res, "current_expense", cur_exp);
    csilk_json_add_number(res, "prev_expense", prev_exp);
    csilk_json_add_number(res, "mom_diff", mom_diff);
    csilk_json_add_number(res, "mom_rate", mom_rate);
    csilk_json_add_array(res, "categories", top_cats);

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
step_mr_health(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "aggregate_data") : NULL;

    double income = s0 ? db_get_num(s0, "total_income") : 0.0;
    double expense = s0 ? db_get_num(s0, "total_expense") : 0.0;
    double liquid_cash = s0 ? db_get_num(s0, "liquid_cash") : 0.0;
    double net_worth = s0 ? db_get_num(s0, "net_worth") : 0.0;
    double total_liab = s0 ? db_get_num(s0, "total_liabilities") : 0.0;
    double total_assets = s0 ? db_get_num(s0, "total_assets") : 0.0;
    if (root) {
        csilk_json_free(root);
    }

    /* Savings rate calculation based on real records */
    double savings_rate = 0.0;
    if (income > 0.0) {
        savings_rate = ((income - expense) / income) * 100.0;
        if (savings_rate < 0.0) {
            savings_rate = 0.0;
        }
    }

    /* Real average monthly burn */
    double avg_burn = get_user_avg_monthly_burn(pool, user_id);
    if (avg_burn <= 0.0) {
        avg_burn = expense > 0.0 ? expense : 0.0;
    }

    /* Real emergency reserve months */
    double emergency_months =
        (avg_burn > 0.0) ? (liquid_cash / avg_burn) : (liquid_cash > 0.0 ? 12.0 : 0.0);

    /* Debt to Asset Ratio */
    double debt_ratio = (total_assets > 0.0) ? (total_liab / total_assets) * 100.0 : 0.0;

    /* Dynamic Scoring */
    int score = 75;
    if (income > 0.0 && savings_rate >= 30.0) {
        score += 10;
    } else if (income > 0.0 && savings_rate < 10.0) {
        score -= 10;
    }

    if (emergency_months >= 6.0) {
        score += 10;
    } else if (emergency_months >= 3.0) {
        score += 5;
    } else if (emergency_months < 1.0) {
        score -= 15;
    }

    if (debt_ratio > 50.0) {
        score -= 15;
    } else if (debt_ratio < 20.0) {
        score += 5;
    }

    if (net_worth > 0.0) {
        score += 5;
    }

    if (score > 100) {
        score = 100;
    }
    if (score < 30) {
        score = 30;
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "health_score", (double)score);
    csilk_json_add_number(res, "savings_rate", savings_rate);
    csilk_json_add_number(res, "emergency_months", emergency_months);
    csilk_json_add_number(res, "debt_ratio", debt_ratio);
    csilk_json_add_number(res, "avg_burn", avg_burn);

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
step_mr_generate_report(csilk_db_pool_t*    pool,
                        int64_t             user_id,
                        const csilk_json_t* params,
                        const char*         ctx_json)
{
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "aggregate_data") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "analyze_trends") : NULL;
    csilk_json_t* s2 = root ? csilk_json_get(root, "health_diagnosis") : NULL;

    const char* month = s0 ? csilk_json_get_string(s0, "month") : "2026-08";
    double      expense = s0 ? db_get_num(s0, "total_expense") : 0.0;
    double      income = s0 ? db_get_num(s0, "total_income") : 0.0;
    double      net_worth = s0 ? db_get_num(s0, "net_worth") : 0.0;
    double      mom_rate = s1 ? db_get_num(s1, "mom_rate") : 0.0;
    double      mom_diff = s1 ? db_get_num(s1, "mom_diff") : 0.0;
    double      score = s2 ? db_get_num(s2, "health_score") : 80.0;
    double      savings_rate = s2 ? db_get_num(s2, "savings_rate") : 0.0;
    double      em_months = s2 ? db_get_num(s2, "emergency_months") : 0.0;
    double      debt_ratio = s2 ? db_get_num(s2, "debt_ratio") : 0.0;

    /* Build real Mermaid Pie Chart from actual category list */
    char          mermaid_pie[2048] = {0};
    csilk_json_t* cats = s1 ? csilk_json_get(s1, "categories") : NULL;
    size_t        cat_count = (cats && csilk_json_is_array(cats)) ? csilk_json_array_size(cats) : 0;

    if (cat_count > 0) {
        snprintf(mermaid_pie,
                 sizeof(mermaid_pie),
                 "```mermaid\n"
                 "pie showData\n"
                 "    title %s 实际支出分类构成\n",
                 month);
        for (size_t i = 0; i < cat_count && i < 8; i++) {
            const csilk_json_t* c_row = csilk_json_array_get(cats, i);
            const char*         c_name = csilk_json_get_string(c_row, "category_name") ?: "未分类";
            double              c_amt = db_get_num(c_row, "amount");
            if (c_amt > 0.0) {
                char line[128];
                snprintf(line, sizeof(line), "    \"%s\" : %.2f\n", c_name, c_amt);
                strncat(mermaid_pie, line, sizeof(mermaid_pie) - strlen(mermaid_pie) - 1);
            }
        }
        strncat(mermaid_pie, "```\n\n", sizeof(mermaid_pie) - strlen(mermaid_pie) - 1);
    } else {
        snprintf(mermaid_pie,
                 sizeof(mermaid_pie),
                 "> 💡 **提示**：%s 暂无分类支出明细记录。\n\n",
                 month);
    }

    char buf[8192];
    snprintf(buf,
             sizeof(buf),
             "### 📊 %s 月度财务深度复盘报告\n\n"
             "**综合财务健康评分**：`%.0f / 100` ⭐\n\n"
             "#### 一、真实收支与净资产全景\n"
             "- 💰 **本月总收入**：`￥%.2f`\n"
             "- 🛒 **本月日常支出**：`￥%.2f`\n"
             "- ⚖️ **本月收支净结余**：`%s￥%.2f`\n"
             "- 📈 **支出环比变动**：`%+.1f%%`（环比变动额：%+.2f 元）\n"
             "- 🏦 **当前总净资产**：`￥%.2f`（资产负债率：%.1f%%）\n"
             "- 🎯 **本月实际储蓄率**：`%.1f%%`\n"
             "- 🛡️ **应急备用金覆盖**：`%.1f 个月`\n\n"
             "#### 二、支出构成可视化分析\n"
             "%s"
             "#### 三、AI 财务优化洞察\n"
             "1. **收支平衡状态**：%s\n"
             "2. **支出异动提示**：%s\n"
             "3. **流动性与备用金**：%s",
             month,
             score,
             income,
             expense,
             (income - expense >= 0 ? "+" : "-"),
             fabs(income - expense),
             mom_rate,
             mom_diff,
             net_worth,
             debt_ratio,
             savings_rate,
             em_months,
             mermaid_pie,
             income > expense
                 ? "本月实现正向资金净结余，建议将结余部分按预定比例转入定投或中长期理财。"
                 : (expense > 0
                        ? "本月支出超出当月收入，请关注是否有大额低频消费或临时性非刚需支出。"
                        : "本月暂无收支记录，保持良好记账习惯是财务管理的第一步。"),
             fabs(mom_rate) > 20.0
                 ? (mom_rate > 0
                        ? "⚠️ 本月日常支出环比显著增长超过 20%，建议检查大类开销明细排查超支原因。"
                        : "🟢 本月日常支出环比显著下降超过 20%，控制良好！")
                 : "✅ 本月收支走势总体平稳，环比波动处于正常合理区间。",
             em_months >= 6.0
                 ? "🛡️ 应急备用金充足（覆盖 6 个月以上刚性支出），抗风险能力强。"
                 : (em_months >= 3.0
                        ? "🟡 备用金处于安全边际（3~6 个月），建议继续保持稳健积累。"
                        : "⚠️ 备用金不足 3 个月，建议优先储备充足的活期流动性资金应对不时之需。"));

    if (root) {
        csilk_json_free(root);
    }
    return strdup(buf);
}

/* ========================================================================= */
/*  Workflow 2: Portfolio Rebalance (投资组合再平衡体检 - 真实数据驱动)       */
/* ========================================================================= */

static char*
step_pr_scan(csilk_db_pool_t*    pool,
             int64_t             user_id,
             const csilk_json_t* params,
             const char*         ctx_json)
{
    int64_t       total = 0;
    csilk_json_t* list = asset_list(pool, user_id, 1, 100, NULL, &total);
    double        stock_val = 0.0, fund_val = 0.0, crypto_val = 0.0, bond_val = 0.0, cash_val = 0.0;
    csilk_json_t* top_holdings = csilk_json_array();

    if (list) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* a = csilk_json_array_get(list, i);
            const char*   type = csilk_json_get_string(a, "asset_type") ?: "";
            const char*   name = csilk_json_get_string(a, "name") ?: "未命名资产";
            double        val = db_get_num(a, "current_value");
            if (val == 0.0) {
                val = db_get_num(a, "balance");
            }

            if (strcmp(type, "stock") == 0) {
                stock_val += val;
            } else if (strcmp(type, "fund") == 0) {
                fund_val += val;
            } else if (strcmp(type, "crypto") == 0) {
                crypto_val += val;
            } else if (strcmp(type, "bond") == 0) {
                bond_val += val;
            } else if (strcmp(type, "cash") == 0 || strcmp(type, "bank") == 0) {
                cash_val += val;
            }

            /* Record holding asset item if value > 0 */
            if (val > 0.0 && (strcmp(type, "stock") == 0 || strcmp(type, "fund") == 0 ||
                              strcmp(type, "crypto") == 0 || strcmp(type, "bond") == 0)) {
                csilk_json_t* h = csilk_json_object();
                csilk_json_add_string(h, "name", name);
                csilk_json_add_string(h, "type", type);
                csilk_json_add_number(h, "current_value", val);
                csilk_json_add_number(h, "cost_basis", db_get_num(a, "cost_basis"));
                csilk_json_add_number(h, "quantity", db_get_num(a, "quantity"));
                csilk_json_array_append(top_holdings, h);
            }
        }
        csilk_json_free(list);
    }

    double total_val = stock_val + fund_val + crypto_val + bond_val + cash_val;

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "total_portfolio_value", total_val);
    csilk_json_add_number(res, "stock_value", stock_val);
    csilk_json_add_number(res, "fund_value", fund_val);
    csilk_json_add_number(res, "crypto_value", crypto_val);
    csilk_json_add_number(res, "bond_value", bond_val);
    csilk_json_add_number(res, "cash_value", cash_val);
    csilk_json_add_array(res, "holdings", top_holdings);

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
step_pr_exposure(csilk_db_pool_t*    pool,
                 int64_t             user_id,
                 const csilk_json_t* params,
                 const char*         ctx_json)
{
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "scan_holdings") : NULL;

    double total = s0 ? db_get_num(s0, "total_portfolio_value") : 0.0;
    double stock = s0 ? db_get_num(s0, "stock_value") : 0.0;
    double fund = s0 ? db_get_num(s0, "fund_value") : 0.0;
    double crypto = s0 ? db_get_num(s0, "crypto_value") : 0.0;
    double bond = s0 ? db_get_num(s0, "bond_value") : 0.0;
    double cash = s0 ? db_get_num(s0, "cash_value") : 0.0;
    if (root) {
        csilk_json_free(root);
    }

    double equity_val = stock + fund + crypto;
    double fixed_val = bond;
    double cash_val = cash;

    double equity_pct = (total > 0.0) ? (equity_val / total) * 100.0 : 0.0;
    double fixed_pct = (total > 0.0) ? (fixed_val / total) * 100.0 : 0.0;
    double cash_pct = (total > 0.0) ? (cash_val / total) * 100.0 : 0.0;

    /* Target Benchmark: 50% Equity, 30% Fixed Income, 20% Cash */
    double equity_deviation = (total > 0.0) ? (equity_pct - 50.0) : 0.0;
    double fixed_deviation = (total > 0.0) ? (fixed_pct - 30.0) : 0.0;
    double cash_deviation = (total > 0.0) ? (cash_pct - 20.0) : 0.0;

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "equity_pct", equity_pct);
    csilk_json_add_number(res, "fixed_pct", fixed_pct);
    csilk_json_add_number(res, "cash_pct", cash_pct);
    csilk_json_add_number(res, "equity_deviation", equity_deviation);
    csilk_json_add_number(res, "fixed_deviation", fixed_deviation);
    csilk_json_add_number(res, "cash_deviation", cash_deviation);

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
step_pr_report(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "scan_holdings") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "risk_exposure") : NULL;

    double total = s0 ? db_get_num(s0, "total_portfolio_value") : 0.0;
    double stock = s0 ? db_get_num(s0, "stock_value") : 0.0;
    double fund = s0 ? db_get_num(s0, "fund_value") : 0.0;
    double crypto = s0 ? db_get_num(s0, "crypto_value") : 0.0;
    double bond = s0 ? db_get_num(s0, "bond_value") : 0.0;
    double cash = s0 ? db_get_num(s0, "cash_value") : 0.0;

    double equity_pct = s1 ? db_get_num(s1, "equity_pct") : 0.0;
    double fixed_pct = s1 ? db_get_num(s1, "fixed_pct") : 0.0;
    double cash_pct = s1 ? db_get_num(s1, "cash_pct") : 0.0;
    double eq_dev = s1 ? db_get_num(s1, "equity_deviation") : 0.0;

    /* Build Dynamic Mermaid Pie Chart from actual holdings */
    char mermaid_pie[2048] = {0};
    if (total > 0.0) {
        snprintf(mermaid_pie,
                 sizeof(mermaid_pie),
                 "```mermaid\n"
                 "pie showData\n"
                 "    title 实际投资资产大类配置分布\n");
        if (stock > 0.0) {
            char l[128];
            snprintf(
                l, sizeof(l), "    \"股票证券 (%.1f%%)\" : %.2f\n", (stock / total) * 100.0, stock);
            strncat(mermaid_pie, l, sizeof(mermaid_pie) - strlen(mermaid_pie) - 1);
        }
        if (fund > 0.0) {
            char l[128];
            snprintf(
                l, sizeof(l), "    \"基金理财 (%.1f%%)\" : %.2f\n", (fund / total) * 100.0, fund);
            strncat(mermaid_pie, l, sizeof(mermaid_pie) - strlen(mermaid_pie) - 1);
        }
        if (crypto > 0.0) {
            char l[128];
            snprintf(l,
                     sizeof(l),
                     "    \"数字货币 (%.1f%%)\" : %.2f\n",
                     (crypto / total) * 100.0,
                     crypto);
            strncat(mermaid_pie, l, sizeof(mermaid_pie) - strlen(mermaid_pie) - 1);
        }
        if (bond > 0.0) {
            char l[128];
            snprintf(
                l, sizeof(l), "    \"债券固收 (%.1f%%)\" : %.2f\n", (bond / total) * 100.0, bond);
            strncat(mermaid_pie, l, sizeof(mermaid_pie) - strlen(mermaid_pie) - 1);
        }
        if (cash > 0.0) {
            char l[128];
            snprintf(
                l, sizeof(l), "    \"现金存款 (%.1f%%)\" : %.2f\n", (cash / total) * 100.0, cash);
            strncat(mermaid_pie, l, sizeof(mermaid_pie) - strlen(mermaid_pie) - 1);
        }
        strncat(mermaid_pie, "```\n\n", sizeof(mermaid_pie) - strlen(mermaid_pie) - 1);
    } else {
        snprintf(
            mermaid_pie, sizeof(mermaid_pie), "> 💡 **提示**：当前尚未添加投资或现金类资产。\n\n");
    }

    char buf[8192];
    snprintf(
        buf,
        sizeof(buf),
        "### 📈 投资组合大类资产配置与再平衡报告\n\n"
        "**总资产组合规模**：`￥%.2f`\n\n"
        "#### 一、大类资产敞口与偏离度（真实数据）\n"
        "- 📊 **权益类资产（股票/基金/加密货币）**：`￥%.2f` (占比 **%.1f%%**，基准 "
        "50.0%%，偏离度：%+.1f%%)\n"
        "- 🛡️ **固收及债券资产**：`￥%.2f` (占比 **%.1f%%**，基准 30.0%%)\n"
        "- 💵 **流动性现金及存款**：`￥%.2f` (占比 **%.1f%%**，基准 20.0%%)\n\n"
        "#### 二、资产配置可视化图表\n"
        "%s"
        "#### 三、再平衡操作与风控建议\n"
        "%s\n\n"
        "1. "
        "**定投平衡法**"
        "：对于偏离基准的大类，优先通过后续月度现金流结余定投补充低配资产，避免频繁调仓产生额外摩擦"
        "成本；\n"
        "2. **流动性纪律**：无论市场行情如何变动，建议保留至少 15%%~20%% "
        "的现金及等价物作为防守底仓。",
        total,
        (stock + fund + crypto),
        equity_pct,
        eq_dev,
        bond,
        fixed_pct,
        cash,
        cash_pct,
        mermaid_pie,
        total <= 0.0
            ? "当前暂无资产数据，请先在「资产管理」中添加账户或持仓。"
            : (fabs(eq_dev) > 15.0
                   ? (eq_dev > 0
                          ? "⚠️ **权益类超配警报**：权益类资产偏离基准超过 "
                            "+15%"
                            "，组合波动风险较大。建议逐步止盈部分高估值标的，将资金再平衡至稳健固收"
                            "类。"
                          : "🟡 **权益类低配提示**：权益类资产偏离基准超过 "
                            "-15%"
                            "，防御性过强。可逢低逐步增配优质宽基指数或核心资产提升长期复合回报。")
                   : "✅ **资产配置健康稳健**：当前各大类资产敞口均处于合理波动容忍区间（±15% "
                     "内），无需进行激进调仓。"));

    if (root) {
        csilk_json_free(root);
    }
    return strdup(buf);
}

/* ========================================================================= */
/*  Workflow 3: Major Expense Decision (大额支出决策评估 - 真实数据驱动)     */
/* ========================================================================= */

static char*
step_ed_assess(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
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
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "assess_liquidity") : NULL;

    double target = s0 ? db_get_num(s0, "target_amount") : 5000.0;
    double liquid = s0 ? db_get_num(s0, "liquid_cash") : 0.0;
    if (root) {
        csilk_json_free(root);
    }

    double remaining_cash = liquid - target;
    double avg_monthly_burn = get_user_avg_monthly_burn(pool, user_id);
    if (avg_monthly_burn <= 0.0) {
        avg_monthly_burn = 3000.0;
    }

    double runway_months = (avg_monthly_burn > 0.0) ? (remaining_cash / avg_monthly_burn) : 0.0;
    if (remaining_cash < 0.0) {
        runway_months = 0.0;
    }

    int is_safe = (remaining_cash >= 0.0 && runway_months >= 3.0);

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
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "assess_liquidity") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "stress_test") : NULL;

    double target = s0 ? db_get_num(s0, "target_amount") : 5000.0;
    double liquid = s0 ? db_get_num(s0, "liquid_cash") : 0.0;
    double remaining = s1 ? db_get_num(s1, "remaining_cash") : 0.0;
    double monthly_burn = s1 ? db_get_num(s1, "avg_monthly_burn") : 3000.0;
    double runway = s1 ? db_get_num(s1, "runway_months") : 0.0;
    int    is_safe = s1 ? csilk_json_get_bool(s1, "is_safe") : 0;

    double installment_3_fee = target * 0.02;
    double installment_3_monthly = (target + installment_3_fee) / 3.0;

    double installment_6_fee = target * 0.042;
    double installment_6_monthly = (target + installment_6_fee) / 6.0;

    double installment_12_fee = target * 0.075;
    double installment_12_monthly = (target + installment_12_fee) / 12.0;

    char buf[8192];
    snprintf(buf,
             sizeof(buf),
             "### ⚖️ 大额消费智能决策评估报告\n\n"
             "**拟计划支出金额**：`￥%.2f`\n\n"
             "#### 一、流动性压力测试（基于真实账户余额）\n"
             "- 🏦 **当前可用流动现金（现金+银行存款）**：`￥%.2f`\n"
             "- 📉 **支出后剩余备用金**：`%s￥%.2f`\n"
             "- 📊 **历史月均刚性开销**：`￥%.2f / 月`\n"
             "- 🛡️ **支出后可维持保障时长**：`%.1f 个月`（安全边际底线：3.0 个月）\n"
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
             monthly_burn,
             runway,
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

/* ========================================================================= */
/*  Workflow Registry                                                        */
/* ========================================================================= */

static const ai_workflow_def_t g_workflows[] = {
    {
     .id = "wf_monthly_review",
     .title = "月末财务深度复盘",
     .description =
            "自动汇聚本月收支与资产净值，环比异动分析与财务健康度综合评分，生成专业复盘图表报告。", .icon = "ph:calendar-check",
     .step_count = 4,
     .steps =
            {
                {"aggregate_data",
                 "数据汇聚与对账",
                 "拉取本月日常收支、交易流向及资金账户净资产",
                 step_mr_aggregate},
                {"analyze_trends",
                 "分类异动与超支项分析",
                 "对比上月收支环比波动，挖掘高频与异常支出项",
                 step_mr_trends},
                {"health_diagnosis",
                 "财务健康度综合评分",
                 "计算储蓄率、应急保障月数及负债健康指数",
                 step_mr_health},
                {"generate_report",
                 "生成深度复盘报告与图表",
                 "结构化复盘建议与 Mermaid 收支流向图渲染",
                 step_mr_generate_report},
            }, },
    {
     .id = "wf_portfolio_rebalance",
     .title = "投资组合再平衡体检",
     .description = "扫描股票/基金/债券/"
                       "加密资产全局持仓，计算大类资产配置偏离度，输出再平衡调仓建议与草案。",                                                                .icon = "ph:chart-polar",
     .step_count = 3,
     .steps =
            {
                {"scan_holdings",
                 "全局持仓与资产扫描",
                 "统计权益、固收、现金各类资产最新市值与比重",
                 step_pr_scan},
                {"risk_exposure",
                 "大类资产敞口测算",
                 "比对标准大类配置基准，计算偏离幅度",
                 step_pr_exposure},
                {"generate_report",
                 "调仓方案与图表生成",
                 "生成资产配置结构图与再平衡执行方案",
                 step_pr_report},
            }, },
    {
     .id = "wf_expense_decision",
     .title = "大额支出智能决策评估",
     .description = "测算大额支出对未来现金流与应急备用金的冲击程度，对比全款与分期成本，提供量"
                       "化决策建议。",                                                                                  .icon = "ph:scales",
     .step_count = 3,
     .steps =
            {
                {"assess_liquidity",
                 "流动性资金池测算",
                 "获取活期与高流动性现金资产总额",
                 step_ed_assess},
                {"stress_test",
                 "安全边际压力测试",
                 "模拟支出后未来 3~6 个月刚性支出保障能力",
                 step_ed_stress_test},
                {"generate_report",
                 "综合决策评估与执行草案",
                 "输出支付方案对比建议与预定支出备忘",
                 step_ed_report},
            }, },
};

static const size_t g_workflow_count = sizeof(g_workflows) / sizeof(g_workflows[0]);

csilk_json_t*
ai_workflow_get_definitions_json(void)
{
    csilk_json_t* arr = csilk_json_array();
    for (size_t i = 0; i < g_workflow_count; i++) {
        const ai_workflow_def_t* wf = &g_workflows[i];
        csilk_json_t*            w = csilk_json_object();
        csilk_json_add_string(w, "id", wf->id);
        csilk_json_add_string(w, "title", wf->title);
        csilk_json_add_string(w, "description", wf->description);
        csilk_json_add_string(w, "icon", wf->icon);
        csilk_json_add_number(w, "step_count", (double)wf->step_count);

        csilk_json_t* steps_arr = csilk_json_array();
        for (int j = 0; j < wf->step_count; j++) {
            csilk_json_t* st = csilk_json_object();
            csilk_json_add_string(st, "step_id", wf->steps[j].step_id);
            csilk_json_add_string(st, "title", wf->steps[j].title);
            csilk_json_add_string(st, "description", wf->steps[j].description);
            csilk_json_array_append(steps_arr, st);
        }
        csilk_json_add_array(w, "steps", steps_arr);
        csilk_json_array_append(arr, w);
    }
    return arr;
}

/* ========================================================================= */
/*  Workflow Execution & SSE Streaming                                       */
/* ========================================================================= */

void
ai_workflow_run_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    const char*         workflow_id = csilk_json_get_string(body, "workflow_id");
    int64_t             session_id = (int64_t)db_get_num(body, "session_id");
    const csilk_json_t* params = csilk_json_get(body, "params");

    if (!workflow_id || !workflow_id[0]) {
        csilk_json_free(body);
        respond_bad_request(c, "缺少 workflow_id");
        return;
    }

    /* Find workflow definition */
    const ai_workflow_def_t* target_wf = NULL;
    for (size_t i = 0; i < g_workflow_count; i++) {
        if (strcmp(g_workflows[i].id, workflow_id) == 0) {
            target_wf = &g_workflows[i];
            break;
        }
    }

    if (!target_wf) {
        csilk_json_free(body);
        respond_bad_request(c, "未找到指定的工作流");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();

    /* Auto create session if not specified */
    if (session_id <= 0 && pool) {
        session_id = ai_session_insert(pool, user_id, target_wf->title, "workflow-agent", "system");
    }

    /* Start SSE streaming */
    csilk_sse_init(c);

    /* 1. Send workflow_start */
    csilk_json_t* ev_start = csilk_json_object();
    csilk_json_add_string(ev_start, "workflow_id", target_wf->id);
    csilk_json_add_string(ev_start, "title", target_wf->title);
    csilk_json_add_number(ev_start, "total_steps", (double)target_wf->step_count);
    csilk_json_add_number(ev_start, "session_id", (double)session_id);
    size_t slen = 0;
    char*  str_start = csilk_json_serialize(ev_start, &slen);
    csilk_json_free(ev_start);
    csilk_sse_send(c, "workflow_start", str_start ? str_start : "{}");
    free(str_start);

    /* Cumulative context across steps */
    csilk_json_t* ctx_obj = csilk_json_object();
    char*         final_report = NULL;

    for (int i = 0; i < target_wf->step_count; i++) {
        const ai_workflow_step_t* st = &target_wf->steps[i];

        /* Send step_start */
        csilk_json_t* ev_st_start = csilk_json_object();
        csilk_json_add_number(ev_st_start, "step_index", (double)i);
        csilk_json_add_string(ev_st_start, "step_id", st->step_id);
        csilk_json_add_string(ev_st_start, "title", st->title);
        char* str_st_start = csilk_json_serialize(ev_st_start, &slen);
        csilk_json_free(ev_st_start);
        csilk_sse_send(c, "step_start", str_st_start ? str_st_start : "{}");
        free(str_st_start);

        /* Execute step with real database queries */
        char* cur_ctx_str = csilk_json_serialize(ctx_obj, &slen);
        char* step_out = st->execute(pool, user_id, params, cur_ctx_str);
        free(cur_ctx_str);

        char summary_buf[256] = {0};

        if (i == target_wf->step_count - 1) {
            /* Last step is the final markdown report */
            final_report = step_out;
            snprintf(summary_buf, sizeof(summary_buf), "诊断报告与图表生成完毕");
        } else if (step_out) {
            csilk_json_t* parsed_out = csilk_json_parse(step_out);
            if (parsed_out) {
                if (strcmp(st->step_id, "aggregate_data") == 0) {
                    snprintf(summary_buf,
                             sizeof(summary_buf),
                             "本月总收入 ￥%.2f，总支出 ￥%.2f，净资产 ￥%.2f",
                             db_get_num(parsed_out, "total_income"),
                             db_get_num(parsed_out, "total_expense"),
                             db_get_num(parsed_out, "net_worth"));
                } else if (strcmp(st->step_id, "analyze_trends") == 0) {
                    snprintf(summary_buf,
                             sizeof(summary_buf),
                             "支出环比变动 %+.1f%%，已提取核心分类明细",
                             db_get_num(parsed_out, "mom_rate"));
                } else if (strcmp(st->step_id, "health_diagnosis") == 0) {
                    snprintf(summary_buf,
                             sizeof(summary_buf),
                             "健康评分 %.0f 分，储蓄率 %.1f%%，备用金维持 %.1f 个月",
                             db_get_num(parsed_out, "health_score"),
                             db_get_num(parsed_out, "savings_rate"),
                             db_get_num(parsed_out, "emergency_months"));
                } else if (strcmp(st->step_id, "scan_holdings") == 0) {
                    snprintf(summary_buf,
                             sizeof(summary_buf),
                             "总资产规模 ￥%.2f，已扫描权益/固收/现金持仓",
                             db_get_num(parsed_out, "total_portfolio_value"));
                } else if (strcmp(st->step_id, "risk_exposure") == 0) {
                    snprintf(summary_buf,
                             sizeof(summary_buf),
                             "权益占比 %.1f%%，固收占比 %.1f%%，现金占比 %.1f%%",
                             db_get_num(parsed_out, "equity_pct"),
                             db_get_num(parsed_out, "fixed_pct"),
                             db_get_num(parsed_out, "cash_pct"));
                } else if (strcmp(st->step_id, "assess_liquidity") == 0) {
                    snprintf(summary_buf,
                             sizeof(summary_buf),
                             "拟支出 ￥%.2f，当前可用流动现金 ￥%.2f",
                             db_get_num(parsed_out, "target_amount"),
                             db_get_num(parsed_out, "liquid_cash"));
                } else if (strcmp(st->step_id, "stress_test") == 0) {
                    snprintf(summary_buf,
                             sizeof(summary_buf),
                             "支出后剩余现金 ￥%.2f，可维持 %.1f 个月刚性开销",
                             db_get_num(parsed_out, "remaining_cash"),
                             db_get_num(parsed_out, "runway_months"));
                }
                csilk_json_add_object(ctx_obj, st->step_id, parsed_out);
            }
            free(step_out);
        }

        /* Send step_complete with real summary */
        csilk_json_t* ev_st_done = csilk_json_object();
        csilk_json_add_number(ev_st_done, "step_index", (double)i);
        csilk_json_add_string(ev_st_done, "step_id", st->step_id);
        csilk_json_add_string(ev_st_done, "status", "completed");
        if (summary_buf[0]) {
            csilk_json_add_string(ev_st_done, "summary", summary_buf);
        }
        char* str_st_done = csilk_json_serialize(ev_st_done, &slen);
        csilk_json_free(ev_st_done);
        csilk_sse_send(c, "step_complete", str_st_done ? str_st_done : "{}");
        free(str_st_done);
    }

    csilk_json_free(ctx_obj);

    /* Stream final report text in chunks */
    if (final_report && final_report[0]) {
        size_t rlen = strlen(final_report);
        size_t offset = 0;
        size_t chunk_sz = 64;
        while (offset < rlen) {
            size_t take = (offset + chunk_sz < rlen) ? chunk_sz : (rlen - offset);
            char   chunk[128];
            memcpy(chunk, final_report + offset, take);
            chunk[take] = '\0';
            offset += take;

            csilk_json_t* ev_chunk = csilk_json_object();
            csilk_json_add_string(ev_chunk, "content", chunk);
            char* str_chunk = csilk_json_serialize(ev_chunk, &slen);
            csilk_json_free(ev_chunk);
            csilk_sse_send(c, "delta", str_chunk ? str_chunk : "{}");
            free(str_chunk);
        }

        /* Persist generated report in session messages */
        if (pool && session_id > 0) {
            ai_message_insert(pool, session_id, "assistant", final_report, "workflow-agent");
        }
        free(final_report);
    }

    /* Send workflow_complete */
    csilk_json_t* ev_done = csilk_json_object();
    csilk_json_add_string(ev_done, "workflow_id", target_wf->id);
    csilk_json_add_string(ev_done, "status", "completed");
    csilk_json_add_number(ev_done, "session_id", (double)session_id);
    char* str_done = csilk_json_serialize(ev_done, &slen);
    csilk_json_free(ev_done);
    csilk_sse_send(c, "workflow_complete", str_done ? str_done : "{}");
    free(str_done);

    csilk_sse_close(c);
    csilk_json_free(body);
}
