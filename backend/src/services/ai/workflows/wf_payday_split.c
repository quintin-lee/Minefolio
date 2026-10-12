#include "services/ai/workflows/cashflow_forecast.h"
#include "services/ai/workflows/cashflow_forecast_common.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

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
    csilk_json_t* list = mf_ai_repo_asset_list(pool, user_id, 1, 100, NULL, &tot);
    if (list) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* a = csilk_json_array_get(list, i);
            const char*   t = csilk_json_get_string(a, "asset_type")
                                  ? csilk_json_get_string(a, "asset_type")
                                  : "";
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
    csilk_json_t* list = mf_ai_repo_asset_list(pool, user_id, 1, 100, NULL, &tot);
    if (list) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* a = csilk_json_array_get(list, i);
            const char*   t = csilk_json_get_string(a, "asset_type")
                                  ? csilk_json_get_string(a, "asset_type")
                                  : "";
            const char*   nm =
                csilk_json_get_string(a, "name") ? csilk_json_get_string(a, "name") : "";
            int64_t aid = db_get_int(a, "id");
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
