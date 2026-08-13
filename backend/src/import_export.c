#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "common/tx_types.h"
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

void transactions_export_csv(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_db_pool_t* pool = db_get_pool();
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* rows = csilk_db_query_param_json(pool,
        "SELECT t.transaction_date, t.transaction_type, t.source_type, t.amount, "
        "t.price_per_unit, t.quantity, t.currency, t.note, "
        "a.name as asset_name, la.name as linked_asset_name, c.name as category_name "
        "FROM transactions t "
        "LEFT JOIN assets a ON t.asset_id=a.id "
        "LEFT JOIN assets la ON t.linked_asset_id=la.id "
        "LEFT JOIN categories c ON t.category_id=c.id "
        "WHERE t.user_id=? ORDER BY t.transaction_date DESC",
        (const char*[]){ uid_str, NULL });

    char* csv = NULL;
    size_t csv_len = 0;
    size_t csv_cap = 4096;
    csv = malloc(csv_cap);
    if (!csv) { respond_error(c, 500, "内存不足"); return; }
    csv_len = snprintf(csv, csv_cap, "date,asset_name,category_name,transaction_type,source_type,amount,price_per_unit,quantity,currency,linked_asset_name,note\n");
    /* Prepend UTF-8 BOM */
    {
        unsigned char bom[] = {0xef, 0xbb, 0xbf};
        if (csv_len + 3 <= csv_cap) {
            memmove(csv + 3, csv, csv_len);
            memcpy(csv, bom, 3);
            csv_len += 3;
        }
    }

    char buf[512];
    if (rows && csilk_json_array_size(rows) > 0) {
        for (size_t i = 0; i < csilk_json_array_size(rows); i++) {
            const csilk_json_t* row = csilk_json_array_get(rows, i);
            const char* date = csilk_json_get_string(row, "transaction_date");
            const char* tx_type = csilk_json_get_string(row, "transaction_type");
            const char* src_type = csilk_json_get_string(row, "source_type");
            double amount = csilk_json_get_number(row, "amount");
            double price = csilk_json_get_number(row, "price_per_unit");
            double qty = csilk_json_get_number(row, "quantity");
            const char* currency = csilk_json_get_string(row, "currency");
            const char* asset_name = csilk_json_get_string(row, "asset_name");
            const char* linked_name = csilk_json_get_string(row, "linked_asset_name");
            const char* cat_name = csilk_json_get_string(row, "category_name");
            const char* note = csilk_json_get_string(row, "note");

            char escaped_date[64], escaped_type[64], escaped_src[64];
            char escaped_amount[64], escaped_price[64], escaped_qty[64];
            char escaped_currency[32], escaped_asset[256], escaped_linked[256];
            char escaped_cat[256], escaped_note[512];

            csv_escape(escaped_date, sizeof(escaped_date), date ? date : "");
            csv_escape(escaped_type, sizeof(escaped_type), tx_type ? tx_type : "");
            csv_escape(escaped_src, sizeof(escaped_src), src_type ? src_type : "");
            snprintf(escaped_amount, sizeof(escaped_amount), "%.2f", amount);
            snprintf(escaped_price, sizeof(escaped_price), price > 0 ? "%.4f" : "", price);
            snprintf(escaped_qty, sizeof(escaped_qty), qty > 0 ? "%.4f" : "", qty);
            csv_escape(escaped_currency, sizeof(escaped_currency), currency ? currency : "CNY");
            csv_escape(escaped_asset, sizeof(escaped_asset), asset_name ? asset_name : "");
            csv_escape(escaped_linked, sizeof(escaped_linked), linked_name ? linked_name : "");
            csv_escape(escaped_cat, sizeof(escaped_cat), cat_name ? cat_name : "");
            csv_escape(escaped_note, sizeof(escaped_note), note ? note : "");

            size_t line_len = snprintf(buf, sizeof(buf),
                "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
                escaped_date, escaped_asset, escaped_cat,
                escaped_type, escaped_src, escaped_amount,
                escaped_price, escaped_qty, escaped_currency,
                escaped_linked, escaped_note);

            if (csv_len + line_len + 1 > csv_cap) {
                csv_cap = csv_cap * 2 + line_len;
                char* tmp = realloc(csv, csv_cap);
                if (!tmp) { free(csv); respond_error(c, 500, "内存不足"); return; }
                csv = tmp;
            }
            memcpy(csv + csv_len, buf, line_len);
            csv_len += line_len;
        }
    }
    if (rows) csilk_json_free(rows);

    time_t now = time(NULL);
    char date_str[16];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d", localtime(&now));

    char fname[128];
    snprintf(fname, sizeof(fname), "transactions_%s.csv", date_str);

    csilk_set_header(c, "Content-Type", "text/csv; charset=utf-8");
    csilk_set_header(c, "Content-Disposition", fname);
    csilk_response_write(c, (const uint8_t*)csv, csv_len);
    csilk_response_end(c);
    free(csv);
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

static int parse_csv_row(const char* line, size_t len, char out[11][512], int* count) {
    size_t pos = 0;
    *count = 0;
    while (pos < len) {
        size_t consumed = 0;
        if (*count >= 11) break;
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
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

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

        char fields[11][512];
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
        const char* currency_s = fields[8];
        const char* linked_s = fields[9];
        const char* note_s = fields[10];

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

        char category_param[32] = {0};
        if (category_id > 0) snprintf(category_param, sizeof(category_param), "%lld", (long long)category_id);
        char price_param[64] = {0};
        if (price > 0) snprintf(price_param, sizeof(price_param), "%.4f", price);
        char qty_param[64] = {0};
        if (qty > 0) snprintf(qty_param, sizeof(qty_param), "%.4f", qty);
        char linked_param[32] = {0};
        if (linked_id > 0) snprintf(linked_param, sizeof(linked_param), "%lld", (long long)linked_id);

        char asset_param[32];
        snprintf(asset_param, sizeof(asset_param), "%lld", (long long)asset_id);

        const char* ins_params[] = {
            uid_str, asset_param, linked_param, category_param,
            src_type, tx_type_s, ttype->stat_dir, ttype->linked_dir,
            amount_s, price_param, qty_param, currency,
            date_s, note_s ? note_s : "",
            NULL
        };

        csilk_json_t* res = csilk_db_query_param_json(pool,
            "INSERT INTO transactions (user_id, asset_id, linked_asset_id, category_id, "
            "source_type, transaction_type, direction, linked_direction, "
            "amount, price_per_unit, quantity, currency, "
            "transaction_date, note) "
            "VALUES (?, ?, NULLIF(?, '0'), NULLIF(?, '0'), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id",
            ins_params);
        if (res) csilk_json_free(res);
        imported++;

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

// ── Daily Expenses Export ─────────────────────────────────────────────────────

void daily_expenses_export_csv(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_db_pool_t* pool = db_get_pool();
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* rows = csilk_db_query_param_json(pool,
        "SELECT de.expense_date, de.expense_type, de.amount, de.currency, de.note, "
        "a.name as asset_name, c.name as category_name "
        "FROM daily_expenses de "
        "LEFT JOIN assets a ON de.asset_id=a.id "
        "LEFT JOIN categories c ON de.category_id=c.id "
        "WHERE de.user_id=? ORDER BY de.expense_date DESC",
        (const char*[]){ uid_str, NULL });

    char* csv = NULL;
    size_t csv_len = 0, csv_cap = 4096;
    csv = malloc(csv_cap);
    if (!csv) { respond_error(c, 500, "内存不足"); return; }
    csv_len = snprintf(csv, csv_cap, "date,asset_name,category_name,expense_type,amount,currency,note\n");
    {
        unsigned char bom[] = {0xef, 0xbb, 0xbf};
        if (csv_len + 3 <= csv_cap) {
            memmove(csv + 3, csv, csv_len);
            memcpy(csv, bom, 3);
            csv_len += 3;
        }
    }

    char buf[1024];
    if (rows && csilk_json_array_size(rows) > 0) {
        for (size_t i = 0; i < csilk_json_array_size(rows); i++) {
            const csilk_json_t* row = csilk_json_array_get(rows, i);
            char e_date[32], e_asset[256], e_cat[256], e_type[16], e_amount[32];
            char e_currency[16], e_note[512];
            csv_escape(e_date, sizeof(e_date), csilk_json_get_string(row, "expense_date"));
            csv_escape(e_asset, sizeof(e_asset), csilk_json_get_string(row, "asset_name"));
            csv_escape(e_cat, sizeof(e_cat), csilk_json_get_string(row, "category_name"));
            csv_escape(e_type, sizeof(e_type), csilk_json_get_string(row, "expense_type"));
            snprintf(e_amount, sizeof(e_amount), "%.2f", db_get_num(row, "amount"));
            csv_escape(e_currency, sizeof(e_currency), csilk_json_get_string(row, "currency"));
            csv_escape(e_note, sizeof(e_note), csilk_json_get_string(row, "note"));

            size_t line_len = snprintf(buf, sizeof(buf),
                "%s,%s,%s,%s,%s,%s,%s\n",
                e_date, e_asset, e_cat, e_type, e_amount, e_currency, e_note);

            if (csv_len + line_len + 1 > csv_cap) {
                csv_cap = csv_cap * 2 + line_len;
                char* tmp = realloc(csv, csv_cap);
                if (!tmp) { free(csv); respond_error(c, 500, "内存不足"); return; }
                csv = tmp;
            }
            memcpy(csv + csv_len, buf, line_len);
            csv_len += line_len;
        }
    }
    if (rows) csilk_json_free(rows);

    time_t now = time(NULL);
    char date_str[16];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d", localtime(&now));

    char fname[128];
    snprintf(fname, sizeof(fname), "daily_expenses_%s.csv", date_str);

    csilk_set_header(c, "Content-Type", "text/csv; charset=utf-8");
    csilk_set_header(c, "Content-Disposition", fname);
    csilk_response_write(c, (const uint8_t*)csv, csv_len);
    csilk_response_end(c);
    free(csv);
}

// ── Daily Expenses Import ─────────────────────────────────────────────────────

void daily_expenses_import_csv(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

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

    fprintf(stderr, "[DEBUG] daily_expenses import: body_len=%zu, csv_len=%zu\n", body_len, csv_len);
    fprintf(stderr, "[DEBUG] daily_expenses import: first 80 bytes: %.80s\n", data);

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
        fprintf(stderr, "[DEBUG] line %d: fc=%d fields[0..4]=[%s,%s,%s,%s,%s]\n",
            line_num, fc,
            fields[0], fields[1], fields[2], fields[3], fields[4]);
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
        if (res) csilk_json_free(res);
        imported++;

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
