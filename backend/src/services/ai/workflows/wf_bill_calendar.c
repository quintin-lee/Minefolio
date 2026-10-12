#include "services/ai/workflows/cashflow_forecast.h"
#include "services/ai/workflows/cashflow_forecast_common.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

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
            const char* sdate = csilk_json_get_string(r, "start_date")
                                    ? csilk_json_get_string(r, "start_date")
                                    : "";
            int         due_day = 1;
            int         y, m, d;
            if (sscanf(sdate, "%d-%d-%d", &y, &m, &d) == 3 && d >= 1 && d <= 31) {
                due_day = d;
            }
            csilk_json_t* item = csilk_json_object();
            csilk_json_add_number(item, "id", (double)db_get_int(r, "id"));
            csilk_json_add_string(
                item,
                "name",
                csilk_json_get_string(r, "name") ? csilk_json_get_string(r, "name") : "现金流排期");
            csilk_json_add_number(item, "amount", amt);
            csilk_json_add_string(item, "kind", "schedule");
            csilk_json_add_string(item,
                                  "type",
                                  csilk_json_get_string(r, "flow_type")
                                      ? csilk_json_get_string(r, "flow_type")
                                      : "dividend");
            csilk_json_add_number(item, "day", (double)due_day);
            csilk_json_add_string(
                item,
                "note",
                csilk_json_get_string(r, "note") ? csilk_json_get_string(r, "note") : "");
            csilk_json_array_append(bills, item);
            total_monthly_bill += amt;
            bill_count++;
        }
    }
    if (res) {
        csilk_json_free(res);
    }

    int64_t       tot_assets = 0;
    csilk_json_t* alist = mf_ai_repo_asset_list(pool, user_id, 1, 100, NULL, &tot_assets);
    if (alist && csilk_json_is_array(alist)) {
        size_t n = csilk_json_array_size(alist);
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* a = csilk_json_array_get(alist, i);
            const char*         atype = csilk_json_get_string(a, "asset_type")
                                            ? csilk_json_get_string(a, "asset_type")
                                            : "";
            if (strcmp(atype, "loan") != 0 && strcmp(atype, "credit_card") != 0 &&
                strcmp(atype, "other_liability") != 0) {
                continue;
            }
            double bal = cf_get_asset_val(a);
            if (bal <= 0.0) {
                continue;
            }

            const char* aname =
                csilk_json_get_string(a, "name") ? csilk_json_get_string(a, "name") : "负债账单";
            const char* anote =
                csilk_json_get_string(a, "note") ? csilk_json_get_string(a, "note") : "";
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
            const char* last_d =
                csilk_json_get_string(r, "last_date") ? csilk_json_get_string(r, "last_date") : "";
            int due_day = 1;
            int y, m, d;
            if (sscanf(last_d, "%d-%d-%d", &y, &m, &d) == 3 && d >= 1 && d <= 31) {
                due_day = d;
            }
            const char* cname = csilk_json_get_string(r, "category_name")
                                    ? csilk_json_get_string(r, "category_name")
                                    : "固定支出";
            const char* notes =
                csilk_json_get_string(r, "notes") ? csilk_json_get_string(r, "notes") : "";
            char bill_title[128];
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
