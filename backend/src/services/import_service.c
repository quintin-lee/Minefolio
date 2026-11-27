#include "services/import_service.h"
#include "application/account/usecases.h"
#include "application/task/async_task_manager.h"
#include "interfaces/http/controllers/import_export_controller.h"
#include "infrastructure/repositories/account_permission_repo_impl.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/jwt.h"
#include "common/tx_types.h"
#include "common/balance.h"
#include "core/ledger/ledger_engine.h"
#include "csilk/csilk.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "infrastructure/repositories/import_rule_repo_impl.h"
#include "csilk/util/csv.h"
#include <ctype.h>
#include <math.h>
#include <time.h>

static int
parse_positive_amount(const char* s, double* out_amount)
{
    if (!s || !s[0]) {
        return -1;
    }
    char*  endptr = NULL;
    double val = strtod(s, &endptr);
    if (endptr == s || *endptr != '\0' || !isfinite(val) || val <= 0.0) {
        return -1;
    }
    if (out_amount) {
        *out_amount = val;
    }
    return 0;
}

static const char*
ci_strstr(const char* haystack, const char* needle)
{
    if (!haystack || !needle) {
        return NULL;
    }
    if (!*needle) {
        return haystack;
    }
    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n && (tolower((unsigned char)*h) == tolower((unsigned char)*n))) {
            h++;
            n++;
        }
        if (!*n) {
            return haystack;
        }
    }
    return NULL;
}

static int
apply_smart_rules(csilk_json_t* rules_arr,
                  const char*   desc,
                  const char*   counterparty,
                  const char*   note,
                  int64_t*      io_category_id,
                  char*         io_target_type,
                  size_t        target_type_cap)
{
    if (!rules_arr || csilk_json_array_size(rules_arr) == 0) {
        return 0;
    }

    size_t count = csilk_json_array_size(rules_arr);
    for (size_t i = 0; i < count; i++) {
        csilk_json_t* r = csilk_json_array_get(rules_arr, i);
        if (!r) {
            continue;
        }
        int is_active = csilk_json_get(r, "is_active") ? db_get_bool(r, "is_active") : 1;
        if (!is_active) {
            continue;
        }

        const char* kw = csilk_json_get_string(r, "keyword");
        if (!kw || !kw[0]) {
            continue;
        }

        const char* field = csilk_json_get_string(r, "match_field");
        if (!field) {
            field = "all";
        }

        int matched = 0;
        if (strcmp(field, "all") == 0) {
            if ((desc && ci_strstr(desc, kw)) || (counterparty && ci_strstr(counterparty, kw)) ||
                (note && ci_strstr(note, kw))) {
                matched = 1;
            }
        } else if (strcmp(field, "description") == 0) {
            if (desc && ci_strstr(desc, kw)) {
                matched = 1;
            }
        } else if (strcmp(field, "counterparty") == 0) {
            if (counterparty && ci_strstr(counterparty, kw)) {
                matched = 1;
            }
        } else if (strcmp(field, "note") == 0) {
            if (note && ci_strstr(note, kw)) {
                matched = 1;
            }
        }

        if (matched) {
            int64_t rule_cid = db_get_int(r, "category_id");
            if (rule_cid > 0 && (*io_category_id <= 0)) {
                *io_category_id = rule_cid;
            }
            const char* rule_tt = csilk_json_get_string(r, "target_type");
            if (rule_tt && rule_tt[0] && io_target_type &&
                (!io_target_type[0] || strcmp(io_target_type, "expense") == 0)) {
                snprintf(io_target_type, target_type_cap, "%s", rule_tt);
            }
            return 1;
        }
    }
    return 0;
}

typedef struct {
    char*  csv_data;
    size_t csv_len;
} csv_import_task_arg_t;

static void
csv_import_task_cleanup(void* raw)
{
    csv_import_task_arg_t* arg = (csv_import_task_arg_t*)raw;
    if (!arg) {
        return;
    }
    free(arg->csv_data);
    free(arg);
}

