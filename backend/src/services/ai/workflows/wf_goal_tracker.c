#include "services/ai/workflows/cashflow_forecast.h"
#include "services/ai/workflows/cashflow_forecast_common.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

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
    /* savings_goals 表由迁移 V009 统一管理（sql/migrations/{sqlite,postgres}/V009__savings_goals.sql），
     * 应用启动时 main.c 已执行迁移，此处无需内联建表。 */

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
            const char* deadline =
                csilk_json_get_string(r, "deadline") ? csilk_json_get_string(r, "deadline") : "";
            int is_overdue = 0, is_completed = prog >= 100 ? 1 : 0;
            int days_left = -1;
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
            csilk_json_add_string(
                o,
                "name",
                csilk_json_get_string(r, "name") ? csilk_json_get_string(r, "name") : "未命名目标");
            csilk_json_add_number(o, "target_amount", tgt);
            csilk_json_add_number(o, "current_amount", cur);
            csilk_json_add_number(o, "progress", prog);
            csilk_json_add_number(o, "remaining", tgt > cur ? tgt - cur : 0);
            csilk_json_add_string(o, "deadline", deadline);
            csilk_json_add_number(o, "days_left", (double)days_left);
            csilk_json_add_bool(o, "is_overdue", is_overdue);
            csilk_json_add_bool(o, "is_completed", is_completed);
            csilk_json_add_string(
                o,
                "note",
                csilk_json_get_string(r, "note") ? csilk_json_get_string(r, "note") : "");
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
        const char*         deadline =
            csilk_json_get_string(g, "deadline") ? csilk_json_get_string(g, "deadline") : "";
        int days_left = (int)db_get_num(g, "days_left");

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
        csilk_json_add_string(o,
                              "name",
                              csilk_json_get_string(g, "name") ? csilk_json_get_string(g, "name")
                                                               : "未命名");
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
            const char*         nm =
                csilk_json_get_string(g, "name") ? csilk_json_get_string(g, "name") : "目标";
            char safe[64] = {0};
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
        const char* nm = csilk_json_get_string(g, "name") ? csilk_json_get_string(g, "name") : "-";
        double      tgt = db_get_num(g, "target_amount");
        double      cur = db_get_num(g, "current_amount");
        double      prog = db_get_num(g, "progress");
        const char* dl =
            csilk_json_get_string(g, "deadline") ? csilk_json_get_string(g, "deadline") : "—";
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
