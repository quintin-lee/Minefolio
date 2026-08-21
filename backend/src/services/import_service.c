#include "services/import_export_service.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/jwt.h"
#include "common/tx_types.h"
#include "common/balance.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void csv_escape(char* out, size_t out_size, const char* val) {
    if (!val) { *out = '\0'; return; }
    int needs_quote = 0;
    for (const char* p = val; *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') { needs_quote = 1; break; }
    }
    if (!needs_quote) {
        snprintf(out, out_size, "%s", val);
        return;
    }
    size_t j = 0;
    out[j++] = '"';
    for (const char* p = val; *p && j < out_size - 2; p++) {
        if (*p == '"') { out[j++] = '"'; out[j++] = '"'; }
        else { out[j++] = *p; }
    }
    out[j++] = '"'; out[j++] = '\0';
}

static int parse_csv_field(const char* line, size_t len, char* out, size_t out_size, size_t* chars_consumed) {
    size_t pos = 0;
    if (pos >= len || line[pos] != '"') {
        while (pos < len && line[pos] != ',' && line[pos] != '\n' && line[pos] != '\r') pos++;
        size_t n = pos < out_size - 1 ? pos : out_size - 1;
        memcpy(out, line, n);
        out[n] = '\0';
        if (chars_consumed) *chars_consumed = pos;
        return 0;
    }
    pos++;
    size_t oi = 0;
    while (pos < len) {
        if (line[pos] == '"' && pos + 1 < len && line[pos + 1] == '"') {
            if (oi < out_size - 1) out[oi++] = '"';
            pos += 2;
        } else if (line[pos] == '"') {
            pos++;
            if (chars_consumed) *chars_consumed = pos;
            out[oi] = '\0';
            return 0;
        } else {
            if (oi < out_size - 1) out[oi++] = line[pos];
            pos++;
        }
    }
    out[oi] = '\0';
    if (chars_consumed) *chars_consumed = pos;
    return 0;
}