static char*
serialize_csv_import_result(int         imported,
                            int         errors,
                            int         matched_rules_count,
                            const char* errors_detail)
{
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "imported", imported);
    csilk_json_add_number(resp, "errors", errors);
    csilk_json_add_number(resp, "matched_rules", matched_rules_count);
    if (errors_detail && errors_detail[0]) {
        csilk_json_add_string(resp, "errors_detail", errors_detail);
    }
    char* json = csilk_json_serialize(resp, NULL);
    csilk_json_free(resp);
    return json;
}

static void
submit_csv_import_task(csilk_ctx_t*       c,
                       const char*        task_type,
                       const char*        initial_msg,
                       mf_async_task_fn_t fn)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        respond_unauthorized(c);
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id <= 0) {
        return;
    }

    size_t      body_len = 0;
    const char* body = csilk_get_body(c, &body_len);
    if (!body || body_len == 0) {
        respond_bad_request(c, "请求体为空");
        return;
    }

    const char* csv = body;
    size_t      csv_len = body_len;
    if (csv_len >= 3 && (unsigned char)csv[0] == 0xEF && (unsigned char)csv[1] == 0xBB &&
        (unsigned char)csv[2] == 0xBF) {
        csv += 3;
        csv_len -= 3;
    }
    if (csv_len == 0) {
        respond_bad_request(c, "请求体为空");
        return;
    }
    if (csv_len > MF_MAX_CSV_IMPORT_BYTES) {
        respond_bad_request(c, "CSV 文件大小不能超过 10MB");
        return;
    }

    csv_import_task_arg_t* arg = calloc(1, sizeof(*arg));
    if (!arg) {
        respond_error(c, 500, "内存不足");
        return;
    }
    arg->csv_data = malloc(csv_len + 1);
    if (!arg->csv_data) {
        free(arg);
        respond_error(c, 500, "内存不足");
        return;
    }
    memcpy(arg->csv_data, csv, csv_len);
    arg->csv_data[csv_len] = '\0';
    arg->csv_len = csv_len;

    bool    deduplicated = false;
    int64_t task_id = mf_async_task_submit(db_get_pool(),
                                           user_id,
                                           ledger_id,
                                           task_type,
                                           NULL,
                                           initial_msg,
                                           fn,
                                           arg,
                                           csv_import_task_cleanup,
                                           &deduplicated);
    if (task_id <= 0) {
        respond_error(c, 500, "提交异步 CSV 导入任务失败");
        return;
    }

    mf_async_task_respond_submitted(c, task_id, task_type, deduplicated, NULL);
}

static void
append_import_error(char* buf, size_t cap, const char* fmt, ...)
{
    if (!buf || cap == 0 || !fmt) {
        return;
    }
    size_t used = strlen(buf);
    if (used + 1 >= cap) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + used, cap - used, fmt, ap);
    va_end(ap);
}

static int
resolve_import_asset(csilk_db_pool_t* pool,
                     int64_t          user_id,
                     int64_t          ledger_id,
                     const char*      name,
                     int64_t*         out_asset_id,
                     int64_t*         out_account_id)
{
    if (!pool || user_id <= 0 || ledger_id <= 0 || !name || !name[0]) {
        return -1;
    }
    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char*   sql = "SELECT a.id, COALESCE(a.account_id, 0) AS account_id "
                        "FROM assets a "
                        "WHERE a.name = ? "
                        "AND " MF_LEDGER_SHARED_ACCESS_SQL("a") " "
                                                                "AND " MF_RBAC_ASSET_VISIBLE_SQL(
                                                                    "a") " "
                                                                         "ORDER BY a.id ASC LIMIT 1";
    const char*   params[] = {name, lid_str, uid_str, uid_str, uid_str, uid_str, uid_str, NULL};
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return -1;
    }
    csilk_json_t* row = csilk_json_array_get(res, 0);
    if (out_asset_id) {
        *out_asset_id = db_get_int(row, "id");
    }
    if (out_account_id) {
        *out_account_id = db_get_int(row, "account_id");
    }
    csilk_json_free(res);
    return (out_asset_id && *out_asset_id > 0) ? 0 : -1;
}

