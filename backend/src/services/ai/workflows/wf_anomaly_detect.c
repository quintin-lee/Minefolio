#include "services/ai/workflows/cashflow_forecast.h"
#include "services/ai/workflows/cashflow_forecast_common.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

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
            csilk_json_add_string(o,
                                  "category_name",
                                  csilk_json_get_string(r, "category_name")
                                      ? csilk_json_get_string(r, "category_name")
                                      : "未分类");
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
        const char*         cname = csilk_json_get_string(row, "category_name")
                                        ? csilk_json_get_string(row, "category_name")
                                        : "未分类";
        const char*         note =
            csilk_json_get_string(row, "note") ? csilk_json_get_string(row, "note") : "";
        const char* date = csilk_json_get_string(row, "expense_date")
                               ? csilk_json_get_string(row, "expense_date")
                               : "";
        double      avg = 0, std = 0, cnt = 0;
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
            const char*         da = csilk_json_get_string(a, "expense_date")
                                         ? csilk_json_get_string(a, "expense_date")
                                         : "";
            for (size_t j = i + 1; j < rn; j++) {
                const csilk_json_t* b = csilk_json_array_get(recent, j);
                double              amtb = db_get_num(b, "amount");
                int64_t             idb = db_get_int(b, "id");
                const char*         db_str = csilk_json_get_string(b, "expense_date")
                                                 ? csilk_json_get_string(b, "expense_date")
                                                 : "";
                if (ida != idb && fabs(amta - amtb) < 0.01 && strcmp(da, db_str) == 0 &&
                    amta >= 10.0) {
                    csilk_json_t* an = csilk_json_object();
                    csilk_json_add_string(an, "type", "duplicate");
                    csilk_json_add_string(an,
                                          "category_name",
                                          csilk_json_get_string(a, "category_name")
                                              ? csilk_json_get_string(a, "category_name")
                                              : "未分类");
                    csilk_json_add_number(an, "amount", amta);
                    csilk_json_add_string(an, "date", da);
                    csilk_json_add_string(
                        an,
                        "note",
                        csilk_json_get_string(a, "note") ? csilk_json_get_string(a, "note") : "");
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
            const char*         td = csilk_json_get_string(t, "transaction_date")
                                         ? csilk_json_get_string(t, "transaction_date")
                                         : "";
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
                csilk_json_add_string(
                    an,
                    "note",
                    csilk_json_get_string(t, "note") ? csilk_json_get_string(t, "note") : "");
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
        const char*         tp =
            csilk_json_get_string(it, "type") ? csilk_json_get_string(it, "type") : "-";
        const char* badge =
            strcmp(tp, "3sigma") == 0
                ? "🔴 金额异常"
                : (strcmp(tp, "duplicate") == 0
                       ? "🟡 重复扣款"
                       : (strcmp(tp, "midnight") == 0 ? "🟣 凌晨大额" : "🔵 高频小额"));
        const char* cname = csilk_json_get_string(it, "category_name")
                                ? csilk_json_get_string(it, "category_name")
                                : "-";
        double      amt = db_get_num(it, "amount");
        const char* date =
            csilk_json_get_string(it, "date") ? csilk_json_get_string(it, "date") : "-";
        const char* reason =
            csilk_json_get_string(it, "reason") ? csilk_json_get_string(it, "reason") : "-";
        char line[512];
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