static int parse_csv_row(const char* line, size_t len, char out[12][512], int* count) {
    size_t pos = 0;
    *count = 0;
    while (pos < len) {
        size_t consumed = 0;
        if (*count >= 12) break;
        parse_csv_field(line + pos, len - pos, out[*count], 512, &consumed);
        pos += consumed;
        (*count)++;
        if (pos >= len || line[pos] == '\n' || line[pos] == '\r') {
            if (pos < len && (line[pos] == '\r')) pos++;
            if (pos < len && line[pos] == '\n') pos++;
            break;
        }
        if (pos < len && line[pos] == ',') pos++;
    }
    return *count;
}
void transactions_import_csv(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    size_t body_len = 0;
    const char* body = csilk_get_body(c, &body_len);
    if (!body || body_len == 0) {
        respond_bad_request(c, "请求体为空");
        return;
    }

    // Strip UTF-8 BOM if present
    const char* csv = body;
    size_t csv_len = body_len;
    if (csv_len >= 3 && csv[0] == '\xEF' && csv[1] == '\xBB' && csv[2] == '\xBF') {
        csv += 3;
        csv_len -= 3;
    }

    int imported = 0, errors = 0;
    char errors_detail[2048] = {0};
    csilk_db_pool_t* pool = db_get_pool();

    char* data = malloc(csv_len + 1);
    if (!data) { respond_error(c, 500, "内存不足"); return; }
    memcpy(data, csv, csv_len);
    data[csv_len] = '\0';

    char* line_start = data;
    int line_num = 0;
    while (*line_start) {
        char* line_end = strchr(line_start, '\n');
        size_t line_len = line_end ? (line_end - line_start) : strlen(line_start);

        while (line_len > 0 && (line_start[line_len-1] == '\r' || line_start[line_len-1] == '\n'))
            line_len--;
        if (line_len == 0) { line_start = line_end ? line_end + 1 : line_start + 1; continue; }

        line_num++;
        if (line_num == 1) { line_start = line_end ? line_end + 1 : line_start + 1; continue; }

        char fields[12][512];
        int fc = 0;
        parse_csv_row(line_start, line_len, fields, &fc);
        if (fc < 6) {
            errors++;
            if (snprintf(errors_detail + strlen(errors_detail), sizeof(errors_detail) - strlen(errors_detail),
                    "第%d行: 字段数不足(%d)\n", line_num, fc) > 0) {}
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
        double fee = fee_s[0] ? strtod(fee_s, NULL) : 0;

        if (!date_s[0] || !asset_name[0] || !tx_type_s[0] || !amount_s[0]) {
            errors++;
            if (snprintf(errors_detail + strlen(errors_detail), sizeof(errors_detail) - strlen(errors_detail),
                    "第%d行: 缺少必填字段\n", line_num) > 0) {}
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        int64_t asset_id = 0;
        const char* a_params[] = { uid_str, asset_name, NULL };
        csilk_json_t* a_res = csilk_db_query_param_json(pool,
            "SELECT id FROM assets WHERE user_id=? AND name=?", a_params);
        if (a_res && csilk_json_array_size(a_res) > 0)
            asset_id = db_get_int(csilk_json_array_get(a_res, 0), "id");
        if (a_res) csilk_json_free(a_res);
        if (asset_id <= 0) {
            errors++;
            if (snprintf(errors_detail + strlen(errors_detail), sizeof(errors_detail) - strlen(errors_detail),
                    "第%d行: 找不到资产 '%s'\n", line_num, asset_name) > 0) {}
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        int64_t category_id = 0;
        if (cat_name[0]) {
            const char* c_params[] = { uid_str, cat_name, NULL };
            csilk_json_t* c_res = csilk_db_query_param_json(pool,
                "SELECT id FROM categories WHERE user_id=? AND name=?", c_params);
            if (c_res && csilk_json_array_size(c_res) > 0)
                category_id = db_get_int(csilk_json_array_get(c_res, 0), "id");
            if (c_res) csilk_json_free(c_res);
        }

        int64_t linked_id = 0;
        if (linked_s[0]) {
            const char* l_params[] = { uid_str, linked_s, NULL };
            csilk_json_t* l_res = csilk_db_query_param_json(pool,
                "SELECT id FROM assets WHERE user_id=? AND name=?", l_params);
            if (l_res && csilk_json_array_size(l_res) > 0)
                linked_id = db_get_int(csilk_json_array_get(l_res, 0), "id");
            if (l_res) csilk_json_free(l_res);
        }

        const tx_type_t* ttype = tx_type_lookup(tx_type_s);
        if (!ttype) {
            errors++;
            if (snprintf(errors_detail + strlen(errors_detail), sizeof(errors_detail) - strlen(errors_detail),
                    "第%d行: 未知交易类型 '%s'\n", line_num, tx_type_s) > 0) {}
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        double amount = strtod(amount_s, NULL);
        double price = price_s[0] ? strtod(price_s, NULL) : 0;
        double qty = qty_s[0] ? strtod(qty_s, NULL) : 0;
        const char* currency = currency_s[0] ? currency_s : "CNY";
        const char* src_type = src_type_s[0] ? src_type_s : "expense";

        if (strcmp(src_type, "income") != 0 && strcmp(src_type, "expense") != 0) {
            errors++;
            if (snprintf(errors_detail + strlen(errors_detail), sizeof(errors_detail) - strlen(errors_detail),
                    "第%d行: source_type 必须为 income 或 expense\n", line_num) > 0) {}
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        char category_param[32] = {0};
        if (category_id > 0) snprintf(category_param, sizeof(category_param), "%lld", (long long)category_id);
        char price_param[64] = {0};
        if (price > 0) snprintf(price_param, sizeof(price_param), "%.4f", price);
        char qty_param[64] = {0};
        if (qty > 0) snprintf(qty_param, sizeof(qty_param), "%.4f", qty);
        char fee_param[64] = {0};
        if (fee > 0) snprintf(fee_param, sizeof(fee_param), "%.2f", fee);
        char linked_param[32] = {0};
        if (linked_id > 0) snprintf(linked_param, sizeof(linked_param), "%lld", (long long)linked_id);

        char asset_param[32];
        snprintf(asset_param, sizeof(asset_param), "%lld", (long long)asset_id);

        const char* ins_params[] = {
            uid_str, asset_param, linked_param, category_param,
            src_type, tx_type_s, ttype->stat_dir, ttype->linked_dir,
            amount_s, price_param, qty_param, fee_param,
            currency, date_s, note_s ? note_s : "",
            NULL
        };

        csilk_json_t* res = csilk_db_query_param_json(pool,
            "INSERT INTO transactions (user_id, asset_id, linked_asset_id, category_id, "
            "source_type, transaction_type, direction, linked_direction, "
            "amount, price_per_unit, quantity, fee, "
            "currency, transaction_date, note) "
            "VALUES (?, ?, NULLIF(?, '0'), NULLIF(?, '0'), ?, ?, ?, ?, ?, ?, ?, NULLIF(?, '0'), ?, ?, ?) RETURNING id",
            ins_params);
        int64_t tx_id = 0;
        if (res && csilk_json_array_size(res) > 0)
            tx_id = db_get_int(csilk_json_array_get(res, 0), "id");
        if (res) csilk_json_free(res);
        if (tx_id <= 0) {
            errors++;
            if (snprintf(errors_detail + strlen(errors_detail), sizeof(errors_detail) - strlen(errors_detail),
                    "第%d行: 插入失败\n", line_num) > 0) {}
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }
        imported++;

        if (strcmp(tx_type_s, "fee") == 0 && linked_id > 0) {
            double lfee = amount;
            balance_apply_delta(pool, linked_id, user_id, -lfee, "transaction_fee", tx_id, note_s ? note_s : "");
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        int is_investment_tx = 0;
        if (strcmp(tx_type_s, "buy") == 0 || strcmp(tx_type_s, "sell") == 0) {
            double position_delta = 0;
            int prc = apply_position(pool, asset_id, tx_type_s, amount, fee, price, qty, &position_delta);
            if (prc < 0) {
                errors++;
                if (snprintf(errors_detail + strlen(errors_detail), sizeof(errors_detail) - strlen(errors_detail),
                        "第%d行: 持有份额不足\n", line_num) > 0) {}
                line_start = line_end ? line_end + 1 : line_start + 1;
                continue;
            }
            is_investment_tx = (position_delta != 0 || strcmp(tx_type_s, "buy") == 0);
            if (is_investment_tx)
                balance_apply_delta(pool, asset_id, user_id, position_delta, "transaction", tx_id, note_s ? note_s : "");
        }

        double tdelta = is_investment_tx ? 0 : tx_delta(tx_type_s, amount, price, qty);
        if (!is_investment_tx && tdelta != 0)
            balance_apply_delta(pool, asset_id, user_id, tdelta, "transaction", tx_id, note_s ? note_s : "");

        if (linked_id > 0) {
            double ldelta = tx_effective_ldelta(tx_type_s, amount, is_investment_tx ? 0 : tdelta);
            if (ldelta != 0)
                balance_apply_delta(pool, linked_id, user_id, ldelta, "transaction_linked", tx_id, note_s ? note_s : "");
        }

        line_start = line_end ? line_end + 1 : line_start + 1;
    }
    free(data);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "imported", imported);
    csilk_json_add_number(resp, "errors", errors);
    if (errors_detail[0])
        csilk_json_add_string(resp, "errors_detail", errors_detail);
    respond_ok(c, resp);
}
void daily_expenses_import_csv(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    size_t body_len = 0;
    const char* body = csilk_get_body(c, &body_len);
    if (!body || body_len == 0) { respond_bad_request(c, "请求体为空"); return; }

    const char* csv = body;
    size_t csv_len = body_len;
    if (csv_len >= 3 && csv[0] == '\xef' && csv[1] == '\xbb' && csv[2] == '\xbf') {
        csv += 3; csv_len -= 3;
    }

    int imported = 0, errors = 0;
    char errors_detail[2048] = {0};
    csilk_db_pool_t* pool = db_get_pool();

    char* data = malloc(csv_len + 1);
    if (!data) { respond_error(c, 500, "内存不足"); return; }
    memcpy(data, csv, csv_len);
    data[csv_len] = '\0';

    char* line_start = data;
    int line_num = 0;
    while (*line_start) {
        char* line_end = strchr(line_start, '\n');
        size_t line_len = line_end ? (line_end - line_start) : strlen(line_start);
        while (line_len > 0 && (line_start[line_len-1] == '\r' || line_start[line_len-1] == '\n'))
            line_len--;
        if (line_len == 0) { line_start = line_end ? line_end + 1 : line_start + 1; continue; }

        line_num++;
        if (line_num == 1) { line_start = line_end ? line_end + 1 : line_start + 1; continue; }

        char fields[7][512];
        int fc = 0;
        parse_csv_row(line_start, line_len, fields, &fc);
        if (fc < 5) {
            errors++;
            snprintf(errors_detail + strlen(errors_detail), sizeof(errors_detail) - strlen(errors_detail),
                "第%d行: 字段数不足(%d)\n", line_num, fc);
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
            snprintf(errors_detail + strlen(errors_detail), sizeof(errors_detail) - strlen(errors_detail),
                "第%d行: 缺少必填字段\n", line_num);
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        int64_t asset_id = 0;
        const char* a_params[] = { uid_str, asset_name, NULL };
        csilk_json_t* a_res = csilk_db_query_param_json(pool,
            "SELECT id FROM assets WHERE user_id=? AND name=?", a_params);
        if (a_res && csilk_json_array_size(a_res) > 0)
            asset_id = db_get_int(csilk_json_array_get(a_res, 0), "id");
        if (a_res) csilk_json_free(a_res);
        if (asset_id <= 0) {
            errors++;
            snprintf(errors_detail + strlen(errors_detail), sizeof(errors_detail) - strlen(errors_detail),
                "第%d行: 找不到资产 '%s'\n", line_num, asset_name);
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        int64_t category_id = 0;
        if (cat_name[0]) {
            const char* c_params[] = { uid_str, cat_name, NULL };
            csilk_json_t* c_res = csilk_db_query_param_json(pool,
                "SELECT id FROM categories WHERE user_id=? AND name=?", c_params);
            if (c_res && csilk_json_array_size(c_res) > 0)
                category_id = db_get_int(csilk_json_array_get(c_res, 0), "id");
            if (c_res) csilk_json_free(c_res);
        }
        if (category_id <= 0) {
            errors++;
            snprintf(errors_detail + strlen(errors_detail), sizeof(errors_detail) - strlen(errors_detail),
                "第%d行: 找不到分类 '%s'\n", line_num, cat_name);
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }

        const char* currency = currency_s[0] ? currency_s : "CNY";
        const char* exp_type = strcmp(exp_type_s, "income") == 0 ? "income" : "expense";

        char asset_param[32], cat_param[32];
        snprintf(asset_param, sizeof(asset_param), "%lld", (long long)asset_id);
        snprintf(cat_param, sizeof(cat_param), "%lld", (long long)category_id);

        const char* ins_params[] = {
            uid_str, cat_param, asset_param, exp_type, amount_s,
            currency, date_s, note_s ? note_s : "",
            NULL
        };

        csilk_json_t* res = csilk_db_query_param_json(pool,
            "INSERT INTO daily_expenses (user_id, category_id, asset_id, expense_type, amount, currency, expense_date, note) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?) RETURNING id",
            ins_params);
        int64_t expense_id = 0;
        if (res && csilk_json_array_size(res) > 0)
            expense_id = db_get_int(csilk_json_array_get(res, 0), "id");
        if (res) csilk_json_free(res);
        if (expense_id <= 0) {
            errors++;
            snprintf(errors_detail + strlen(errors_detail), sizeof(errors_detail) - strlen(errors_detail),
                "第%d行: 插入失败\n", line_num);
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }
        imported++;

        double amount = strtod(amount_s, NULL);
        double business_delta = strcmp(exp_type, "income") == 0 ? amount : -amount;
        balance_apply_delta(pool, asset_id, user_id, business_delta, "daily_expense", expense_id, note_s ? note_s : "");

        line_start = line_end ? line_end + 1 : line_start + 1;
    }
    free(data);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "imported", imported);
    csilk_json_add_number(resp, "errors", errors);
    if (errors_detail[0])
        csilk_json_add_string(resp, "errors_detail", errors_detail);
    respond_ok(c, resp);
}