static int64_t
resolve_import_category(csilk_db_pool_t* pool, int64_t user_id, int64_t ledger_id, const char* name)
{
    if (!pool || user_id <= 0 || ledger_id <= 0 || !name || !name[0]) {
        return 0;
    }
    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* sql =
        "SELECT c.id FROM categories c "
        "WHERE c.name = ? AND " MF_LEDGER_SHARED_ACCESS_SQL("c") " "
                                                                 "ORDER BY c.id ASC LIMIT 1";
    const char*   params[] = {name, lid_str, uid_str, uid_str, uid_str, NULL};
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int64_t       cid = 0;
    if (res && csilk_json_array_size(res) > 0) {
        cid = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return cid;
}

static bool
category_belongs_to_ledger(csilk_db_pool_t* pool,
                           int64_t          user_id,
                           int64_t          ledger_id,
                           int64_t          cat_id)
{
    if (!pool || user_id <= 0 || ledger_id <= 0 || cat_id <= 0) {
        return false;
    }
    char cid_str[32], uid_str[32], lid_str[32];
    snprintf(cid_str, sizeof(cid_str), "%lld", (long long)cat_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* sql =
        "SELECT c.id FROM categories c WHERE c.id = ? AND " MF_LEDGER_SHARED_ACCESS_SQL(
            "c") " LIMIT 1";
    csilk_json_t* res = csilk_db_query_param_json(
        pool, sql, (const char*[]){cid_str, lid_str, uid_str, uid_str, uid_str, NULL});
    bool ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int
import_transactions_csv_execute(csilk_db_pool_t* pool,
                                int64_t          task_id,
                                int64_t          user_id,
                                int64_t          ledger_id,
                                const char*      csv_data,
                                size_t           csv_len,
                                char**           out_result_json,
                                char*            err_buf,
                                size_t           err_cap)
{
    if (!pool || user_id <= 0 || ledger_id <= 0 || !csv_data || csv_len == 0) {
        if (err_buf && err_cap > 0) {
            snprintf(err_buf, err_cap, "参数错误");
        }
        return -1;
    }
    if (csv_len > MF_MAX_CSV_IMPORT_BYTES) {
        if (err_buf && err_cap > 0) {
            snprintf(err_buf, err_cap, "CSV 文件大小不能超过 10MB");
        }
        return -1;
    }

    const char* data = csv_data;
    if (csv_len >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF) {
        data += 3;
        csv_len -= 3;
    }
    if (csv_len == 0) {
        if (err_buf && err_cap > 0) {
            snprintf(err_buf, err_cap, "CSV 内容为空");
        }
        return -1;
    }

    int           imported = 0, errors = 0, matched_rules_count = 0;
    char          errors_detail[2048] = {0};
    csilk_json_t* rules = mf_import_rule_list(pool, user_id);
    const char*   end = data + csv_len;

    if (task_id > 0) {
        mf_async_task_update_progress(pool, task_id, 5, "正在解析交易 CSV 数据");
    }

    const char* line_start = data;
    int         line_num = 0;
    while (line_start < end) {
        if (task_id > 0 && (line_num % 25) == 0 && line_num > 0) {
            int pct = 5 + (int)(((size_t)(line_start - data) * 90) / csv_len);
            mf_async_task_update_progress(pool, task_id, pct, "正在批量导入交易记录");
        }
        const char* line_end = memchr(line_start, '\n', (size_t)(end - line_start));
        size_t line_len = line_end ? (size_t)(line_end - line_start) : (size_t)(end - line_start);
        const char* next_line = line_end ? line_end + 1 : end;

        while (line_len > 0 &&
               (line_start[line_len - 1] == '\r' || line_start[line_len - 1] == '\n')) {
            line_len--;
        }
        if (line_len == 0) {
            line_start = next_line;
            continue;
        }

        line_num++;
        if (line_num == 1) {
            line_start = next_line;
            continue;
        }

        char fields[12][512];
        int  fc = 0;
        csilk_csv_parse_row(line_start, line_len, fields, &fc);
        if (fc < 6) {
            errors++;
            append_import_error(
                errors_detail, sizeof(errors_detail), "第%d行: 字段数不足(%d)\n", line_num, fc);
            line_start = next_line;
            continue;
        }

        const char* date_s = fields[0];
        const char* asset_name = fields[1];
        const char* cat_name = fields[2];
        const char* tx_type_s = fields[3];
        const char* src_type_s = fields[4];
        const char* amount_s = fields[5];
        const char* price_s = fields[6];
        const char* qty_s = fields[7];
        const char* fee_s = (fc >= 12) ? fields[8] : "";
        const char* currency_s = (fc >= 12) ? fields[9] : fields[8];
        const char* linked_s = (fc >= 12) ? fields[10] : fields[9];
        const char* note_s = (fc >= 12) ? fields[11] : fields[10];
        double      fee = fee_s[0] ? strtod(fee_s, NULL) : 0;

        if (!date_s[0] || !asset_name[0] || !tx_type_s[0] || !amount_s[0]) {
            errors++;
            append_import_error(
                errors_detail, sizeof(errors_detail), "第%d行: 缺少必填字段\n", line_num);
            line_start = next_line;
            continue;
        }

        int64_t asset_id = 0, account_id = 0;
        if (resolve_import_asset(pool, user_id, ledger_id, asset_name, &asset_id, &account_id) !=
            0) {
            errors++;
            append_import_error(errors_detail,
                                sizeof(errors_detail),
                                "第%d行: 找不到资产 '%s'\n",
                                line_num,
                                asset_name);
            line_start = next_line;
            continue;
        }

        if (account_id > 0 &&
            account_usecase_check_permission(
                pool, ledger_id, user_id, account_id, MF_ACCOUNT_ACTION_WRITE_TX) != 0) {
            errors++;
            append_import_error(
                errors_detail, sizeof(errors_detail), "第%d行: 账户无记账权限\n", line_num);
            line_start = next_line;
            continue;
        }

        int64_t category_id = 0;
        if (cat_name[0]) {
            category_id = resolve_import_category(pool, user_id, ledger_id, cat_name);
        }
        if (category_id <= 0) {
            if (apply_smart_rules(rules, note_s, note_s, note_s, &category_id, NULL, 0)) {
                if (category_id > 0 &&
                    !category_belongs_to_ledger(pool, user_id, ledger_id, category_id)) {
                    category_id = 0;
                } else if (category_id > 0) {
                    matched_rules_count++;
                }
            }
        }

        int64_t linked_id = 0, linked_account_id = 0;
        if (linked_s[0]) {
            if (resolve_import_asset(
                    pool, user_id, ledger_id, linked_s, &linked_id, &linked_account_id) != 0) {
                errors++;
                append_import_error(errors_detail,
                                    sizeof(errors_detail),
                                    "第%d行: 找不到关联资产 '%s'\n",
                                    line_num,
                                    linked_s);
                line_start = next_line;
                continue;
            }
            if (linked_account_id > 0 &&
                account_usecase_check_permission(
                    pool, ledger_id, user_id, linked_account_id, MF_ACCOUNT_ACTION_WRITE_TX) != 0) {
                errors++;
                append_import_error(
                    errors_detail, sizeof(errors_detail), "第%d行: 关联账户无记账权限\n", line_num);
                line_start = next_line;
                continue;
            }
        }

        const tx_type_t* ttype = tx_type_lookup(tx_type_s);
        if (!ttype) {
            errors++;
            append_import_error(errors_detail,
                                sizeof(errors_detail),
                                "第%d行: 未知交易类型 '%s'\n",
                                line_num,
                                tx_type_s);
            line_start = next_line;
            continue;
        }

        double amount = 0.0;
        if (parse_positive_amount(amount_s, &amount) != 0) {
            errors++;
            append_import_error(errors_detail,
                                sizeof(errors_detail),
                                "第%d行: 无效金额 '%s'\n",
                                line_num,
                                amount_s);
            line_start = next_line;
            continue;
        }
        double      price = price_s[0] ? strtod(price_s, NULL) : 0;
        double      qty = qty_s[0] ? strtod(qty_s, NULL) : 0;
        const char* currency = currency_s[0] ? currency_s : "CNY";
        const char* src_type = src_type_s[0] ? src_type_s : "expense";

        if (strcmp(src_type, "income") != 0 && strcmp(src_type, "expense") != 0) {
            errors++;
            append_import_error(errors_detail,
                                sizeof(errors_detail),
                                "第%d行: source_type 必须为 income 或 expense\n",
                                line_num);
            line_start = next_line;
            continue;
        }

        currency_t cur = currency_from_str(currency);
        money_t    amt_m, fee_m;
        price_t    price_p;
        quantity_t qty_q;
        money_from_double(amount, cur, &amt_m);
        money_from_double(fee, cur, &fee_m);
        price_from_double(price, 4, cur, &price_p);
        quantity_from_double(qty, 4, &qty_q);

        ledger_tx_t ltx = {
            .id = 0,
            .user_id = user_id,
            .asset_id = asset_id,
            .linked_asset_id = linked_id,
            .category_id = category_id,
            .type = ledger_tx_type_from_str(tx_type_s),
            .type_str = tx_type_s,
            .amount = amt_m,
            .price = price_p,
            .quantity = qty_q,
            .fee = fee_m,
            .tx_date = date_s,
            .note = note_s,
            .parent_tx_id = 0,
            .ledger_id = ledger_id,
        };

        if (ledger_apply_tx(pool, &ltx) != 0) {
            errors++;
            append_import_error(
                errors_detail, sizeof(errors_detail), "第%d行: 导入失败或持有份额不足\n", line_num);
            line_start = next_line;
            continue;
        }

        imported++;
        line_start = next_line;
    }
    if (rules) {
        csilk_json_free(rules);
    }

    if (out_result_json) {
        *out_result_json =
            serialize_csv_import_result(imported, errors, matched_rules_count, errors_detail);
    }
    return 0;
}

static int
transactions_import_csv_task_fn(void*   raw_pool,
                                int64_t task_id,
                                int64_t user_id,
                                int64_t ledger_id,
                                void*   raw_arg,
                                char**  out_result_json,
                                char*   err_buf,
                                size_t  err_cap)
{
    csilk_db_pool_t*       pool = (csilk_db_pool_t*)raw_pool;
    csv_import_task_arg_t* arg = (csv_import_task_arg_t*)raw_arg;
    if (!pool || !arg || !arg->csv_data) {
        if (err_buf && err_cap > 0) {
            snprintf(err_buf, err_cap, "参数错误");
        }
        return -1;
    }
    return import_transactions_csv_execute(pool,
                                           task_id,
                                           user_id,
                                           ledger_id,
                                           arg->csv_data,
                                           arg->csv_len,
                                           out_result_json,
                                           err_buf,
                                           err_cap);
}

void
transactions_import_csv(csilk_ctx_t* c)
{
    submit_csv_import_task(
        c, "import_transactions", "正在排队导入交易 CSV", transactions_import_csv_task_fn);
}

int
import_daily_expenses_csv_execute(csilk_db_pool_t* pool,
                                  int64_t          task_id,
                                  int64_t          user_id,
                                  int64_t          ledger_id,
                                  const char*      csv_data,
                                  size_t           csv_len,
                                  char**           out_result_json,
                                  char*            err_buf,
                                  size_t           err_cap)
{
    if (!pool || user_id <= 0 || ledger_id <= 0 || !csv_data || csv_len == 0) {
        if (err_buf && err_cap > 0) {
            snprintf(err_buf, err_cap, "参数错误");
        }
        return -1;
    }
    if (csv_len > MF_MAX_CSV_IMPORT_BYTES) {
        if (err_buf && err_cap > 0) {
            snprintf(err_buf, err_cap, "CSV 文件大小不能超过 10MB");
        }
        return -1;
    }

    const char* data = csv_data;
    if (csv_len >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF) {
        data += 3;
        csv_len -= 3;
    }
    if (csv_len == 0) {
        if (err_buf && err_cap > 0) {
            snprintf(err_buf, err_cap, "CSV 内容为空");
        }
        return -1;
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    int           imported = 0, errors = 0, matched_rules_count = 0;
    char          errors_detail[2048] = {0};
    csilk_json_t* rules = mf_import_rule_list(pool, user_id);
    const char*   end = data + csv_len;

    if (task_id > 0) {
        mf_async_task_update_progress(pool, task_id, 5, "正在解析日常收支 CSV 数据");
    }

    const char* line_start = data;
    int         line_num = 0;
    while (line_start < end) {
        if (task_id > 0 && (line_num % 25) == 0 && line_num > 0) {
            int pct = 5 + (int)(((size_t)(line_start - data) * 90) / csv_len);
            mf_async_task_update_progress(pool, task_id, pct, "正在批量导入日常收支记录");
        }
        const char* line_end = memchr(line_start, '\n', (size_t)(end - line_start));
        size_t line_len = line_end ? (size_t)(line_end - line_start) : (size_t)(end - line_start);
        const char* next_line = line_end ? line_end + 1 : end;
        while (line_len > 0 &&
               (line_start[line_len - 1] == '\r' || line_start[line_len - 1] == '\n')) {
            line_len--;
        }
        if (line_len == 0) {
            line_start = next_line;
            continue;
        }

        line_num++;
        if (line_num == 1) {
            line_start = next_line;
            continue;
        }

        char fields[12][512];
        int  fc = 0;
        csilk_csv_parse_row(line_start, line_len, fields, &fc);
        if (fc < 5) {
            errors++;
            append_import_error(
                errors_detail, sizeof(errors_detail), "第%d行: 字段数不足(%d)\n", line_num, fc);
            line_start = next_line;
            continue;
        }

        const char* date_s = fields[0];
        const char* asset_name = fields[1];
        const char* cat_name = fields[2];
        const char* exp_type_s = fields[3];
        const char* amount_s = fields[4];
        const char* currency_s = fields[5];
        const char* note_s = fields[6];

        if (!date_s[0] || !asset_name[0] || !exp_type_s[0] || !amount_s[0]) {
            errors++;
            append_import_error(
                errors_detail, sizeof(errors_detail), "第%d行: 缺少必填字段\n", line_num);
            line_start = next_line;
            continue;
        }

        int64_t asset_id = 0, account_id = 0;
        if (resolve_import_asset(pool, user_id, ledger_id, asset_name, &asset_id, &account_id) !=
            0) {
            errors++;
            append_import_error(errors_detail,
                                sizeof(errors_detail),
                                "第%d行: 找不到资产 '%s'\n",
                                line_num,
                                asset_name);
            line_start = next_line;
            continue;
        }

        if (account_id > 0 &&
            account_usecase_check_permission(
                pool, ledger_id, user_id, account_id, MF_ACCOUNT_ACTION_WRITE_TX) != 0) {
            errors++;
            append_import_error(
                errors_detail, sizeof(errors_detail), "第%d行: 账户无记账权限\n", line_num);
            line_start = next_line;
            continue;
        }

        int64_t category_id = 0;
        char    matched_type[32] = {0};
        if (cat_name[0]) {
            category_id = resolve_import_category(pool, user_id, ledger_id, cat_name);
        }
        if (category_id <= 0) {
            if (apply_smart_rules(rules,
                                  note_s,
                                  note_s,
                                  note_s,
                                  &category_id,
                                  matched_type,
                                  sizeof(matched_type))) {
                if (category_id > 0 &&
                    !category_belongs_to_ledger(pool, user_id, ledger_id, category_id)) {
                    category_id = 0;
                } else if (category_id > 0) {
                    matched_rules_count++;
                }
            }
        }
        if (category_id <= 0) {
            errors++;
            append_import_error(errors_detail,
                                sizeof(errors_detail),
                                "第%d行: 找不到分类 '%s'\n",
                                line_num,
                                cat_name);
            line_start = next_line;
            continue;
        }

        double amount = 0.0;
        if (parse_positive_amount(amount_s, &amount) != 0) {
            errors++;
            append_import_error(errors_detail,
                                sizeof(errors_detail),
                                "第%d行: 无效金额 '%s'\n",
                                line_num,
                                amount_s);
            line_start = next_line;
            continue;
        }

        const char* currency = currency_s[0] ? currency_s : "CNY";
        const char* exp_type = strcmp(exp_type_s, "income") == 0 ? "income" : "expense";

        char asset_param[32], cat_param[32], lid_param[32];
        snprintf(asset_param, sizeof(asset_param), "%lld", (long long)asset_id);
        snprintf(cat_param, sizeof(cat_param), "%lld", (long long)category_id);
        snprintf(lid_param, sizeof(lid_param), "%lld", (long long)ledger_id);

        const char* ins_params[] = {uid_str,
                                    lid_param,
                                    cat_param,
                                    asset_param,
                                    exp_type,
                                    amount_s,
                                    currency,
                                    date_s,
                                    note_s ? note_s : "",
                                    NULL};

        db_tx_scope_t scope;
        if (db_tx_scope_begin(pool, "import_expense", &scope) != 0) {
            errors++;
            append_import_error(
                errors_detail, sizeof(errors_detail), "第%d行: 开启事务失败\n", line_num);
            line_start = next_line;
            continue;
        }

        csilk_json_t* res = csilk_db_query_param_json(
            pool,
            "INSERT INTO daily_expenses (user_id, ledger_id, category_id, asset_id, "
            "expense_type, amount, currency, expense_date, note) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id",
            ins_params);
        int64_t expense_id = 0;
        if (res && csilk_json_array_size(res) > 0) {
            expense_id = db_get_int(csilk_json_array_get(res, 0), "id");
        }
        if (res) {
            csilk_json_free(res);
        }
        if (expense_id <= 0) {
            db_tx_scope_rollback(pool, &scope);
            errors++;
            append_import_error(
                errors_detail, sizeof(errors_detail), "第%d行: 插入失败\n", line_num);
            line_start = next_line;
            continue;
        }

        int        is_income = (strcmp(exp_type, "income") == 0);
        currency_t cur = currency_from_str(currency);
        money_t    amt_m;
        money_from_double(amount, cur, &amt_m);

        if (ledger_apply_expense_dated(pool,
                                       user_id,
                                       asset_id,
                                       amt_m,
                                       is_income,
                                       expense_id,
                                       note_s ? note_s : "",
                                       date_s) != 0) {
            db_tx_scope_rollback(pool, &scope);
            errors++;
            append_import_error(
                errors_detail, sizeof(errors_detail), "第%d行: 账本余额更新失败\n", line_num);
            line_start = next_line;
            continue;
        }

        db_tx_scope_commit(pool, &scope);
        imported++;

        line_start = next_line;
    }
    if (rules) {
        csilk_json_free(rules);
    }

    if (out_result_json) {
        *out_result_json =
            serialize_csv_import_result(imported, errors, matched_rules_count, errors_detail);
    }
    return 0;
}

static int
daily_expenses_import_csv_task_fn(void*   raw_pool,
                                  int64_t task_id,
                                  int64_t user_id,
                                  int64_t ledger_id,
                                  void*   raw_arg,
                                  char**  out_result_json,
                                  char*   err_buf,
                                  size_t  err_cap)
{
    csilk_db_pool_t*       pool = (csilk_db_pool_t*)raw_pool;
    csv_import_task_arg_t* arg = (csv_import_task_arg_t*)raw_arg;
    if (!pool || !arg || !arg->csv_data) {
        if (err_buf && err_cap > 0) {
            snprintf(err_buf, err_cap, "参数错误");
        }
        return -1;
    }
    return import_daily_expenses_csv_execute(pool,
                                             task_id,
                                             user_id,
                                             ledger_id,
                                             arg->csv_data,
                                             arg->csv_len,
                                             out_result_json,
                                             err_buf,
                                             err_cap);
}

void
daily_expenses_import_csv(csilk_ctx_t* c)
{
    submit_csv_import_task(
        c, "import_daily_expenses", "正在排队导入日常收支 CSV", daily_expenses_import_csv_task_fn);
}
