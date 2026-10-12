#include "services/ai/workflows/cashflow_forecast.h"
#include "services/ai/workflows/cashflow_forecast_common.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

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
        const char*         cname = csilk_json_get_string(g, "category_name")
                                        ? csilk_json_get_string(g, "category_name")
                                        : "未分类";
        double              amt = db_get_num(g, "amount");
        int                 cnt = (int)db_get_num(g, "cnt");
        const char*         first_date =
            csilk_json_get_string(g, "first_date") ? csilk_json_get_string(g, "first_date") : "";
        const char* last_date =
            csilk_json_get_string(g, "last_date") ? csilk_json_get_string(g, "last_date") : "";
        const char* notes =
            csilk_json_get_string(g, "notes") ? csilk_json_get_string(g, "notes") : "";

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
            const char* d = csilk_json_get_string(row, "expense_date")
                                ? csilk_json_get_string(row, "expense_date")
                                : "";
            char        ym[8] = {0};
            snprintf(ym, sizeof(ym), "%.7s", d);
            int found = 0;
            for (int k = 0; k < distinct_months; k++) {
                if (strcmp(months_seen[k], ym) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found && distinct_months < 12) {
                snprintf(months_seen[distinct_months], sizeof(months_seen[0]), "%s", ym);
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
            const char*         nm = csilk_json_get_string(it, "name");
            if (!nm) {
                nm = csilk_json_get_string(it, "category_name");
            }
            if (!nm) {
                nm = "未命名";
            }
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
        const char*         nm =
            csilk_json_get_string(it, "name") ? csilk_json_get_string(it, "name") : "-";
        const char* cname = csilk_json_get_string(it, "category_name")
                                ? csilk_json_get_string(it, "category_name")
                                : "-";
        double      amt = db_get_num(it, "amount");
        double      annual = db_get_num(it, "annual");
        int         cnt = (int)db_get_num(it, "cnt");
        int         dmonths = (int)db_get_num(it, "distinct_months");
        const char* last =
            csilk_json_get_string(it, "last_date") ? csilk_json_get_string(it, "last_date") : "-";
        int         stale = csilk_json_get_bool(it, "is_stale");
        int         hiked = csilk_json_get_bool(it, "is_hiked");
        const char* badge = stale ? "⚫ 疑似闲置" : (hiked ? "🔴 已涨价" : "🟢 正常扣费");
        char        line[512];
        char        safe_nm[96] = {0};
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
