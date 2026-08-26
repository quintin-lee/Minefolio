#include "repositories/transaction_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

csilk_json_t* tx_list(csilk_db_pool_t* pool, int64_t user_id, int64_t page, int64_t page_size,
                       const char* asset_id, const char* category_id, const char* type,
                       const char* source_type, const char* start_date, const char* end_date, int64_t* total) {
    char uid[32], limit[32], offset[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(limit, sizeof(limit), "%lld", (long long)page_size);
    snprintf(offset, sizeof(offset), "%lld", (long long)((page - 1) * page_size));
    char sql[1024], count_sql[1024];
    const char* params[16], *cnt_params[16];
    int pidx = 0, cnt_pidx = 0;
    params[pidx++] = uid; cnt_params[cnt_pidx++] = uid;
    snprintf(sql, sizeof(sql), "SELECT t.id,t.asset_id,t.linked_asset_id,t.category_id,t.transaction_type,t.source_type,t.direction,t.linked_direction,t.amount,t.price_per_unit,t.quantity,t.currency,t.transaction_date,t.note,a.name as asset_name,la.name as linked_asset_name,c.name as category_name FROM transactions t LEFT JOIN assets a ON t.asset_id=a.id LEFT JOIN assets la ON t.linked_asset_id=la.id LEFT JOIN categories c ON t.category_id=c.id WHERE t.user_id=?");
    snprintf(count_sql, sizeof(count_sql), "SELECT COUNT(*) AS cnt FROM transactions t WHERE t.user_id=?");
#define AF(x) do { strncat(sql," AND t." x "=?",sizeof(sql)-strlen(sql)-1); strncat(count_sql," AND t." x "=?",sizeof(count_sql)-strlen(count_sql)-1); params[pidx++]=x; cnt_params[cnt_pidx++]=x; } while(0)
    if (asset_id) AF("asset_id");
    if (category_id) AF("category_id");
    if (type) AF("transaction_type");
    if (source_type) AF("source_type");
    if (start_date) { strncat(sql," AND t.transaction_date >= ?",sizeof(sql)-strlen(sql)-1); strncat(count_sql," AND t.transaction_date >= ?",sizeof(count_sql)-strlen(count_sql)-1); params[pidx++]=start_date; cnt_params[cnt_pidx++]=start_date; }
    if (end_date) { strncat(sql," AND t.transaction_date <= ?",sizeof(sql)-strlen(sql)-1); strncat(count_sql," AND t.transaction_date <= ?",sizeof(count_sql)-strlen(count_sql)-1); params[pidx++]=end_date; cnt_params[cnt_pidx++]=end_date; }
#undef AF
    strncat(sql, " ORDER BY t.transaction_date DESC LIMIT ? OFFSET ?", sizeof(sql)-strlen(sql)-1);
    params[pidx++] = limit; params[pidx++] = offset; params[pidx] = NULL;
    cnt_params[cnt_pidx] = NULL;
    csilk_json_t* cnt_res = csilk_db_query_param_json(pool, count_sql, cnt_params);
    *total = 0;
    if (cnt_res && csilk_json_array_size(cnt_res) > 0) *total = db_get_int(csilk_json_array_get(cnt_res, 0), "cnt");
    if (cnt_res) csilk_json_free(cnt_res);
    return csilk_db_query_param_json(pool, sql, params);
}
csilk_json_t* tx_monthly(csilk_db_pool_t* pool, int64_t user_id, const char* pattern) {
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    return csilk_db_query_param_json(pool, "SELECT COALESCE(SUM(amount),0) AS total_volume,COALESCE(SUM(CASE WHEN direction='in' THEN amount ELSE 0 END),0) AS inflows,COALESCE(SUM(CASE WHEN direction='out' THEN amount ELSE 0 END),0) AS outflows,COUNT(*) AS count FROM transactions WHERE user_id=? AND transaction_date LIKE ?", (const char*[]){ uid, pattern, NULL });
}
int64_t tx_insert(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id, int64_t linked_asset_id, int64_t category_id, const char* source_type, const char* transaction_type, const char* direction, const char* linked_direction, double amount, double price_per_unit, double quantity, double fee, const char* currency, const char* date, const char* note) {
    char uid[32], ast[32], last[32], cat[32], amt[64], pp[64], qty[64], fee_s[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(ast, sizeof(ast), "%lld", (long long)asset_id);
    snprintf(last, sizeof(last), "%lld", (long long)linked_asset_id);
    snprintf(cat, sizeof(cat), "%lld", (long long)category_id);
    snprintf(amt, sizeof(amt), "%.6f", amount);
    snprintf(pp, sizeof(pp), "%.4f", price_per_unit);
    snprintf(qty, sizeof(qty), "%.4f", quantity);
    snprintf(fee_s, sizeof(fee_s), "%.6f", fee);
    csilk_json_t* res = csilk_db_query_param_json(pool, "INSERT INTO transactions (user_id,asset_id,linked_asset_id,category_id,source_type,transaction_type,direction,linked_direction,amount,price_per_unit,quantity,fee,currency,transaction_date,note) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) RETURNING id", (const char*[]){ uid, ast, linked_asset_id > 0 ? last : "NULL", cat, source_type, transaction_type, direction ? "in" : "out", linked_direction ? "out" : NULL, amt, pp, qty, fee_s, currency ? currency : "CNY", date, note ? note : "", NULL });
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) id = db_get_int(csilk_json_array_get(res, 0), "id");
    if (res) csilk_json_free(res);
    return id;
}
int tx_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* transaction_type, const char* direction, const char* linked_direction, double amount, double price_per_unit, double quantity, const char* currency, const char* date, const char* note, int64_t category_id, const char* source_type, int64_t linked_asset_id) {
    char uid[32], idstr[32], amt[64], pp[64], qty[64], cat[32], last[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    snprintf(amt, sizeof(amt), "%.6f", amount);
    snprintf(pp, sizeof(pp), "%.4f", price_per_unit);
    snprintf(qty, sizeof(qty), "%.4f", quantity);
    snprintf(cat, sizeof(cat), "%lld", (long long)category_id);
    snprintf(last, sizeof(last), "%lld", (long long)linked_asset_id);
    csilk_json_t* res = csilk_db_query_param_json(pool, "UPDATE transactions SET transaction_type=?,direction=?,linked_direction=?,amount=?,price_per_unit=?,quantity=?,currency=?,transaction_date=?,note=?,category_id=?,source_type=?,linked_asset_id=NULLIF(?, '0') WHERE id=? AND user_id=? RETURNING id", (const char*[]){ transaction_type, direction ? "in" : "out", linked_direction ? "out" : NULL, amt, pp, qty, currency ? currency : "CNY", date ? date : "", note ? note : "", cat, source_type ? source_type : "expense", last, idstr, uid, NULL });
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) csilk_json_free(res);
    return ok;
}
csilk_json_t* tx_get_old(csilk_db_pool_t* pool, int64_t user_id, int64_t id) {
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    return csilk_db_query_param_json(pool, "SELECT asset_id,linked_asset_id,amount,transaction_type,quantity,price_per_unit,fee FROM transactions WHERE id=? AND user_id=?", (const char*[]){ idstr, uid, NULL });
}
int tx_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id) {
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(pool, "DELETE FROM transactions WHERE id=? AND user_id=? RETURNING id", (const char*[]){ idstr, uid, NULL });
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) csilk_json_free(res);
    return ok;
}
int tx_asset_exists(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id) {
    char uid[32], ast[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(ast, sizeof(ast), "%lld", (long long)asset_id);
    csilk_json_t* res = csilk_db_query_param_json(pool, "SELECT id FROM assets WHERE id=? AND user_id=?", (const char*[]){ ast, uid, NULL });
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) csilk_json_free(res);
    return ok;
}
