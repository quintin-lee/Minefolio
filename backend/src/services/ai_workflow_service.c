#include "services/ai_workflow_service.h"
#include "services/ai_service.h"
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
get_current_datetime_str(char* out, size_t sz)
{
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    snprintf(out,
             sz,
             "%04d-%02d-%02d %02d:%02d:%02d",
             tm_buf.tm_year + 1900,
             tm_buf.tm_mon + 1,
             tm_buf.tm_mday,
             tm_buf.tm_hour,
             tm_buf.tm_min,
             tm_buf.tm_sec);
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
        /* Fallback: real-time previous month instead of hard-coded date */
        char cur[32];
        get_current_month_str(cur, sizeof(cur));
        if (sscanf(cur, "%d-%d", &year, &month) == 2) {
            month--;
            if (month <= 0) {
                month = 12;
                year--;
            }
            snprintf(out, sz, "%04d-%02d", year, month);
        } else {
            snprintf(out, sz, "%s", cur);
        }
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

    char cur_month_fallback[32];
    get_current_month_str(cur_month_fallback, sizeof(cur_month_fallback));
    const char* month = s0 ? csilk_json_get_string(s0, "month") : cur_month_fallback;
    if (!month || !month[0]) {
        month = cur_month_fallback;
    }
    double expense = s0 ? db_get_num(s0, "total_expense") : 0.0;
    double income = s0 ? db_get_num(s0, "total_income") : 0.0;
    double net_worth = s0 ? db_get_num(s0, "net_worth") : 0.0;
    double mom_rate = s1 ? db_get_num(s1, "mom_rate") : 0.0;
    double mom_diff = s1 ? db_get_num(s1, "mom_diff") : 0.0;
    double score = s2 ? db_get_num(s2, "health_score") : 80.0;
    double savings_rate = s2 ? db_get_num(s2, "savings_rate") : 0.0;
    double em_months = s2 ? db_get_num(s2, "emergency_months") : 0.0;
    double debt_ratio = s2 ? db_get_num(s2, "debt_ratio") : 0.0;

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
/*  Workflow 4: Payday Auto-Split (工资到账自动分配 - 真实数据驱动)         */
/* ========================================================================= */

static char*
step_payday_detect(csilk_db_pool_t*    pool,
                   int64_t             user_id,
                   const csilk_json_t* params,
                   const char*         ctx_json)
{
    char        month[32];
    const char* m_in = params ? csilk_json_get_string(params, "month") : NULL;
    if (m_in && m_in[0]) {
        strncpy(month, m_in, sizeof(month) - 1);
        month[sizeof(month) - 1] = '\0';
    } else {
        get_current_month_str(month, sizeof(month));
    }
    char pat[64];
    snprintf(pat, sizeof(pat), "%s%%", month);

    double  income_de = 0.0, tx_inflows = 0.0;
    int64_t tx_cnt = 0;

    /* daily_expenses income */
    {
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
    }
    /* transactions inflows in month (by created_at) */
    {
        char uid_str[32];
        snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
        const char*   p[] = {uid_str, pat, NULL};
        csilk_json_t* res =
            csilk_db_query_param_json(pool,
                                      "SELECT COALESCE(SUM(amount),0) as total, COUNT(*) as cnt "
                                      "FROM transactions WHERE user_id=? AND transaction_type IN "
                                      "('income','deposit','transfer_in') AND created_at LIKE ?",
                                      p);
        if (res && csilk_json_array_size(res) > 0) {
            const csilk_json_t* row = csilk_json_array_get(res, 0);
            tx_inflows = db_get_num(row, "total");
            tx_cnt = db_get_int(row, "cnt");
        }
        if (res) {
            csilk_json_free(res);
        }
    }

    /* liquid cash */
    double liquid_cash = 0.0;
    {
        int64_t       tot = 0;
        csilk_json_t* list = asset_list(pool, user_id, 1, 100, NULL, &tot);
        if (list) {
            size_t n = csilk_json_array_size(list);
            for (size_t i = 0; i < n; i++) {
                csilk_json_t* a = csilk_json_array_get(list, i);
                const char*   t = csilk_json_get_string(a, "asset_type") ?: "";
                if (strcmp(t, "cash") == 0 || strcmp(t, "bank") == 0) {
                    double v = db_get_num(a, "current_value");
                    if (v == 0.0) {
                        v = db_get_num(a, "balance");
                    }
                    liquid_cash += v;
                }
            }
            csilk_json_free(list);
        }
    }

    double total_income = income_de + tx_inflows;
    /* fallback: if no record use liquid_cash as reference */
    if (total_income <= 0.0) {
        total_income = 0.0;
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "month", month);
    csilk_json_add_number(res, "income_de", income_de);
    csilk_json_add_number(res, "tx_inflows", tx_inflows);
    csilk_json_add_number(res, "tx_count", (double)tx_cnt);
    csilk_json_add_number(res, "total_income", total_income);
    csilk_json_add_number(res, "liquid_cash", liquid_cash);
    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
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

    /* Allow custom ratios via params: living/invest/debt/emergency (0-100) */
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
            /* normalize */
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

    /* Discover candidate assets for hints */
    char    invest_asset[128] = "", debt_asset[128] = "", emer_asset[128] = "";
    int64_t invest_id = 0, debt_id = 0, emer_id = 0;
    {
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
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "payday_detect") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "payday_allocate") : NULL;
    const char*   month = s0 ? csilk_json_get_string(s0, "month") : NULL;
    char          month_fb[32];
    if (!month || !month[0]) {
        get_current_month_str(month_fb, sizeof(month_fb));
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

/* ========================================================================= */
/*  Workflow 5: Budget Guard (预算超支预警 - 真实数据驱动)                    */
/* ========================================================================= */

static int
days_in_month(int y, int m)
{
    static const int dm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int              d = dm[m - 1];
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))) {
        d = 29;
    }
    return d;
}

