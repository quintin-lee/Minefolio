#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "common/balance.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void transfers_create(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    int64_t from_id = db_get_int(body, "from_asset_id");
    int64_t to_id = db_get_int(body, "to_asset_id");
    double amount = db_get_num(body, "amount");
    const char* date = csilk_json_get_string(body, "transfer_date");
    if (!date || strlen(date) == 0) date = csilk_json_get_string(body, "transaction_date");
    if (!date || strlen(date) == 0) date = csilk_json_get_string(body, "date");
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
    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "数据库错误");
        return;
    }

    // 1. 插入 transfers 主表记录
    const char* tr_params[] = { uid_str, fid_str, tid_str, amt_str, currency, date, note ? note : "", NULL };
    csilk_json_t* tr_res = csilk_db_query_param_json(pool,
        "INSERT INTO transfers (user_id, from_asset_id, to_asset_id, amount, currency, transfer_date, note) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) RETURNING id", tr_params);
    if (!tr_res || csilk_json_array_size(tr_res) == 0) {
        csilk_db_exec(pool, "ROLLBACK");
        if (tr_res) csilk_json_free(tr_res);
        csilk_json_free(body);
        respond_error(c, 500, "转账失败");
        return;
    }
    int64_t transfer_id = db_get_int(csilk_json_array_get(tr_res, 0), "id");
    csilk_json_free(tr_res);

    // 2. 插入转出交易记录 (transfer_out)
    const char* out_params[] = { uid_str, fid_str, amt_str, currency, date, note ? note : "", NULL };
    csilk_json_t* res1 = csilk_db_query_param_json(pool,
        "INSERT INTO transactions (user_id, asset_id, source_type, transaction_type, amount, currency, transaction_date, note) "
        "VALUES (?, ?, 'expense', 'transfer_out', ?, ?, ?, ?) RETURNING id", out_params);
    if (!res1 || csilk_json_array_size(res1) == 0) {
        csilk_db_exec(pool, "ROLLBACK");
        if (res1) csilk_json_free(res1);
        csilk_json_free(body);
        respond_error(c, 500, "记录划转交易失败");
        return;
    }
    csilk_json_free(res1);

    // 3. 插入转入交易记录 (transfer_in)
    const char* in_params[] = { uid_str, tid_str, amt_str, currency, date, note ? note : "", NULL };
    csilk_json_t* res2 = csilk_db_query_param_json(pool,
        "INSERT INTO transactions (user_id, asset_id, source_type, transaction_type, amount, currency, transaction_date, note) "
        "VALUES (?, ?, 'income', 'transfer_in', ?, ?, ?, ?) RETURNING id", in_params);
    if (!res2 || csilk_json_array_size(res2) == 0) {
        csilk_db_exec(pool, "ROLLBACK");
        if (res2) csilk_json_free(res2);
        csilk_json_free(body);
        respond_error(c, 500, "记录划转交易失败");
        return;
    }
    csilk_json_free(res2);

    // 4. 联动资产余额与审计日志
    if (balance_apply_delta(pool, from_id, user_id, -amount, "transfer", transfer_id, note) != 0 ||
        balance_apply_delta(pool, to_id, user_id, amount, "transfer", transfer_id, note) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(body);
        respond_bad_request(c, "资产余额更新失败");
        return;
    }

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(body);
    respond_ok_null(c);
}
