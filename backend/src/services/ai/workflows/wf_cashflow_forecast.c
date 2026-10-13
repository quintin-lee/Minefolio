#include "services/ai/workflows/cashflow_forecast.h"
#include "services/ai/workflows/cashflow_forecast_common.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

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
                                  "  GROUP BY substr(CAST(expense_date AS TEXT),1,7))",
                                  p);
    double avg_income = 0;
    if (inc_res && csilk_json_array_size(inc_res) > 0) {
        avg_income = db_get_num(csilk_json_array_get(inc_res, 0), "avg_income");
    }
    if (inc_res) {
        csilk_json_free(inc_res);
    }

    int64_t       tot = 0;
    csilk_json_t* asset_arr = mf_ai_repo_asset_list(pool, user_id, 1, 100, NULL, &tot);
    double        liquid = 0;
    if (asset_arr) {
        for (size_t i = 0; i < csilk_json_array_size(asset_arr); i++) {
            const csilk_json_t* a = csilk_json_array_get(asset_arr, i);
            const char*         tp = csilk_json_get_string(a, "asset_type")
                                         ? csilk_json_get_string(a, "asset_type")
                                         : "";
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