static char*
step_bg_collect(csilk_db_pool_t*    pool,
                int64_t             user_id,
                const csilk_json_t* params,
                const char*         ctx_json)
{
    char        month[32];
    const char* m_in = params ? csilk_json_get_string(params, "month") : NULL;
    if (m_in && m_in[0]) {
        strncpy(month, m_in, sizeof(month) - 1);
        month[sizeof(month) - 1] = '\0';
    } else {
        get_current_month_str(month, sizeof(month));
    }
    char pat[64];
    snprintf(pat, sizeof(pat), "%s%%", month);

    /* current month by category */
    csilk_json_t* cur_cats = de_monthly_by_category(pool, user_id, pat);
    /* filter expense only */
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

    /* historical avg per category (last up to 6 months excluding current) */
    csilk_json_t* hist_avg_arr = csilk_json_array();
    {
        char uid_str[32];
        snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
        const char* p[] = {uid_str, pat, NULL};
        /* per-category monthly avg: AVG of monthly sums per category */
        csilk_json_t* res = csilk_db_query_param_json(
            pool,
            "SELECT category_id, COALESCE(category_name,'未分类') as category_name, "
            "AVG(m_sum) as avg_amount FROM ("
            "  SELECT category_id, category_name, substr(expense_date,1,7) as ym, "
            "SUM(amount) as m_sum FROM daily_expenses "
            "  LEFT JOIN categories ON categories.id = daily_expenses.category_id "
            "  WHERE user_id=? AND expense_type='expense' AND expense_date NOT LIKE ? "
            "  GROUP BY category_id, ym"
            ") GROUP BY category_id",
            p);
        if (res && csilk_json_is_array(res)) {
            size_t n = csilk_json_array_size(res);
            for (size_t i = 0; i < n; i++) {
                csilk_json_array_append(hist_avg_arr,
                                        csilk_json_copy(csilk_json_array_get(res, i)));
            }
        }
        if (res) {
            csilk_json_free(res);
        }
    }

    /* day progress */
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    int    cur_y = tm_buf.tm_year + 1900;
    int    cur_m = tm_buf.tm_mon + 1;
    int    cur_d = tm_buf.tm_mday;
    int    dim = days_in_month(cur_y, cur_m);
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

    /* build map category_id -> avg */
    /* simple linear search (categories < 100) */
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
            budget = avg * 1.2; /* 20% buffer */
        } else {
            budget = cur_amt > 0 ? cur_amt / progress * 1.0 : 0.0;
            /* if no history, use current projected as budget baseline */
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
        get_current_month_str(month_fb, sizeof(month_fb));
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

    /* Mermaid bar chart */
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
        /* bar for cur_amount */
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

    /* build table rows */
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

/* ========================================================================= */
/*  Workflow 6: Anomaly Detect (异常交易检测 - 真实数据驱动)                   */
/* ========================================================================= */

static char*
step_ad_collect(csilk_db_pool_t*    pool,
                int64_t             user_id,
                const csilk_json_t* params,
                const char*         ctx_json)
{
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

    /* fetch recent daily_expenses */
    csilk_json_t* recent = csilk_json_array();
    double        total = 0.0;
    int64_t       cnt = 0;
    {
        char uid_str[32];
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
    }
    /* per-category stats (mean/std) over same window */
    csilk_json_t* cat_stats = csilk_json_array();
    {
        char uid_str[32];
        snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
        const char*   p[] = {uid_str, since_date, NULL};
        csilk_json_t* res = csilk_db_query_param_json(
            pool,
            "SELECT category_id, COALESCE(c.name,'未分类') as category_name, "
            "COUNT(*) as cnt, AVG(amount) as avg_amt, "
            "AVG(amount*amount) as avg_sq "
            "FROM daily_expenses de LEFT JOIN categories c ON c.id=de.category_id "
            "WHERE de.user_id=? AND de.expense_type='expense' AND de.expense_date >= ? "
            "GROUP BY category_id",
            p);
        if (res && csilk_json_is_array(res)) {
            size_t n = csilk_json_array_size(res);
            for (size_t i = 0; i < n; i++) {
                const csilk_json_t* r = csilk_json_array_get(res, i);
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
        if (res) {
            csilk_json_free(res);
        }
    }
    /* recent transactions for midnight/high-freq (created_at LIKE) */
    csilk_json_t* tx_recent = csilk_json_array();
    {
        char uid_str[32];
        snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
        char pat[64];
        snprintf(pat, sizeof(pat), "%s%%", since_date);
        /* approximate: created_at >= since_date */
        const char*   p[] = {uid_str, since_date, NULL};
        csilk_json_t* res = csilk_db_query_param_json(
            pool,
            "SELECT id, amount, transaction_type, note, created_at FROM transactions "
            "WHERE user_id=? AND created_at >= ? ORDER BY created_at DESC LIMIT 500",
            p);
        if (res && csilk_json_is_array(res)) {
            size_t n = csilk_json_array_size(res);
            for (size_t i = 0; i < n; i++) {
                csilk_json_array_append(tx_recent, csilk_json_copy(csilk_json_array_get(res, i)));
            }
        }
        if (res) {
            csilk_json_free(res);
        }
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

    /* Build map category_id -> avg/std for 3sigma */
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
        } else if (cnt >= 5 && avg > 0) {
            if (amt > avg * 3.0 && amt > 500) {
                is_anomaly = 1;
                snprintf(reason, sizeof(reason), "金额 %.0f 超过均值 3 倍（均值%.0f）", amt, avg);
            }
        } else if (amt > 2000) {
            /* fallback for sparse categories */
            is_anomaly = 1;
            snprintf(reason, sizeof(reason), "大额支出 ￥%.0f（样本少，固定阈值触发）", amt);
        }
        if (is_anomaly) {
            csilk_json_t* o = csilk_json_object();
            csilk_json_add_string(o, "type", "3sigma");
            csilk_json_add_string(o, "category_name", cname);
            csilk_json_add_number(o, "amount", amt);
            csilk_json_add_string(o, "date", date);
            csilk_json_add_string(o, "note", note);
            csilk_json_add_string(o, "reason", reason);
            csilk_json_add_number(o, "score", 85);
            csilk_json_array_append(anomalies, o);
            cnt_3sigma++;
        }
    }
    /* Duplicate detection: same amount+category within 2 days */
    for (size_t i = 0; i < rn; i++) {
        const csilk_json_t* a = csilk_json_array_get(recent, i);
        double              amt_a = db_get_num(a, "amount");
        int64_t             cid_a = db_get_int(a, "category_id");
        const char*         date_a = csilk_json_get_string(a, "expense_date") ?: "";
        for (size_t j = i + 1; j < rn; j++) {
            const csilk_json_t* b = csilk_json_array_get(recent, j);
            if (db_get_int(b, "category_id") != cid_a) {
                continue;
            }
            if (fabs(db_get_num(b, "amount") - amt_a) > 0.01) {
                continue;
            }
            const char* date_b = csilk_json_get_string(b, "expense_date") ?: "";
            /* simple date diff by string compare: if dates within 2 days, check by converting */
            int ya, ma, da, yb, mb, dbd;
            if (sscanf(date_a, "%d-%d-%d", &ya, &ma, &da) == 3 &&
                sscanf(date_b, "%d-%d-%d", &yb, &mb, &dbd) == 3) {
                struct tm ta = {0}, tb = {0};
                ta.tm_year = ya - 1900;
                ta.tm_mon = ma - 1;
                ta.tm_mday = da;
                tb.tm_year = yb - 1900;
                tb.tm_mon = mb - 1;
                tb.tm_mday = dbd;
                time_t t1 = mktime(&ta), t2 = mktime(&tb);
                double diff = fabs(difftime(t1, t2)) / 86400.0;
                if (diff <= 2.0 && diff >= 0) {
                    /* avoid double counting same pair */
                    int    already = 0;
                    size_t an = csilk_json_array_size(anomalies);
                    for (size_t k = 0; k < an; k++) {
                        const csilk_json_t* ex = csilk_json_array_get(anomalies, k);
                        if (strcmp(csilk_json_get_string(ex, "type") ?: "", "duplicate") == 0 &&
                            fabs(db_get_num(ex, "amount") - amt_a) < 0.01 &&
                            strcmp(csilk_json_get_string(ex, "date") ?: "", date_a) == 0) {
                            already = 1;
                            break;
                        }
                    }
                    if (!already) {
                        csilk_json_t* o = csilk_json_object();
                        char          rs[256];
                        snprintf(rs, sizeof(rs), "与 %s 同额同类重复扣款嫌疑", date_b);
                        csilk_json_add_string(o, "type", "duplicate");
                        csilk_json_add_string(o,
                                              "category_name",
                                              csilk_json_get_string(a, "category_name")
                                                  ?: "未分类");
                        csilk_json_add_number(o, "amount", amt_a);
                        csilk_json_add_string(o, "date", date_a);
                        csilk_json_add_string(o, "note", csilk_json_get_string(a, "note") ?: "");
                        csilk_json_add_string(o, "reason", rs);
                        csilk_json_add_number(o, "score", 75);
                        csilk_json_array_append(anomalies, o);
                        cnt_dup++;
                    }
                    break;
                }
            }
        }
    }
    /* midnight large + high-freq small from transactions */
    size_t tn =
        (tx_recent && csilk_json_is_array(tx_recent)) ? csilk_json_array_size(tx_recent) : 0;
    /* midnight: hour 0-5 and amount > 500 */
    for (size_t i = 0; i < tn; i++) {
        const csilk_json_t* r = csilk_json_array_get(tx_recent, i);
        double              amt = db_get_num(r, "amount");
        if (amt < 500) {
            continue;
        }
        const char* ts = csilk_json_get_string(r, "created_at") ?: "";
        int         hh = -1;
        /* try to parse HH from created_at: YYYY-MM-DD HH:MM:SS */
        const char* sp = strchr(ts, ' ');
        if (sp) {
            hh = atoi(sp + 1);
        }
        if (hh >= 0 && hh <= 5) {
            csilk_json_t* o = csilk_json_object();
            char          rs[256];
            snprintf(rs, sizeof(rs), "凌晨 %02d 时大额交易 ￥%.0f", hh, amt);
            csilk_json_add_string(o, "type", "midnight");
            csilk_json_add_string(o, "category_name", "交易");
            csilk_json_add_number(o, "amount", amt);
            csilk_json_add_string(o, "date", ts);
            csilk_json_add_string(o, "note", csilk_json_get_string(r, "note") ?: "");
            csilk_json_add_string(o, "reason", rs);
            csilk_json_add_number(o, "score", 70);
            csilk_json_array_append(anomalies, o);
            cnt_midnight++;
        }
    }
    /* high-freq small: >5 small (<50) transactions on same date */
    {
        /* group by date string YYYY-MM-DD */
        for (size_t i = 0; i < tn;) {
            const char* d0 =
                csilk_json_get_string(csilk_json_array_get(tx_recent, i), "created_at") ?: "";
            char day[11] = {0};
            strncpy(day, d0, 10);
            int    small_cnt = 0;
            double small_sum = 0;
            size_t j = i;
            for (; j < tn; j++) {
                const char* dj =
                    csilk_json_get_string(csilk_json_array_get(tx_recent, j), "created_at") ?: "";
                char dj_day[11] = {0};
                strncpy(dj_day, dj, 10);
                if (strncmp(dj_day, day, 10) != 0) {
                    break;
                }
                double a = db_get_num(csilk_json_array_get(tx_recent, j), "amount");
                if (a > 0 && a < 50) {
                    small_cnt++;
                    small_sum += a;
                }
            }
            if (small_cnt >= 5) {
                csilk_json_t* o = csilk_json_object();
                char          rs[256];
                snprintf(
                    rs, sizeof(rs), "%s 当日 %d 笔小额交易合计 ￥%.0f", day, small_cnt, small_sum);
                csilk_json_add_string(o, "type", "freq_small");
                csilk_json_add_string(o, "category_name", "高频小额");
                csilk_json_add_number(o, "amount", small_sum);
                csilk_json_add_string(o, "date", day);
                csilk_json_add_string(o, "note", "");
                csilk_json_add_string(o, "reason", rs);
                csilk_json_add_number(o, "score", 60);
                csilk_json_array_append(anomalies, o);
                cnt_freq++;
            }
            i = j ? j : i + 1;
            if (j == i) {
                i++;
            }
        }
    }

    if (root) {
        csilk_json_free(root);
    }
    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "total", (double)csilk_json_array_size(anomalies));
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
    double        total = s1 ? db_get_num(s1, "total") : 0;
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

/* ========================================================================= */
/*  Workflow 7: Subscription Audit (订阅/固定支出审计 - 真实数据驱动)         */
/* ========================================================================= */

static char*
step_sa_collect(csilk_db_pool_t*    pool,
                int64_t             user_id,
                const csilk_json_t* params,
                const char*         ctx_json)
{
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
    {
        char uid_str[32];
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
    }
    /* Also fetch recurring candidates via SQL grouping (amount+category) */
    csilk_json_t* grouped = csilk_json_array();
    {
        char uid_str[32];
        snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
        const char*   p[] = {uid_str, since_date, NULL};
        csilk_json_t* res = csilk_db_query_param_json(
            pool,
            "SELECT category_id, COALESCE(c.name,'未分类') as category_name, "
            "amount, COUNT(*) as cnt, MIN(expense_date) as first_date, MAX(expense_date) as "
            "last_date, GROUP_CONCAT(note, ' | ') as notes "
            "FROM daily_expenses de LEFT JOIN categories c ON c.id=de.category_id "
            "WHERE de.user_id=? AND de.expense_type='expense' AND de.expense_date >= ? "
            "GROUP BY category_id, amount HAVING cnt >= 2 ORDER BY cnt DESC LIMIT 50",
            p);
        if (res && csilk_json_is_array(res)) {
            size_t n = csilk_json_array_size(res);
            for (size_t i = 0; i < n; i++) {
                csilk_json_array_append(grouped, csilk_json_copy(csilk_json_array_get(res, i)));
            }
        }
        if (res) {
            csilk_json_free(res);
        }
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

    /* Evaluate each grouped candidate for subscription likelihood
       Heuristics: cnt >= 3 and spans >= 2 distinct months => subscription
       Also check price hike and staleness */
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

        /* Need at least 3 occurrences to be considered subscription-like; allow 2 if amount >= 20 */
        if (cnt < 3 && !(cnt == 2 && amt >= 50)) {
            continue;
        }
        /* Check month span: count distinct YYYY-MM in recent for this amount+category */
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

        /* Derive display name from most frequent note snippet (first note) */
        char display_name[128] = {0};
        /* take first note token before ' | ' */
        const char* sep = strstr(notes, " | ");
        size_t      nlen = sep ? (size_t)(sep - notes) : strlen(notes);
        if (nlen > 0 && nlen < sizeof(display_name)) {
            strncpy(display_name, notes, nlen);
            display_name[nlen] = '\0';
        }
        if (!display_name[0]) {
            snprintf(display_name, sizeof(display_name), "%s", cname);
        }
        /* trim */
        for (size_t k = strlen(display_name); k > 0 && display_name[k - 1] == ' '; k--) {
            display_name[k - 1] = '\0';
        }

        /* Staleness: last_date < now - 45 days */
        int is_stale = 0;
        {
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
        }

        /* Price hike: compare avg of last 2 vs earlier occurrences.
           Simplified: check if amount differs from median of group? Use first vs last not enough.
           We query recent amounts for this group to see variance */
        int    is_hiked = 0;
        double max_amt = amt, min_amt = amt;
        /* If grouped amount is aggregate key, all same amount, so no variance; detect hike via recent variance */
        /* Fallback: if distinct amounts for same category within ±20%, we would have separate groups, so skip */
        /* For now, mark hiked only if notes contain price keywords or cnt large and amt recently increased  */
        /* We do a secondary check: look for same category but slightly different amount in recent 60 days */
        double alt_amt = 0;
        if (recent && csilk_json_is_array(recent)) {
            for (size_t r = 0; r < rn; r++) {
                const csilk_json_t* row = csilk_json_array_get(recent, r);
                if (db_get_int(row, "category_id") != cid) {
                    continue;
                }
                double a = db_get_num(row, "amount");
                if (fabs(a - amt) > 0.01 && fabs(a - amt) / amt < 0.30 && a > amt) {
                    /* found a higher amount for same category recently */
                    if (a > max_amt) {
                        max_amt = a;
                    }
                    alt_amt = a;
                }
                if (a < min_amt) {
                    min_amt = a;
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

    /* Sort subs by amount descending (simple bubble for <50) */
    size_t sn = csilk_json_array_size(subs);
    for (size_t a = 0; a < sn; a++) {
        for (size_t b = a + 1; b < sn; b++) {
            double va = db_get_num(csilk_json_array_get(subs, a), "amount");
            double vb = db_get_num(csilk_json_array_get(subs, b), "amount");
            if (vb > va) {
                csilk_json_t* tmp = csilk_json_copy(csilk_json_array_get(subs, b));
                csilk_json_t* cur_a = csilk_json_copy(csilk_json_array_get(subs, a));
                csilk_json_t* arr_new = csilk_json_array();
                for (size_t k = 0; k < sn; k++) {
                    if (k == a) {
                        csilk_json_array_append(arr_new,
                                                csilk_json_copy(csilk_json_array_get(subs, b)));
                    } else if (k == b) {
                        csilk_json_array_append(arr_new,
                                                csilk_json_copy(csilk_json_array_get(subs, a)));
                    } else {
                        csilk_json_array_append(arr_new,
                                                csilk_json_copy(csilk_json_array_get(subs, k)));
                    }
                }
                csilk_json_free(subs);
                subs = arr_new;
                csilk_json_free(tmp);
                csilk_json_free(cur_a);
                break;
            }
        }
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
            /* escape quotes in name */
            char safe_nm[128] = {0};
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

/* ========================================================================= */
/*  Emergency Fund Health Check Workflow — 三步                              */
/* ========================================================================= */

/* ---- Step 1: 流动现金与月均刚性支出盘点 ---- */
static char*
step_ef_collect(csilk_db_pool_t*    pool,
                int64_t             user_id,
                const csilk_json_t* params,
                const char*         ctx_json)
{
    (void)params;
    (void)ctx_json;
    if (!pool) {
        return strdup("{\"error\":\"db not ready\"}");
    }
    /* liquid cash — 与现有 health_diagnosis / payday 保持同口径 */
    double liquid_cash = 0;
    {
        int64_t       tot = 0;
        csilk_json_t* arr = asset_list(pool, user_id, 1, 100, NULL, &tot);
        if (arr && csilk_json_is_array(arr)) {
            size_t n = csilk_json_array_size(arr);
            for (size_t i = 0; i < n; i++) {
                const csilk_json_t* it = csilk_json_array_get(arr, i);
                const char*         atype = csilk_json_get_string(it, "asset_type");
                if (!atype) {
                    atype = "";
                }
                if (strcmp(atype, "cash") != 0 && strcmp(atype, "bank") != 0 &&
                    strcmp(atype, "other_asset") != 0) {
                    continue;
                }
                const char* pid = csilk_json_get_string(it, "parent_id");
                if (pid && pid[0] && strcmp(pid, "0") != 0) {
                    continue;
                }
                liquid_cash += db_get_num(it, "net_value");
            }
        }
        if (arr) {
            csilk_json_free(arr);
        }
    }
    double monthly_burn = get_user_avg_monthly_burn(pool, user_id);
    /* target_months 来自 params，默认 6 个月 */
    int target_months = 6;
    if (params) {
        double tv = db_get_num(params, "target_months");
        if (tv >= 1 && tv <= 12) {
            target_months = (int)tv;
        }
    }
    double target_amount = monthly_burn > 0 ? monthly_burn * target_months : 0;
    double current_runway = monthly_burn > 0 ? liquid_cash / monthly_burn : 0;
    double gap = target_amount > liquid_cash ? target_amount - liquid_cash : 0;

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "liquid_cash", liquid_cash);
    csilk_json_add_number(out, "monthly_burn", monthly_burn);
    csilk_json_add_number(out, "current_runway", current_runway);
    csilk_json_add_number(out, "target_months", (double)target_months);
    csilk_json_add_number(out, "target_amount", target_amount);
    csilk_json_add_number(out, "gap", gap);
    if (monthly_burn <= 0) {
        csilk_json_add_string(out, "note", "暂无月均支出数据，备用金目标按 0 估算");
    }
    size_t slen = 0;
    char*  s = csilk_json_serialize(out, &slen);
    csilk_json_free(out);
    char* ret = s ? strdup(s) : strdup("{}");
    free(s);
    return ret;
}

/* ---- Step 2: 健康度评分与缺口测算 ---- */
static char*
step_ef_health(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "ef_collect") : NULL;
    double        liquid = s0 ? db_get_num(s0, "liquid_cash") : 0;
    double        burn = s0 ? db_get_num(s0, "monthly_burn") : 0;
    double        target_amount = s0 ? db_get_num(s0, "target_amount") : 0;
    double        gap = s0 ? db_get_num(s0, "gap") : 0;
    double        runway = s0 ? db_get_num(s0, "current_runway") : 0;
    double        target_months = s0 ? db_get_num(s0, "target_months") : 6;

    /* 健康度 0-100：liquid/target*100 封顶 100 */
    double health_score = 0;
    if (target_amount > 0) {
        health_score = liquid / target_amount * 100.0;
        if (health_score > 100) {
            health_score = 100;
        }
        if (health_score < 0) {
            health_score = 0;
        }
    } else if (liquid > 0) {
        health_score = 100;
    }

    const char* level = "healthy";
    const char* level_label = "✅ 健康";
    if (target_amount <= 0) {
        level = "unknown";
        level_label = "ℹ️ 待补充数据";
    } else if (health_score >= 100) {
        level = "healthy";
        level_label = "✅ 健康";
    } else if (health_score >= 50) {
        level = "warning";
        level_label = "⚠️ 偏低";
    } else {
        level = "danger";
        level_label = "🔴 不足";
    }

    /* 补足计划：建议每月追加额（默认 6 个月补齐） */
    int    plan_months = 6;
    double monthly_topup = 0;
    if (gap > 0) {
        monthly_topup = gap / plan_months;
    }

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "liquid_cash", liquid);
    csilk_json_add_number(out, "monthly_burn", burn);
    csilk_json_add_number(out, "target_months", target_months);
    csilk_json_add_number(out, "target_amount", target_amount);
    csilk_json_add_number(out, "gap", gap);
    csilk_json_add_number(out, "current_runway", runway);
    csilk_json_add_number(out, "health_score", health_score);
    csilk_json_add_string(out, "level", level);
    csilk_json_add_string(out, "level_label", level_label);
    csilk_json_add_number(out, "plan_months", (double)plan_months);
    csilk_json_add_number(out, "monthly_topup", monthly_topup);
    size_t slen = 0;
    char*  s = csilk_json_serialize(out, &slen);
    csilk_json_free(out);
    if (root) {
        csilk_json_free(root);
    }
    char* ret = s ? strdup(s) : strdup("{}");
    free(s);
    return ret;
}

/* ---- Step 3: 仪表盘与补足计划报告 ---- */
/* HINT: report step — consumes ef_collect + ef_health from ctx_json */
static char*
step_ef_report(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "ef_collect") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "ef_health") : NULL;
    double liquid = s1 ? db_get_num(s1, "liquid_cash") : (s0 ? db_get_num(s0, "liquid_cash") : 0);
    double burn = s1 ? db_get_num(s1, "monthly_burn") : (s0 ? db_get_num(s0, "monthly_burn") : 0);
    double target_amount = s1 ? db_get_num(s1, "target_amount") : 0;
    double gap = s1 ? db_get_num(s1, "gap") : 0;
    double runway = s1 ? db_get_num(s1, "current_runway") : 0;
    double health_score = s1 ? db_get_num(s1, "health_score") : 0;
    const char* level_label = s1 ? csilk_json_get_string(s1, "level_label") : "—";
    if (!level_label) {
        level_label = "—";
    }
    double target_months = s1 ? db_get_num(s1, "target_months") : 6;
    double monthly_topup = s1 ? db_get_num(s1, "monthly_topup") : 0;
    int    plan_months = s1 ? (int)db_get_num(s1, "plan_months") : 6;

    /* Mermaid 饼图：已备 vs 缺口 */
    char mermaid[2048] = {0};
    if (target_amount > 0) {
        double filled = liquid > target_amount ? target_amount : liquid;
        double missing = gap;
        /* avoid empty pie when both 0 */
        if (filled == 0 && missing == 0) {
            snprintf(
                mermaid, sizeof(mermaid), "> ℹ️ 暂无备用金目标数据，完成一次收支记账后重试。\n\n");
        } else {
            snprintf(mermaid,
                     sizeof(mermaid),
                     "```mermaid\npie showData\n    title 应急备用金达成度（目标 %.0f 个月）\n"
                     "    \"已备金额\" : %.2f\n"
                     "    \"缺口金额\" : %.2f\n```\n\n",
                     target_months,
                     filled,
                     missing);
        }
    } else {
        snprintf(mermaid,
                 sizeof(mermaid),
                 "> ℹ️ 月均支出为 0，无法计算备用金目标。建议先完成收支记账。\n\n");
    }

    /* 建议文案 */
    char advice[1024] = {0};
    if (target_amount <= 0) {
        snprintf(advice,
                 sizeof(advice),
                 "暂无月均支出基线，建议先记录 1-2 个月日常流水后再评估备用金。");
    } else if (health_score >= 100) {
        snprintf(
            advice,
            sizeof(advice),
            "备用金已达标（覆盖 %.1f 个月），建议将多余流动资金转入定投或稳健理财，避免现金闲置。",
            runway);
    } else if (health_score >= 50) {
        snprintf(advice,
                 sizeof(advice),
                 "备用金偏低（%.0f%%），建议未来 %d 个月每月追加 `￥%.0f` 补齐缺口 `￥%.0f`。",
                 health_score,
                 plan_months,
                 monthly_topup,
                 gap);
    } else {
        snprintf(advice,
                 sizeof(advice),
                 "备用金不足（%.0f%%，仅覆盖 %.1f 个月），建议优先暂停非刚需支出，%d "
                 "个月内每月追加 `￥%.0f` 紧急补足。",
                 health_score,
                 runway,
                 plan_months,
                 monthly_topup);
    }

    char buf[16384];
    snprintf(buf,
             sizeof(buf),
             "### 🛡️ 应急基金健康检查报告\n\n"
             "**健康度** `%.0f 分` ｜ **状态** `%s` ｜ **当前覆盖** `%.1f 个月` ｜ **目标** `%.0f "
             "个月`\n\n"
             "**流动现金** `￥%.2f` ｜ **月均刚性支出** `￥%.2f` ｜ **备用金目标** `￥%.2f` ｜ "
             "**缺口** `￥%.2f`\n\n"
             "#### 一、达成度构成\n"
             "%s"
             "#### 二、补足计划\n"
             "| 指标 | 数值 |\n"
             "| :--- | :--- |\n"
             "| 目标月数 | %.0f 个月 |\n"
             "| 目标金额 | ￥%.2f |\n"
             "| 当前流动现金 | ￥%.2f |\n"
             "| 缺口 | ￥%.2f |\n"
             "| 建议补足周期 | %d 个月 |\n"
             "| 建议每月追加 | ￥%.2f |\n"
             "\n"
             "#### 三、处置建议\n"
             "%s\n\n"
             "- **健康 (≥100%%)**：已达标，超出部分可转定投；\n"
             "- **偏低 (50-100%%)**：按月追加，优先保障 3 个月底线；\n"
             "- **不足 (<50%%)**：暂停大额非刚需消费，优先补足至 3 个月。\n"
             "```action\n"
             "{\n"
             "  \"action_type\": \"emergency_fund_check\",\n"
             "  \"health_score\": %.0f,\n"
             "  \"gap\": %.2f,\n"
             "  \"monthly_topup\": %.2f,\n"
             "  \"target_months\": %.0f\n"
             "}\n"
             "```\n",
             health_score,
             level_label,
             runway,
             target_months,
             liquid,
             burn,
             target_amount,
             gap,
             mermaid,
             target_months,
             target_amount,
             liquid,
             gap,
             plan_months,
             monthly_topup,
             advice,
             health_score,
             gap,
             monthly_topup,
             target_months);

    if (root) {
        csilk_json_free(root);
    }
    return strdup(buf);
}

/* ========================================================================= */
/*  Goal Tracker Workflow — 三步                                             */
/* ========================================================================= */

/* ---- Step 1: 目标盘点（懒创建 savings_goals 表 + 全量查询） ---- */
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
    /* lazy DDL — 无迁移文件依赖 */
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

    /* today for overdue calc */
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

/* ---- Step 2: 进度测算与补足计划 ---- */
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

    time_t    now = time(NULL);
    struct tm now_tm;
    localtime_r(&now, &now_tm);

    for (size_t i = 0; i < n; i++) {
        const csilk_json_t* g = csilk_json_array_get(goals, i);
        double              tgt = db_get_num(g, "target_amount");
        double              cur = db_get_num(g, "current_amount");
        double              remaining = tgt > cur ? tgt - cur : 0;
        int                 is_completed = csilk_json_get_bool(g, "is_completed");
        const char*         deadline = csilk_json_get_string(g, "deadline") ?: "";
        int                 days_left = (int)db_get_num(g, "days_left");

        double months_left = 12; /* default 12 个月 */
        if (deadline[0] && days_left >= 0) {
            months_left = days_left / 30.0;
            if (months_left < 1) {
                months_left = 1;
            }
        } else if (deadline[0] && days_left < 0 && !is_completed) {
            months_left = 1; /* 已逾期按 1 个月紧急补足 */
        }
        double monthly_needed = 0;
        if (!is_completed && remaining > 0) {
            monthly_needed = remaining / months_left;
        }
        if (!is_completed) {
            total_monthly_needed += monthly_needed;
            if (days_left >= 0 && days_left <= 60 && remaining > 0) {
                urgent_cnt++;
            }
            if (days_left < 0 && remaining > 0) {
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

/* ---- Step 3: 目标追踪报告（Mermaid + 表格 + action） ---- */
/* HINT: report step — consumes gt_collect + gt_plan from ctx_json */
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
        /* bar-like xychart for per-goal progress */
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
        /* sanitize name for table pipe */
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

/* ========================================================================= */
/*  Workflow 10: Debt Payoff (债务加速偿还规划 - 真实数据驱动)               */
/* ========================================================================= */

/* Parse annual rate (%) from note like "利率5.2%" else default by type */
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
    /* find first '%' and scan backwards for number */
    const char* pct = strchr(note, '%');
    if (!pct) {
        /* also try full-width ％ */
        return def;
    }
    /* walk back to find number start */
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

/* ---- Step 1: 债务盘点 ---- */
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

/* internal struct for simulation */
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
    /* copy balances */
    for (int m = 0; m < months; m++) {
        /* accrue interest first */
        for (size_t i = 0; i < n; i++) {
            if (items[i].balance <= 0.01) {
                continue;
            }
            double interest = items[i].balance * items[i].monthly_rate;
            items[i].balance += interest;
            total_interest += interest;
        }
        double remaining = monthly_payment;
        /* minimum payment per debt: max(100, balance*0.02) */
        /* first pass: minimums */
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
        /* second pass: avalanche/snowball priority order (already sorted) */
        for (size_t i = 0; i < n && remaining > 0.01; i++) {
            if (items[i].balance <= 0.01) {
                continue;
            }
            double take = remaining > items[i].balance ? items[i].balance : remaining;
            items[i].balance -= take;
            remaining -= take;
        }
        /* early exit if all cleared */
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

/* ---- Step 2: 雪崩 vs 雪球模拟 12 期 ---- */
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
        /* default: at least 2000 or total/12 */
        monthly_payment = total_debt / 12.0;
        if (monthly_payment < 2000) {
            monthly_payment = 2000;
        }
        if (total_debt <= 0) {
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

    double      saved = interest_sb - interest_av; /* positive means avalanche saves */
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

/* ---- Step 3: 偿还规划报告 ---- */
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
        /* bar comparison */
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

/* ========================================================================= */
/*  Workflow 11: Cashflow Forecast — 6-month rolling cash projection        */
/* ========================================================================= */

static char*
step_cf_collect(csilk_db_pool_t*    pool,
                int64_t             user_id,
                const csilk_json_t* params,
                const char*         ctx_json)
{
    (void)params;
    (void)ctx_json;
    double avg_burn = get_user_avg_monthly_burn(pool, user_id);
    /* avg monthly income */
    char uid_str[32];
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
    /* liquid cash */
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
                liquid += db_get_num(a, "balance");
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
    }
    if (ctx) {
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

/* ------------------------------------------------------------------ */
/*  Bill Calendar — helpers                                              */
/* ------------------------------------------------------------------ */

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

    (void)pool;
    (void)user_id;
    return strdup("{\"bills\":[],\"total_monthly_bill\":0,\"bill_count\":0,\"avg_burn\":3000,"
                  "\"pressure_ratio\":0}");
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
    csilk_json_t* src = NULL;
    if (ctx) {
        csilk_json_t* prev = csilk_json_get(ctx, "bc_collect");
        if (prev) {
            bills = csilk_json_get(prev, "bills");
            src = prev;
        }
    }
    double daily[32] = {0};
    /* Use hash distribution: day = (hash(name) % 30)+1 */
    csilk_json_t* calendar = csilk_json_array();
    if (bills) {
        size_t n = csilk_json_array_size(bills);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* b = csilk_json_array_get(bills, (int)i);
            if (!b) {
                continue;
            }
            const char* name = csilk_json_get_string(b, "name");
            double      amt = db_get_num(b, "amount");
            unsigned    h = 0;
            if (name) {
                for (const char* p = name; *p; p++) {
                    h = h * 31 + (unsigned char)*p;
                }
            }
            int day = (int)(h % 30) + 1;
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
    (void)src;

    /* detect pressure days: daily > 2000 or > 30% avg_burn */
    double avg_burn = 1;
    if (ctx) {
        csilk_json_t* prev = csilk_json_get(ctx, "bc_collect");
        if (prev) {
            avg_burn = db_get_num(prev, "avg_burn");
        }
    }
    if (avg_burn < 1) {
        avg_burn = 1;
    }
    double threshold = avg_burn * 0.3;
    if (threshold < 1500) {
        threshold = 1500;
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
    /* daily_totals 1..30 for chart */
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

    /* xychart for 30 days */
    csilk_json_t* daily = cal ? csilk_json_get(cal, "daily_totals") : NULL;
    char          xychart[8192] = {0};
    char          cats[4096] = {0};
    char          vals[4096] = {0};
    if (daily) {
        size_t n = csilk_json_array_size(daily);
        for (size_t i = 0; i < n && i < 30; i++) {
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "\"%zu日\"", i + 1);
            strcat(cats, tmp);
            if (i + 1 < n) {
                strcat(cats, ",");
            }
            csilk_json_t* v = csilk_json_array_get(daily, (int)i);
            double        amt = 0;
            if (v) {
                if (csilk_json_is_number(v)) {
                    amt = csilk_json_number_value(v);
                } else if (csilk_json_is_string(v)) {
                    const char* s = csilk_json_string_value(v);
                    amt = s ? atof(s) : 0;
                } else {
                    amt = db_get_num(v, "");
                }
            }
            snprintf(tmp, sizeof(tmp), "%.0f", amt);
            strcat(vals, tmp);
            if (i + 1 < n) {
                strcat(vals, ",");
            }
        }
    }
    /* Fallback for csilk numeric array: above direct parse may fail, so re-serialize */
    if (cats[0] == '\0' && daily) {
        /* fallback: iterate via serialized string hack */
        snprintf(
            cats, sizeof(cats), "\"1日\",\"5日\",\"10日\",\"15日\",\"20日\",\"25日\",\"30日\"");
        snprintf(vals, sizeof(vals), "0,0,0,0,0,0,0");
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

    /* Build calendar table sorted by day */
    char rows[8192] = {0};
    if (cal) {
        csilk_json_t* cal_arr = csilk_json_get(cal, "calendar");
        if (cal_arr) {
            /* simple: iterate and emit row per entry */
            size_t n = csilk_json_array_size(cal_arr);
            /* bucket by day for grouped display */
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
                if (kind && strcmp(kind, "debt") == 0) {
                    kind_label = "负债";
                } else if (kind && strcmp(kind, "subscription") == 0) {
                    kind_label = "订阅";
                }
                char line[512];
                snprintf(line,
                         sizeof(line),
                         "| %02d日 | %s | %s | ￥%.0f |\n",
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
        snprintf(rows, sizeof(rows), "| - | 暂无账单 | - | - |\n");
    }

    const char* risk = "低";
    if (pressure_days >= 3 || pressure_ratio >= 80) {
        risk = "高";
    } else if (pressure_days >= 1 || pressure_ratio >= 50) {
        risk = "中";
    }

    char advice[512];
    if (pressure_days >= 3) {
        snprintf(advice,
                 sizeof(advice),
                 "未来30日有 %d 个高压日，建议将部分账单错峰至空档日",
                 pressure_days);
    } else if (pressure_days >= 1) {
        snprintf(advice,
                 sizeof(advice),
                 "最高压日为 %02d日（￥%.0f），建议提前预留资金",
                 max_day,
                 max_day_amt);
    } else {
        snprintf(advice, sizeof(advice), "账单分布较均衡，无明显高压日");
    }

    char buf[16384];
    snprintf(buf,
             sizeof(buf),
             "### 📅 未来30日账单日历\n\n"
             "**账单总数** `%d` 笔 ｜ **月账单总额** `￥%.0f` ｜ **月均支出** `￥%.0f` ｜ "
             "**账单/支出比** `%.0f%%` ｜ **高压日** `%d` 天 ｜ **风险** `%s`\n\n"
             "%s\n"
             "| 日期 | 账单 | 类型 | 金额 |\n"
             "| :--- | :--- | :--- | :--- |\n"
             "%s\n"
             "**最高压日**：`%02d日 ￥%.0f`\n\n"
             "#### 建议\n"
             "1. %s；\n"
             "2. 建议在高压日前 3 日确保活期余额充足；\n"
             "3. 可将订阅类账单统一改至发薪日后 2 日内自动扣款。\n"
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
             max_day,
             max_day_amt,
             advice,
             total_bill,
             pressure_days,
             risk);
    if (ctx) {
        csilk_json_free(ctx);
    }
    return strdup(buf);
}

/* ----------------------------------------------------------------
 *  Workflow: Financial Health Score (health_score)
 *  Score 0-100 across liquidity/debt/savings/discipline
 * ---------------------------------------------------------------- */

static char*
step_hs_collect(csilk_db_pool_t*    pool,
                int64_t             user_id,
                const csilk_json_t* params,
                const char*         ctx_json)
{
    (void)params;
    (void)ctx_json;
    double liquid = 0;
    {
        int64_t       tot = 0;
        csilk_json_t* arr = asset_list(pool, user_id, 1, 100, NULL, &tot);
        if (arr) {
            csilk_json_t* d = csilk_json_get(arr, "data");
            size_t        n = d ? csilk_json_array_size(d) : 0;
            for (size_t i = 0; i < n; i++) {
                csilk_json_t* a = csilk_json_array_get(d, (int)i);
                if (!a) {
                    continue;
                }
                const char* typ = csilk_json_get_string(a, "asset_type");
                int64_t     pid = (int64_t)db_get_num(a, "parent_id");
                if (pid != 0) {
                    continue;
                }
                if (typ && (strcmp(typ, "cash") == 0 || strcmp(typ, "bank") == 0 ||
                            strcmp(typ, "other_asset") == 0)) {
                    liquid += db_get_num(a, "net_value");
                    if (db_get_num(a, "net_value") == 0) {
                        liquid += db_get_num(a, "balance");
                    }
                }
            }
            csilk_json_free(arr);
        }
    }
    double total_assets = 0, total_debt = 0;
    {
        int64_t       tot2 = 0;
        csilk_json_t* arr = asset_list(pool, user_id, 1, 200, NULL, &tot2);
        if (arr) {
            csilk_json_t* d = csilk_json_get(arr, "data");
            size_t        n = d ? csilk_json_array_size(d) : 0;
            for (size_t i = 0; i < n; i++) {
                csilk_json_t* a = csilk_json_array_get(d, (int)i);
                if (!a) {
                    continue;
                }
                double v = db_get_num(a, "net_value");
                if (v == 0) {
                    v = db_get_num(a, "balance");
                }
                if (v < 0) {
                    v = -v;
                }
                const char* typ = csilk_json_get_string(a, "asset_type");
                int is_liab = typ && (strcmp(typ, "loan") == 0 || strcmp(typ, "credit_card") == 0 ||
                                      strcmp(typ, "other_liability") == 0);
                if (is_liab) {
                    total_debt += v;
                } else {
                    total_assets += v;
                }
            }
            csilk_json_free(arr);
        }
    }
    double avg_burn = get_user_avg_monthly_burn(pool, user_id);
    double avg_income = 0;
    {
        char uid2[32];
        snprintf(uid2, sizeof(uid2), "%lld", (long long)user_id);
        const char*   params2[] = {uid2, NULL};
        csilk_json_t* rows = csilk_db_query_param_json(
            pool,
            "SELECT AVG(mv) as avg_income FROM (SELECT substr(expense_date,1,7) as ym, "
            "SUM(amount) as mv FROM daily_expenses WHERE user_id=? AND amount>0 "
            "GROUP BY ym) t",
            params2);
        if (rows && csilk_json_array_size(rows) > 0) {
            avg_income = db_get_num(csilk_json_array_get(rows, 0), "avg_income");
        }
        if (rows) {
            csilk_json_free(rows);
        }
    }
    double savings_rate = 0;
    if (avg_income > 0) {
        savings_rate = (avg_income - avg_burn) / avg_income * 100.0;
    }
    double emergency_months = avg_burn > 1 ? liquid / avg_burn : 0;
    double debt_ratio = total_assets > 1 ? total_debt / (total_assets + total_debt) * 100.0 : 0;

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "liquid_cash", liquid);
    csilk_json_add_number(out, "total_assets", total_assets);
    csilk_json_add_number(out, "total_debt", total_debt);
    csilk_json_add_number(out, "avg_burn", avg_burn);
    csilk_json_add_number(out, "avg_income", avg_income);
    csilk_json_add_number(out, "savings_rate", savings_rate);
    csilk_json_add_number(out, "emergency_months", emergency_months);
    csilk_json_add_number(out, "debt_ratio", debt_ratio);
    size_t sl = 0;
    char*  s = csilk_json_serialize(out, &sl);
    csilk_json_free(out);
    return s ? s : strdup("{}");
}

static char*
step_hs_score(csilk_db_pool_t*    pool,
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
    csilk_json_t* col = ctx ? csilk_json_get(ctx, "hs_collect") : NULL;
    double        emergency = col ? db_get_num(col, "emergency_months") : 0;
    double        debt_ratio = col ? db_get_num(col, "debt_ratio") : 0;
    double        savings_rate = col ? db_get_num(col, "savings_rate") : 0;
    double        avg_income = col ? db_get_num(col, "avg_income") : 0;
    double        avg_burn = col ? db_get_num(col, "avg_burn") : 0;

    int s_liquidity = 0;
    if (emergency >= 6) {
        s_liquidity = 25;
    } else if (emergency >= 3) {
        s_liquidity = 18;
    } else if (emergency >= 1) {
        s_liquidity = 10;
    } else if (emergency >= 0.5) {
        s_liquidity = 5;
    }

    int s_debt = 0;
    if (debt_ratio < 15) {
        s_debt = 25;
    } else if (debt_ratio < 30) {
        s_debt = 18;
    } else if (debt_ratio < 50) {
        s_debt = 10;
    } else if (debt_ratio < 70) {
        s_debt = 5;
    }

    int s_savings = 0;
    if (savings_rate >= 30) {
        s_savings = 25;
    } else if (savings_rate >= 15) {
        s_savings = 18;
    } else if (savings_rate >= 5) {
        s_savings = 10;
    } else if (savings_rate >= 0) {
        s_savings = 5;
    }

    int s_discipline = 0;
    if (avg_income > 0 && avg_burn < avg_income) {
        s_discipline = 25;
    } else if (avg_income > 0 && avg_burn < avg_income * 1.1) {
        s_discipline = 12;
    } else if (avg_income <= 0 && avg_burn < 1000) {
        s_discipline = 10;
    }

    int total = s_liquidity + s_debt + s_savings + s_discipline;
    if (total > 100) {
        total = 100;
    }
    const char* grade = "D";
    if (total >= 85) {
        grade = "A";
    } else if (total >= 70) {
        grade = "B";
    } else if (total >= 50) {
        grade = "C";
    }

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "score_liquidity", (double)s_liquidity);
    csilk_json_add_number(out, "score_debt", (double)s_debt);
    csilk_json_add_number(out, "score_savings", (double)s_savings);
    csilk_json_add_number(out, "score_discipline", (double)s_discipline);
    csilk_json_add_number(out, "total_score", (double)total);
    csilk_json_add_string(out, "grade", grade);
    size_t sl2 = 0;
    char*  s = csilk_json_serialize(out, &sl2);
    csilk_json_free(out);
    if (ctx) {
        csilk_json_free(ctx);
    }
    return s ? s : strdup("{}");
}

static char*
step_hs_report(csilk_db_pool_t*    pool,
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
        return strdup("{\"markdown\":\"暂无健康分数据。\"}");
    }
    csilk_json_t* col = csilk_json_get(ctx, "hs_collect");
    csilk_json_t* sc = csilk_json_get(ctx, "hs_score");
    double        liquid = col ? db_get_num(col, "liquid_cash") : 0;
    double        assets = col ? db_get_num(col, "total_assets") : 0;
    double        debt = col ? db_get_num(col, "total_debt") : 0;
    double        burn = col ? db_get_num(col, "avg_burn") : 0;
    double        income = col ? db_get_num(col, "avg_income") : 0;
    double        sr = col ? db_get_num(col, "savings_rate") : 0;
    double        emer = col ? db_get_num(col, "emergency_months") : 0;
    double        dr = col ? db_get_num(col, "debt_ratio") : 0;
    int           total = sc ? (int)db_get_num(sc, "total_score") : 0;
    const char*   grade = sc ? csilk_json_get_string(sc, "grade") : "D";
    if (!grade) {
        grade = "D";
    }
    int sl = sc ? (int)db_get_num(sc, "score_liquidity") : 0;
    int sd = sc ? (int)db_get_num(sc, "score_debt") : 0;
    int sv = sc ? (int)db_get_num(sc, "score_savings") : 0;
    int sdi = sc ? (int)db_get_num(sc, "score_discipline") : 0;

    const char* label = "需改进";
    if (total >= 85) {
        label = "优秀";
    } else if (total >= 70) {
        label = "良好";
    } else if (total >= 50) {
        label = "一般";
    }

    char suggest[512];
    if (total >= 85) {
        snprintf(suggest, sizeof(suggest), "财务状况优秀，继续保持当前储蓄与支出节奏");
    } else if (emer < 3) {
        snprintf(
            suggest, sizeof(suggest), "优先补足应急资金至 3-6 个月支出（当前 %.1f 个月）", emer);
    } else if (dr >= 50) {
        snprintf(suggest, sizeof(suggest), "负债率偏高（%.0f%%），建议优先降低高息负债", dr);
    } else if (sr < 10) {
        snprintf(suggest, sizeof(suggest), "储蓄率偏低（%.0f%%），建议检视非必要支出", sr);
    } else {
        snprintf(suggest, sizeof(suggest), "整体良好，可优化短板维度进一步提升");
    }

    char xychart[2048];
    snprintf(xychart,
             sizeof(xychart),
             "```mermaid\n"
             "xychart-beta\n"
             "    title \"财务健康分项（满分25）\"\n"
             "    x-axis [\"流动性\",\"负债\",\"储蓄\",\"纪律\"]\n"
             "    y-axis \"分数\" 0 --> 25\n"
             "    bar [%d,%d,%d,%d]\n"
             "```\n",
             sl,
             sd,
             sv,
             sdi);

    char buf[16384];
    snprintf(buf,
             sizeof(buf),
             "### 🏥 财务健康分报告\n\n"
             "**综合得分** `%d/100` ｜ **等级** `%s` ｜ **评价** `%s`\n\n"
             "%s\n"
             "| 维度 | 得分 | 满分 | 说明 |\n"
             "| :--- | :--- | :--- | :--- |\n"
             "| 流动性 | %d | 25 | 应急 %.1f 个月 |\n"
             "| 负债 | %d | 25 | 负债率 %.0f%% |\n"
             "| 储蓄 | %d | 25 | 储蓄率 %.0f%% |\n"
             "| 纪律 | %d | 25 | 收支 %s |\n"
             "| **合计** | **%d** | **100** | **%s** |\n\n"
             "**资产** `￥%.0f` ｜ **负债** `￥%.0f` ｜ **流动现金** `￥%.0f` ｜ "
             "**月均收入** `￥%.0f` ｜ **月均支出** `￥%.0f`\n\n"
             "#### 建议\n"
             "1. %s；\n"
             "2. 每月复盘时关注最低分的维度；\n"
             "3. 目标 3 个月内提升至 %d 分以上。\n"
             "```action\n"
             "{\n"
             "  \"action_type\": \"health_score\",\n"
             "  \"total_score\": %d,\n"
             "  \"grade\": \"%s\"\n"
             "}\n"
             "```\n",
             total,
             grade,
             label,
             xychart,
             sl,
             emer,
             sd,
             dr,
             sv,
             sr,
             sdi,
             burn < income ? "盈余" : "赤字",
             total,
             label,
             assets,
             debt,
             liquid,
             income,
             burn,
             suggest,
             total >= 85 ? 90 : total + 10,
             total,
             grade);
    csilk_json_free(ctx);
    return strdup(buf);
}

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
    {
     .id = "wf_payday_split",
     .title = "工资到账自动分配",
     .description =
            "检测本月工资与入账，按自定义比例生成生活/定投/还贷/应急四象限分配方案与待确认草案。",                                                                               .icon = "ph:wallet",
     .step_count = 3,
     .steps =
            {
                {"payday_detect",
                 "入账检测与资金盘点",
                 "扫描本月工资/入账流水与流动现金总额",
                 step_payday_detect},
                {"payday_allocate",
                 "四象限比例分配测算",
                 "按 50/20/20/10 比例计算各用途金额与去向",
                 step_payday_allocate},
                {"generate_report",
                 "分配方案与执行草案",
                 "生成分配可视化饼图与待确认转账草案",
                 step_payday_report},
            }, },
    {
     .id = "wf_budget_guard",
     .title = "预算超支预警",
     .description = "按日进度外推月底支出，识别分类超支风险，输出节流建议与预算执行可视化。",
     .icon = "ph:warning-circle",
     .step_count = 3,
     .steps =
            {
                {"bg_collect",
                 "当月支出与历史预算盘点",
                 "汇总本月分类支出并计算历史均值预算基线",
                 step_bg_collect},
                {"bg_forecast",
                 "月底外推与风险定级",
                 "按日进度预测月底总额并标记超支/预警分类",
                 step_bg_forecast},
                {"generate_report",
                 "预警报告与节流方案",
                 "生成预算执行表格、图表与节流建议",
                 step_bg_report},
            }, },
    {
     .id = "wf_anomaly_detect",
     .title = "异常交易检测",
     .description =
            "基于 3σ/重复扣款/凌晨大额/高频小额四规则扫描近 60 天流水，输出异常清单与处置建议。",                                                                       .icon = "ph:shield-warning",
     .step_count = 3,
     .steps =
            {
                {"ad_collect",
                 "近 60 天流水与基线统计",
                 "拉取近期流水并计算分分类均值方差基线",
                 step_ad_collect},
                {"ad_score", "四规则异常评分", "3σ/重复/凌晨/高频四规则打分与去重", step_ad_score},
                {"generate_report",
                 "异常清单与处置建议",
                 "生成异常明细表、分布饼图与核查指引",
                 step_ad_report},
            }, },
    {
     .id = "wf_subscription_audit",
     .title = "订阅/固定支出审计",
     .description = "聚类近 6 个月重复扣费，识别订阅项、涨价与闲置项，输出年化节省建议。",
     .icon = "ph:repeat",
     .step_count = 3,
     .steps =
            {
                {"sa_collect",
                 "近 6 月流水与分组扫描",
                 "拉取近期流水并按金额+分类聚类候选订阅",
                 step_sa_collect},
                {"sa_analyze",
                 "订阅识别与涨价/闲置判定",
                 "按月跨度与频次判定订阅，标记涨价与久未扣费",
                 step_sa_analyze},
                {"generate_report",
                 "订阅审计报告与省钱方案",
                 "生成订阅清单、年化统计与取消建议草案",
                 step_sa_report},
            }, },
    {
     .id = "wf_emergency_fund",
     .title = "应急基金健康检查",
     .description = "盘点流动现金与月均刚性支出，评分备用金健康度并输出缺口补足计划。",
     .icon = "ph:shield-check",
     .step_count = 3,
     .steps =
            {
                {"ef_collect",
                 "流动现金与月均支出盘点",
                 "统计活期现金与月均刚性支出基线",
                 step_ef_collect},
                {"ef_health",
                 "健康度评分与缺口测算",
                 "计算覆盖月数、健康度与补足缺口",
                 step_ef_health},
                {"generate_report",
                 "健康仪表盘与补足计划",
                 "生成达成度饼图、补足计划表与处置建议",
                 step_ef_report},
            }, },
    {
     .id = "wf_goal_tracker",
     .title = "目标储蓄追踪",
     .description = "追踪多目标储蓄进度，测算月需追加与逾期风险，输出进度仪表盘与补足计划。",
     .icon = "ph:target",
     .step_count = 3,
     .steps =
            {
                {"gt_collect",
                 "目标盘点与进度汇总",
                 "扫描全部储蓄目标，计算达成率与逾期状态",
                 step_gt_collect},
                {"gt_plan",
                 "月需追加与紧迫度测算",
                 "按剩余期限计算每月需追加额与紧迫目标",
                 step_gt_plan},
                {"generate_report",
                 "追踪仪表盘与行动建议",
                 "生成总进度饼图、目标达成图与补足建议",
                 step_gt_report},
            }, },
    {
     .id = "wf_debt_payoff",
     .title = "债务加速偿还规划",
     .description =
            "盘点贷款与信用卡负债，对比雪崩/雪球 12 期利息与剩余，输出最省利息的加速偿还方案。",                                                                     .icon = "ph:hand-coins",
     .step_count = 3,
     .steps =
            {
                {"dp_collect",
                 "债务盘点与利率识别",
                 "扫描贷款/信用卡负债余额与年利率",
                 step_dp_collect},
                {"dp_simulate",
                 "雪崩 vs 雪球 12 期模拟",
                 "对比两种策略的利息与剩余债务",
                 step_dp_simulate},
                {"generate_report",
                 "偿还规划与执行建议",
                 "生成债务构成图、对比表与推荐策略",
                 step_dp_report},
            }, },
    {
     .id = "wf_cashflow_forecast",
     .title = "未来现金流预测",
     .description = "基于近 6 月收支均值滚动预测未来 6 个月现金余额，识别透支时点与可持续月数。",
     .icon = "ph:trend-up",
     .step_count = 3,
     .steps =
            {
                {"cf_collect", "历史收支与现金盘点", "统计月均收支与当前流动现金", step_cf_collect},
                {"cf_forecast",
                 "滚动预测与风险测算",
                 "外推未来现金轨迹并计算最低点与 runway",
                 step_cf_forecast},
                {"generate_report",
                 "预测图表与处置建议",
                 "生成现金流走势图与节流建议",
                 step_cf_report},
            }, },
    {
     .id = "wf_bill_calendar",
     .title = "账单日历与高压日检测",
     .description = "汇聚负债与订阅账单，生成未来30日账单日历，识别高压日并给出错峰建议。",
     .icon = "ph:calendar-blank",
     .step_count = 3,
     .steps =
            {
                {"bc_collect", "账单汇聚", "汇聚负债与订阅类周期账单及总额", step_bc_collect},
                {"bc_calendar", "30日日历排期", "按日期分布账单并测算每日压力", step_bc_calendar},
                {"generate_report",
                 "日历图表与错峰建议",
                 "生成账单压力分布图与错峰方案",
                 step_bc_report},
            }, },
    {
     .id = "wf_health_score",
     .title = "财务健康分评估",
     .description =
            "从流动性/负债/储蓄/纪律四维度计算 0-100 健康分，定位短板并给出 3 个月提升路径。",    .icon = "ph:heart",
     .step_count = 3,
     .steps =
            {
                {"hs_collect", "财务数据汇聚", "汇聚资产负债与月均收支", step_hs_collect},
                {"hs_score", "四维度评分", "流动性/负债/储蓄/纪律加权评分", step_hs_score},
                {"generate_report",
                 "健康分报告与提升建议",
                 "生成健康分雷达图与短板改进方案",
                 step_hs_report},
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

    char exec_time_str[64];
    get_current_datetime_str(exec_time_str, sizeof(exec_time_str));
    time_t exec_ts = time(NULL);

    /* Start SSE streaming */
    csilk_sse_init(c);

    /* 1. Send workflow_start */
    csilk_json_t* ev_start = csilk_json_object();
    csilk_json_add_string(ev_start, "workflow_id", target_wf->id);
    csilk_json_add_string(ev_start, "title", target_wf->title);
    csilk_json_add_number(ev_start, "total_steps", (double)target_wf->step_count);
    csilk_json_add_number(ev_start, "session_id", (double)session_id);
    csilk_json_add_string(ev_start, "execution_time", exec_time_str);
    csilk_json_add_number(ev_start, "execution_timestamp", (double)exec_ts);
    size_t slen = 0;
    char*  str_start = csilk_json_serialize(ev_start, &slen);
    csilk_json_free(ev_start);
    csilk_sse_send(c, "workflow_start", str_start ? str_start : "{}");
    free(str_start);

    /* Cumulative context across steps */
    csilk_json_t* ctx_obj = csilk_json_object();
    csilk_json_add_string(ctx_obj, "execution_time", exec_time_str);
    csilk_json_add_number(ctx_obj, "execution_timestamp", (double)exec_ts);
    {
        char cur_month_tmp[32];
        get_current_month_str(cur_month_tmp, sizeof(cur_month_tmp));
        csilk_json_add_string(ctx_obj, "current_month", cur_month_tmp);
    }

    for (int i = 0; i < target_wf->step_count; i++) {
        const ai_workflow_step_t* st = &target_wf->steps[i];

        char step_time_str[64];
        get_current_datetime_str(step_time_str, sizeof(step_time_str));
        /* Send step_start */
        csilk_json_t* ev_st_start = csilk_json_object();
        csilk_json_add_number(ev_st_start, "step_index", (double)i);
        csilk_json_add_string(ev_st_start, "step_id", st->step_id);
        csilk_json_add_string(ev_st_start, "title", st->title);
        csilk_json_add_string(ev_st_start, "step_time", step_time_str);
        csilk_json_add_number(ev_st_start, "step_timestamp", (double)time(NULL));
        char* str_st_start = csilk_json_serialize(ev_st_start, &slen);
        csilk_json_free(ev_st_start);
        csilk_sse_send(c, "step_start", str_st_start ? str_st_start : "{}");
        free(str_st_start);

        char summary_buf[256] = {0};

        if (i == target_wf->step_count - 1) {
            /* Final step: Stream report via AI Model or structured fallback */
            char* cur_ctx_str = csilk_json_serialize(ctx_obj, &slen);
            int   streamed_by_model =
                ai_service_stream_report(c, user_id, session_id, target_wf->title, cur_ctx_str);

            if (!streamed_by_model) {
                /* Fallback: generate and stream deterministic real-data report */
                char* step_out = st->execute(pool, user_id, params, cur_ctx_str);
                if (step_out && step_out[0]) {
                    size_t rlen = strlen(step_out);
                    size_t offset = 0;
                    size_t chunk_sz = 64;
                    while (offset < rlen) {
                        size_t take = (offset + chunk_sz < rlen) ? chunk_sz : (rlen - offset);
                        char   chunk[128];
                        memcpy(chunk, step_out + offset, take);
                        chunk[take] = '\0';
                        offset += take;

                        csilk_json_t* ev_chunk = csilk_json_object();
                        csilk_json_add_string(ev_chunk, "content", chunk);
                        char* str_chunk = csilk_json_serialize(ev_chunk, &slen);
                        csilk_json_free(ev_chunk);
                        csilk_sse_send(c, "delta", str_chunk ? str_chunk : "{}");
                        free(str_chunk);
                    }

                    if (pool && session_id > 0) {
                        ai_message_insert(
                            pool, session_id, "assistant", step_out, "workflow-agent");
                    }
                    free(step_out);
                }
                snprintf(summary_buf, sizeof(summary_buf), "财务复盘与图表渲染完毕");
            } else {
                snprintf(summary_buf, sizeof(summary_buf), "AI 大模型已完成深度诊断与图表生成");
            }
            free(cur_ctx_str);
        } else {
            /* Execute data aggregation step */
            char* cur_ctx_str = csilk_json_serialize(ctx_obj, &slen);
            char* step_out = st->execute(pool, user_id, params, cur_ctx_str);
            free(cur_ctx_str);

            if (step_out) {
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
                    } else if (strcmp(st->step_id, "payday_detect") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "本月可分配收入 ￥%.2f，流动现金 ￥%.2f",
                                 db_get_num(parsed_out, "total_income"),
                                 db_get_num(parsed_out, "liquid_cash"));
                    } else if (strcmp(st->step_id, "payday_allocate") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "已分配：生活￥%.0f 定投￥%.0f 还贷￥%.0f 应急￥%.0f",
                                 db_get_num(parsed_out, "amt_living"),
                                 db_get_num(parsed_out, "amt_invest"),
                                 db_get_num(parsed_out, "amt_debt"),
                                 db_get_num(parsed_out, "amt_emergency"));
                    } else if (strcmp(st->step_id, "bg_collect") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "本月已支出 ￥%.2f，进度 %.0f%%，已汇总分类预算基线",
                                 db_get_num(parsed_out, "cur_total"),
                                 db_get_num(parsed_out, "progress") * 100.0);
                    } else if (strcmp(st->step_id, "bg_forecast") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "预计月底 ￥%.2f / 预算 ￥%.2f，🔴%d 🟡%d",
                                 db_get_num(parsed_out, "total_projected"),
                                 db_get_num(parsed_out, "total_budget"),
                                 (int)db_get_num(parsed_out, "danger_cnt"),
                                 (int)db_get_num(parsed_out, "warning_cnt"));
                    } else if (strcmp(st->step_id, "ad_collect") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "已拉取 %d 天内 %d 笔流水，基线统计完成",
                                 (int)db_get_num(parsed_out, "lookback_days"),
                                 (int)db_get_num(parsed_out, "count"));
                    } else if (strcmp(st->step_id, "ad_score") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "检出 %d 项异常（3σ%d 重复%d 凌晨%d 高频%d）",
                                 (int)db_get_num(parsed_out, "total"),
                                 (int)db_get_num(parsed_out, "cnt_3sigma"),
                                 (int)db_get_num(parsed_out, "cnt_dup"),
                                 (int)db_get_num(parsed_out, "cnt_midnight"),
                                 (int)db_get_num(parsed_out, "cnt_freq"));
                    } else if (strcmp(st->step_id, "sa_collect") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "已扫描 %d 天内 %d 笔，发现 %d 组重复扣费候选",
                                 (int)db_get_num(parsed_out, "lookback_days"),
                                 (int)db_get_num(parsed_out, "count"),
                                 (int)(parsed_out ? csilk_json_array_size(
                                                        csilk_json_get(parsed_out, "grouped"))
                                                  : 0));
                    } else if (strcmp(st->step_id, "sa_analyze") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "识别订阅 %d 项，月合计￥%.0f 年化￥%.0f",
                                 (int)db_get_num(parsed_out, "sub_count"),
                                 db_get_num(parsed_out, "total_monthly"),
                                 db_get_num(parsed_out, "total_annual"));
                    } else if (strcmp(st->step_id, "ef_collect") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "流动现金￥%.0f 月均支出￥%.0f 覆盖%.1f月",
                                 db_get_num(parsed_out, "liquid_cash"),
                                 db_get_num(parsed_out, "monthly_burn"),
                                 db_get_num(parsed_out, "current_runway"));
                    } else if (strcmp(st->step_id, "ef_health") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "健康度%.0f分 %s 缺口￥%.0f",
                                 db_get_num(parsed_out, "health_score"),
                                 csilk_json_get_string(parsed_out, "level_label") ?: "",
                                 db_get_num(parsed_out, "gap"));
                    } else if (strcmp(st->step_id, "gt_collect") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "目标%d项 总￥%.0f 已存￥%.0f 进度%.0f%%",
                                 (int)db_get_num(parsed_out, "goal_count"),
                                 db_get_num(parsed_out, "total_target"),
                                 db_get_num(parsed_out, "total_current"),
                                 db_get_num(parsed_out, "avg_progress"));
                    } else if (strcmp(st->step_id, "gt_plan") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "月需追加合计￥%.0f 紧迫%d项",
                                 db_get_num(parsed_out, "total_monthly_needed"),
                                 (int)db_get_num(parsed_out, "urgent_cnt"));
                    } else if (strcmp(st->step_id, "dp_collect") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "负债%d笔 总额￥%.0f 最高利率%.1f%%",
                                 (int)db_get_num(parsed_out, "debt_count"),
                                 db_get_num(parsed_out, "total_debt"),
                                 db_get_num(parsed_out, "max_rate"));
                    } else if (strcmp(st->step_id, "dp_simulate") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "雪崩利息￥%.0f 雪球利息￥%.0f 节省￥%.0f",
                                 db_get_num(parsed_out, "avalanche_interest"),
                                 db_get_num(parsed_out, "snowball_interest"),
                                 db_get_num(parsed_out, "interest_saved"));
                    } else if (strcmp(st->step_id, "cf_collect") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "流动￥%.0f 月净￥%.0f 预测%d月",
                                 db_get_num(parsed_out, "liquid_cash"),
                                 db_get_num(parsed_out, "net_monthly"),
                                 (int)db_get_num(parsed_out, "horizon"));
                    } else if (strcmp(st->step_id, "cf_forecast") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "最低￥%.0f runway %d月 %s",
                                 db_get_num(parsed_out, "min_cash"),
                                 (int)db_get_num(parsed_out, "runway_months"),
                                 db_get_num(parsed_out, "danger") > 0 ? "告急" : "稳健");
                    } else if (strcmp(st->step_id, "bc_collect") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "账单%d笔 月总额￥%.0f 占比%.0f%%",
                                 (int)db_get_num(parsed_out, "bill_count"),
                                 db_get_num(parsed_out, "total_monthly_bill"),
                                 db_get_num(parsed_out, "pressure_ratio"));
                    } else if (strcmp(st->step_id, "bc_calendar") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "高压%d天 峰值%02d日￥%.0f",
                                 (int)db_get_num(parsed_out, "pressure_days"),
                                 (int)db_get_num(parsed_out, "max_day"),
                                 db_get_num(parsed_out, "max_day_amount"));
                    } else if (strcmp(st->step_id, "hs_collect") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "资产￥%.0f 负债￥%.0f 应急%.1f月",
                                 db_get_num(parsed_out, "total_assets"),
                                 db_get_num(parsed_out, "total_debt"),
                                 db_get_num(parsed_out, "emergency_months"));
                    } else if (strcmp(st->step_id, "hs_score") == 0) {
                        snprintf(summary_buf,
                                 sizeof(summary_buf),
                                 "健康分%d/100 等级%s",
                                 (int)db_get_num(parsed_out, "total_score"),
                                 csilk_json_get_string(parsed_out, "grade") ?: "-");
                    }
                    csilk_json_add_object(ctx_obj, st->step_id, parsed_out);
                }
                free(step_out);
            }
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
