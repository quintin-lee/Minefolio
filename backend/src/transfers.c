#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void transfers_create(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    int64_t from_id = (int64_t)csilk_json_get_number(body, "from_asset_id");
    int64_t to_id = (int64_t)csilk_json_get_number(body, "to_asset_id");
    double amount = csilk_json_get_number(body, "amount");
    const char* date = csilk_json_get_string(body, "transfer_date");
    const char* note = csilk_json_get_string(body, "note");
    const char* currency = csilk_json_get_string(body, "currency");
    if (!currency) currency = "CNY";

    if (from_id <= 0 || to_id <= 0 || amount <= 0 || !date) {
        csilk_json_free(body);
        respond_bad_request(c, "from_asset_id、to_asset_id、amount、transfer_date 为必填");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();

    if (from_id == to_id) {
        csilk_json_free(body);
        respond_bad_request(c, "转出和转入资产不能相同");
        return;
    }

    char fid_str[32], tid_str[32], uid_str[32], amt_str[64];
    snprintf(fid_str, sizeof(fid_str), "%lld", (long long)from_id);
    snprintf(tid_str, sizeof(tid_str), "%lld", (long long)to_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(amt_str, sizeof(amt_str), "%.6f", amount);

    // Verify both assets belong to user
    const char* chk_params[] = { fid_str, tid_str, uid_str, NULL };
    csilk_json_t* chk = csilk_db_query_param_json(pool,
        "SELECT COUNT(*) as cnt FROM assets WHERE id IN (?, ?) AND user_id=?", chk_params);
    if (!chk || csilk_json_array_size(chk) == 0 ||
        db_get_num(csilk_json_array_get(chk, 0), "cnt") != 2) {
        csilk_json_free(body);
        if (chk) csilk_json_free(chk);
        respond_not_found(c);
        return;
    }
    csilk_json_free(chk);

    // Start transaction
    csilk_db_exec(pool, "BEGIN");

    // Transfer out
    const char* out_params[] = { uid_str, fid_str, amt_str, currency, date, note ? note : "", NULL };
    csilk_json_t* res1 = csilk_db_query_param_json(pool,
        "INSERT INTO transactions (user_id, asset_id, transaction_type, amount, currency, transaction_date, note) "
        "VALUES (?, ?, 'transfer_out', ?, ?, ?, ?)", out_params);
    if (res1) csilk_json_free(res1);

    // Transfer in
    const char* in_params[] = { uid_str, tid_str, amt_str, currency, date, note ? note : "", NULL };
    csilk_json_t* res2 = csilk_db_query_param_json(pool,
        "INSERT INTO transactions (user_id, asset_id, transaction_type, amount, currency, transaction_date, note) "
        "VALUES (?, ?, 'transfer_in', ?, ?, ?, ?)", in_params);
    if (res2) csilk_json_free(res2);

    // Update asset values
    const char* up1_params[] = { amt_str, fid_str, uid_str, NULL };
    csilk_json_t* res3 = csilk_db_query_param_json(pool,
        "UPDATE assets SET current_value=current_value-?, updated_at=CURRENT_TIMESTAMP "
        "WHERE id=? AND user_id=?", up1_params);
    if (res3) csilk_json_free(res3);

    const char* up2_params[] = { amt_str, tid_str, uid_str, NULL };
    csilk_json_t* res4 = csilk_db_query_param_json(pool,
        "UPDATE assets SET current_value=current_value+?, updated_at=CURRENT_TIMESTAMP "
        "WHERE id=? AND user_id=?", up2_params);
    if (res4) csilk_json_free(res4);

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(body);
    respond_ok_null(c);
}
