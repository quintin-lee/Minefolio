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
void transactions_export_csv(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

    csilk_db_pool_t* pool = db_get_pool();
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* rows = csilk_db_query_param_json(pool,
        "SELECT t.transaction_date, t.transaction_type, t.source_type, t.amount, "
        "t.price_per_unit, t.quantity, t.fee, t.currency, t.note, "
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
    csv_len = snprintf(csv, csv_cap, "date,asset_name,category_name,transaction_type,source_type,amount,price_per_unit,quantity,fee,currency,linked_asset_name,note\n");
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
            double amount = db_get_num(row, "amount");
            double price = db_get_num(row, "price_per_unit");
            double qty = db_get_num(row, "quantity");
            const char* currency = csilk_json_get_string(row, "currency");
            const char* asset_name = csilk_json_get_string(row, "asset_name");
            const char* linked_name = csilk_json_get_string(row, "linked_asset_name");
            const char* cat_name = csilk_json_get_string(row, "category_name");
            const char* note = csilk_json_get_string(row, "note");
            double fee = db_get_num(row, "fee");

            char escaped_date[64], escaped_type[64], escaped_src[64];
            char escaped_amount[64], escaped_price[64], escaped_qty[64];
            char escaped_fee[64];
            char escaped_currency[32], escaped_asset[256], escaped_linked[256];
            char escaped_cat[256], escaped_note[512];

            csv_escape(escaped_date, sizeof(escaped_date), date ? date : "");
            csv_escape(escaped_type, sizeof(escaped_type), tx_type ? tx_type : "");
            csv_escape(escaped_src, sizeof(escaped_src), src_type ? src_type : "");
            snprintf(escaped_amount, sizeof(escaped_amount), "%.2f", amount);
            snprintf(escaped_price, sizeof(escaped_price), price > 0 ? "%.4f" : "", price);
            snprintf(escaped_qty, sizeof(escaped_qty), qty > 0 ? "%.4f" : "", qty);
            snprintf(escaped_fee, sizeof(escaped_fee), fee > 0 ? "%.2f" : "", fee);
            csv_escape(escaped_currency, sizeof(escaped_currency), currency ? currency : "CNY");
            csv_escape(escaped_asset, sizeof(escaped_asset), asset_name ? asset_name : "");
            csv_escape(escaped_linked, sizeof(escaped_linked), linked_name ? linked_name : "");
            csv_escape(escaped_cat, sizeof(escaped_cat), cat_name ? cat_name : "");
            csv_escape(escaped_note, sizeof(escaped_note), note ? note : "");

            size_t line_len = snprintf(buf, sizeof(buf),
                "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
                escaped_date, escaped_asset, escaped_cat,
                escaped_type, escaped_src, escaped_amount,
                escaped_price, escaped_qty, escaped_fee, escaped_currency,
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

    char fname[160];
    snprintf(fname, sizeof(fname), "attachment; filename=\"transactions_%s.csv\"", date_str);

    csilk_status(c, CSILK_STATUS_OK);
    csilk_set_header(c, "Content-Type", "text/csv; charset=utf-8");
    csilk_set_header(c, "Content-Disposition", fname);
    csilk_response_write(c, (const uint8_t*)csv, csv_len);
    csilk_response_end(c);
    free(csv);
}
void daily_expenses_export_csv(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

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

    char fname[180];
    snprintf(fname, sizeof(fname), "attachment; filename=\"daily_expenses_%s.csv\"", date_str);

    csilk_status(c, CSILK_STATUS_OK);
    csilk_set_header(c, "Content-Type", "text/csv; charset=utf-8");
    csilk_set_header(c, "Content-Disposition", fname);
    csilk_response_write(c, (const uint8_t*)csv, csv_len);
    csilk_response_end(c);
    free(csv);
}
