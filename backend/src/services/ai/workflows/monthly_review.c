#include "services/ai/workflows/monthly_review.h"
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
mr_get_current_month_str(char* out, size_t sz)
{
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    snprintf(out, sz, "%04d-%02d", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1);
}

static void
mr_get_prev_month_str(const char* cur_month, char* out, size_t sz)
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
        mr_get_current_month_str(out, sz);
    }
}

static double
mr_get_user_avg_monthly_burn(csilk_db_pool_t* pool, int64_t user_id)
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

static char*
step_mr_aggregate(csilk_db_pool_t*    pool,
                  int64_t             user_id,
                  const csilk_json_t* params,
                  const char*         ctx_json)
{
    (void)ctx_json;
    char        month[32];
    const char* m_in = csilk_json_get_string(params, "month");
    if (m_in && m_in[0]) {
        strncpy(month, m_in, sizeof(month) - 1);
        month[sizeof(month) - 1] = '\0';
    } else {
        mr_get_current_month_str(month, sizeof(month));
    }

    char date_pattern[64];
    snprintf(date_pattern, sizeof(date_pattern), "%s%%", month);

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

    double total_income = total_inflows > 0.0 ? total_inflows : total_daily_income;

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
    (void)ctx_json;
    char        month[32];
    const char* m_in = csilk_json_get_string(params, "month");
    if (m_in && m_in[0]) {
        strncpy(month, m_in, sizeof(month) - 1);
        month[sizeof(month) - 1] = '\0';
    } else {
        mr_get_current_month_str(month, sizeof(month));
    }

    char prev_month[32];
    mr_get_prev_month_str(month, prev_month, sizeof(prev_month));

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
    double mom_rate = (prev_exp > 0.0) ? ((mom_diff / prev_exp) * 100.0) : 0.0;

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
    (void)params;
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

    double savings_rate = 0.0;
    if (income > 0.0) {
        savings_rate = ((income - expense) / income) * 100.0;
        if (savings_rate < 0.0) {
            savings_rate = 0.0;
        }
    }

    double avg_burn = mr_get_user_avg_monthly_burn(pool, user_id);
    if (avg_burn <= 0.0) {
        avg_burn = expense > 0.0 ? expense : 0.0;
    }

    double emergency_months =
        (avg_burn > 0.0) ? (liquid_cash / avg_burn) : (liquid_cash > 0.0 ? 12.0 : 0.0);
    double debt_ratio = (total_assets > 0.0) ? (total_liab / total_assets) * 100.0 : 0.0;

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
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "aggregate_data") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "analyze_trends") : NULL;
    csilk_json_t* s2 = root ? csilk_json_get(root, "health_diagnosis") : NULL;

    char cur_month_fallback[32];
    mr_get_current_month_str(cur_month_fallback, sizeof(cur_month_fallback));
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

static const ai_workflow_graph_t g_mr_graph = {
    .id = "wf_monthly_review",
    .title = "月末财务深度复盘",
    .description =
        "自动汇聚本月收支与资产净值，环比异动分析与财务健康度综合评分，生成专业复盘图表报告。",
    .icon = "ph:calendar-check",
    .node_count = 4,
    .nodes =
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
                },
};

const ai_workflow_graph_t*
ai_workflow_monthly_review_get_graph(void)
{
    return &g_mr_graph;
}
