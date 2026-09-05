#include "services/import_service.h"
#include "interfaces/http/controllers/import_export_controller.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/jwt.h"
#include "common/tx_types.h"
#include "common/balance.h"
#include "core/ledger/ledger_engine.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "repositories/import_rule_repo.h"
#include "common/csv_utils.h"
#include <ctype.h>
#include <time.h>

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
        int is_active = csilk_json_get(r, "is_active") ? csilk_json_get_bool(r, "is_active") : 1;
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

void
transactions_import_csv(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    size_t      body_len = 0;
    const char* body = csilk_get_body(c, &body_len);
    if (!body || body_len == 0) {
        respond_bad_request(c, "请求体为空");
        return;
    }

    // Strip UTF-8 BOM if present
    const char* csv = body;
    size_t      csv_len = body_len;
    if (csv_len >= 3 && csv[0] == '\xEF' && csv[1] == '\xBB' && csv[2] == '\xBF') {
        csv += 3;
        csv_len -= 3;
    }

    int              imported = 0, errors = 0, matched_rules_count = 0;
    char             errors_detail[2048] = {0};
    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    rules = import_rule_list(pool, user_id);

    char* data = malloc(csv_len + 1);
    if (!data) {
        respond_error(c, 500, "内存不足");
        return;
    }
    memcpy(data, csv, csv_len);
    data[csv_len] = '\0';

    char* line_start = data;
    int   line_num = 0;
    while (*line_start) {
        char*  line_end = strchr(line_start, '\n');
        size_t line_len = line_end ? (size_t)(line_end - line_start) : strlen(line_start);

        while (line_len > 0 &&
               (line_start[line_len - 1] == '\r' || line_start[line_len - 1] == '\n')) {
            line_len--;
        }
        if (line_len == 0) {
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        line_num++;
        if (line_num == 1) {
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        char fields[12][512];
        int  fc = 0;
        parse_csv_row(line_start, line_len, fields, &fc);
        if (fc < 6) {
            errors++;
            if (snprintf(errors_detail + strlen(errors_detail),
                         sizeof(errors_detail) - strlen(errors_detail),
                         "第%d行: 字段数不足(%d)\n",
                         line_num,
                         fc) > 0) {
            }
            line_start = line_end ? line_end + 1 : line_start + 1;
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
            if (snprintf(errors_detail + strlen(errors_detail),
                         sizeof(errors_detail) - strlen(errors_detail),
                         "第%d行: 缺少必填字段\n",
                         line_num) > 0) {
            }
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        int64_t       asset_id = 0;
        const char*   a_params[] = {uid_str, asset_name, NULL};
        csilk_json_t* a_res = csilk_db_query_param_json(
            pool, "SELECT id FROM assets WHERE user_id=? AND name=?", a_params);
        if (a_res && csilk_json_array_size(a_res) > 0) {
            asset_id = db_get_int(csilk_json_array_get(a_res, 0), "id");
        }
        if (a_res) {
            csilk_json_free(a_res);
        }
        if (asset_id <= 0) {
            errors++;
            if (snprintf(errors_detail + strlen(errors_detail),
                         sizeof(errors_detail) - strlen(errors_detail),
                         "第%d行: 找不到资产 '%s'\n",
                         line_num,
                         asset_name) > 0) {
            }
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        int64_t category_id = 0;
        if (cat_name[0]) {
            const char*   c_params[] = {uid_str, cat_name, NULL};
            csilk_json_t* c_res = csilk_db_query_param_json(
                pool, "SELECT id FROM categories WHERE user_id=? AND name=?", c_params);
            if (c_res && csilk_json_array_size(c_res) > 0) {
                category_id = db_get_int(csilk_json_array_get(c_res, 0), "id");
            }
            if (c_res) {
                csilk_json_free(c_res);
            }
        }

        if (category_id <= 0) {
            if (apply_smart_rules(rules, note_s, note_s, note_s, &category_id, NULL, 0)) {
                matched_rules_count++;
            }
        }

        int64_t linked_id = 0;
        if (linked_s[0]) {
            const char*   l_params[] = {uid_str, linked_s, NULL};
            csilk_json_t* l_res = csilk_db_query_param_json(
                pool, "SELECT id FROM assets WHERE user_id=? AND name=?", l_params);
            if (l_res && csilk_json_array_size(l_res) > 0) {
                linked_id = db_get_int(csilk_json_array_get(l_res, 0), "id");
            }
            if (l_res) {
                csilk_json_free(l_res);
            }
        }

        const tx_type_t* ttype = tx_type_lookup(tx_type_s);
        if (!ttype) {
            errors++;
            if (snprintf(errors_detail + strlen(errors_detail),
                         sizeof(errors_detail) - strlen(errors_detail),
                         "第%d行: 未知交易类型 '%s'\n",
                         line_num,
                         tx_type_s) > 0) {
            }
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        double      amount = strtod(amount_s, NULL);
        double      price = price_s[0] ? strtod(price_s, NULL) : 0;
        double      qty = qty_s[0] ? strtod(qty_s, NULL) : 0;
        const char* currency = currency_s[0] ? currency_s : "CNY";
        const char* src_type = src_type_s[0] ? src_type_s : "expense";

        if (strcmp(src_type, "income") != 0 && strcmp(src_type, "expense") != 0) {
            errors++;
            if (snprintf(errors_detail + strlen(errors_detail),
                         sizeof(errors_detail) - strlen(errors_detail),
                         "第%d行: source_type 必须为 income 或 expense\n",
                         line_num) > 0) {
            }
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        char category_param[32] = {0};
        if (category_id > 0) {
            snprintf(category_param, sizeof(category_param), "%lld", (long long)category_id);
        }
        char price_param[64] = {0};
        if (price > 0) {
            snprintf(price_param, sizeof(price_param), "%.4f", price);
        }
        char qty_param[64] = {0};
        if (qty > 0) {
            snprintf(qty_param, sizeof(qty_param), "%.4f", qty);
        }
        char fee_param[64] = {0};
        if (fee > 0) {
            snprintf(fee_param, sizeof(fee_param), "%.2f", fee);
        }
        char linked_param[32] = {0};
        if (linked_id > 0) {
            snprintf(linked_param, sizeof(linked_param), "%lld", (long long)linked_id);
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
        };

        if (ledger_apply_tx(pool, &ltx) != 0) {
            errors++;
            if (snprintf(errors_detail + strlen(errors_detail),
                         sizeof(errors_detail) - strlen(errors_detail),
                         "第%d行: 导入失败或持有份额不足\n",
                         line_num) > 0) {
            }
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        imported++;
        line_start = line_end ? line_end + 1 : line_start + 1;
    }
    free(data);
    if (rules) {
        csilk_json_free(rules);
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "imported", imported);
    csilk_json_add_number(resp, "errors", errors);
    csilk_json_add_number(resp, "matched_rules", matched_rules_count);
    if (errors_detail[0]) {
        csilk_json_add_string(resp, "errors_detail", errors_detail);
    }
    respond_ok(c, resp);
}
void
daily_expenses_import_csv(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    size_t      body_len = 0;
    const char* body = csilk_get_body(c, &body_len);
    if (!body || body_len == 0) {
        respond_bad_request(c, "请求体为空");
        return;
    }

    const char* csv = body;
    size_t      csv_len = body_len;
    if (csv_len >= 3 && csv[0] == '\xef' && csv[1] == '\xbb' && csv[2] == '\xbf') {
        csv += 3;
        csv_len -= 3;
    }

    int              imported = 0, errors = 0, matched_rules_count = 0;
    char             errors_detail[2048] = {0};
    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    rules = import_rule_list(pool, user_id);

    char* data = malloc(csv_len + 1);
    if (!data) {
        respond_error(c, 500, "内存不足");
        return;
    }
    memcpy(data, csv, csv_len);
    data[csv_len] = '\0';

    char* line_start = data;
    int   line_num = 0;
    while (*line_start) {
        char*  line_end = strchr(line_start, '\n');
        size_t line_len = line_end ? (size_t)(line_end - line_start) : strlen(line_start);
        while (line_len > 0 &&
               (line_start[line_len - 1] == '\r' || line_start[line_len - 1] == '\n')) {
            line_len--;
        }
        if (line_len == 0) {
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        line_num++;
        if (line_num == 1) {
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        char fields[12][512];
        int  fc = 0;
        parse_csv_row(line_start, line_len, fields, &fc);
        if (fc < 5) {
            errors++;
            snprintf(errors_detail + strlen(errors_detail),
                     sizeof(errors_detail) - strlen(errors_detail),
                     "第%d行: 字段数不足(%d)\n",
                     line_num,
                     fc);
            line_start = line_end ? line_end + 1 : line_start + 1;
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
            snprintf(errors_detail + strlen(errors_detail),
                     sizeof(errors_detail) - strlen(errors_detail),
                     "第%d行: 缺少必填字段\n",
                     line_num);
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        int64_t       asset_id = 0;
        const char*   a_params[] = {uid_str, asset_name, NULL};
        csilk_json_t* a_res = csilk_db_query_param_json(
            pool, "SELECT id FROM assets WHERE user_id=? AND name=?", a_params);
        if (a_res && csilk_json_array_size(a_res) > 0) {
            asset_id = db_get_int(csilk_json_array_get(a_res, 0), "id");
        }
        if (a_res) {
            csilk_json_free(a_res);
        }
        if (asset_id <= 0) {
            errors++;
            snprintf(errors_detail + strlen(errors_detail),
                     sizeof(errors_detail) - strlen(errors_detail),
                     "第%d行: 找不到资产 '%s'\n",
                     line_num,
                     asset_name);
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        int64_t category_id = 0;
        char    matched_type[32] = {0};
        if (cat_name[0]) {
            const char*   c_params[] = {uid_str, cat_name, NULL};
            csilk_json_t* c_res = csilk_db_query_param_json(
                pool, "SELECT id FROM categories WHERE user_id=? AND name=?", c_params);
            if (c_res && csilk_json_array_size(c_res) > 0) {
                category_id = db_get_int(csilk_json_array_get(c_res, 0), "id");
            }
            if (c_res) {
                csilk_json_free(c_res);
            }
        }
        if (category_id <= 0) {
            if (apply_smart_rules(rules,
                                  note_s,
                                  note_s,
                                  note_s,
                                  &category_id,
                                  matched_type,
                                  sizeof(matched_type))) {
                matched_rules_count++;
            }
        }
        if (category_id <= 0) {
            errors++;
            snprintf(errors_detail + strlen(errors_detail),
                     sizeof(errors_detail) - strlen(errors_detail),
                     "第%d行: 找不到分类 '%s'\n",
                     line_num,
                     cat_name);
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        const char* currency = currency_s[0] ? currency_s : "CNY";
        const char* exp_type = strcmp(exp_type_s, "income") == 0 ? "income" : "expense";

        char asset_param[32], cat_param[32];
        snprintf(asset_param, sizeof(asset_param), "%lld", (long long)asset_id);
        snprintf(cat_param, sizeof(cat_param), "%lld", (long long)category_id);

        const char* ins_params[] = {uid_str,
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
            snprintf(errors_detail + strlen(errors_detail),
                     sizeof(errors_detail) - strlen(errors_detail),
                     "第%d行: 开启事务失败\n",
                     line_num);
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        csilk_json_t* res =
            csilk_db_query_param_json(pool,
                                      "INSERT INTO daily_expenses (user_id, category_id, asset_id, "
                                      "expense_type, amount, currency, expense_date, note) "
                                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?) RETURNING id",
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
            snprintf(errors_detail + strlen(errors_detail),
                     sizeof(errors_detail) - strlen(errors_detail),
                     "第%d行: 插入失败\n",
                     line_num);
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        double     amount = strtod(amount_s, NULL);
        int        is_income = (strcmp(exp_type, "income") == 0);
        currency_t cur = currency_from_str(currency);
        money_t    amt_m;
        money_from_double(amount, cur, &amt_m);

        if (ledger_apply_expense(
                pool, user_id, asset_id, amt_m, is_income, expense_id, note_s ? note_s : "") != 0) {
            db_tx_scope_rollback(pool, &scope);
            errors++;
            snprintf(errors_detail + strlen(errors_detail),
                     sizeof(errors_detail) - strlen(errors_detail),
                     "第%d行: 账本余额更新失败\n",
                     line_num);
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        db_tx_scope_commit(pool, &scope);
        imported++;

        line_start = line_end ? line_end + 1 : line_start + 1;
    }
    free(data);
    if (rules) {
        csilk_json_free(rules);
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "imported", imported);
    csilk_json_add_number(resp, "errors", errors);
    csilk_json_add_number(resp, "matched_rules", matched_rules_count);
    if (errors_detail[0]) {
        csilk_json_add_string(resp, "errors_detail", errors_detail);
    }
    respond_ok(c, resp);
}
