#include "services/ai/workflows/cashflow_forecast.h"
#include "services/ai/workflows/cashflow_forecast_common.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

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

    csilk_json_t* cur_cats = mf_ai_repo_daily_expense_monthly_by_category(pool, user_id, pat);
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
        const char*         cname = csilk_json_get_string(row, "category_name")
                                        ? csilk_json_get_string(row, "category_name")
                                        : "未分类";
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
            const char*         nm = csilk_json_get_string(it, "category_name")
                                         ? csilk_json_get_string(it, "category_name")
                                         : "未分类";
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
        const char*         nm = csilk_json_get_string(it, "category_name")
                                     ? csilk_json_get_string(it, "category_name")
                                     : "未分类";
        double              cur = db_get_num(it, "cur_amount");
        double              budget = db_get_num(it, "budget");
        double              proj = db_get_num(it, "projected");
        const char*         risk =
            csilk_json_get_string(it, "risk") ? csilk_json_get_string(it, "risk") : "safe";
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
