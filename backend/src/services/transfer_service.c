#include "services/transfer_service.h"
#include "repositories/transfer_repo.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/balance.h"
#include "core/ledger/ledger_engine.h"
#include "csilk/csilk.h"
#include <string.h>

void
transfers_create(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id < 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    int64_t     from_id = db_get_int(body, "from_asset_id");
    int64_t     to_id = db_get_int(body, "to_asset_id");
    double      amount = db_get_num(body, "amount");
    const char* date = csilk_json_get_string(body, "transfer_date");
    if (!date || date[0] == '\0') {
        date = csilk_json_get_string(body, "transaction_date");
    }
    if (!date || date[0] == '\0') {
        date = csilk_json_get_string(body, "date");
    }
    const char* note = csilk_json_get_string(body, "note");
    const char* currency = csilk_json_get_string(body, "currency");
    if (!currency) {
        currency = "CNY";
    }

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

    if (!transfer_asset_check(pool, user_id, from_id, to_id)) {
        csilk_json_free(body);
        respond_not_found(c);
        return;
    }

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "数据库错误");
        return;
    }

    int64_t transfer_id =
        transfer_insert(pool, user_id, from_id, to_id, amount, currency, date, note);
    if (transfer_id <= 0) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(body);
        respond_error(c, 500, "转账失败");
        return;
    }

    /* Record transfer_out transaction */
    {
        char uid[32], amt[64], fid[32];
        snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
        snprintf(amt, sizeof(amt), "%.6f", amount);
        snprintf(fid, sizeof(fid), "%lld", (long long)from_id);
        csilk_json_t* res = csilk_db_query_param_json(
            pool,
            "INSERT INTO transactions (user_id, asset_id, source_type, transaction_type, amount, "
            "currency, transaction_date, note) "
            "VALUES (?, ?, 'expense', 'transfer_out', ?, ?, ?, ?) RETURNING id",
            (const char*[]){uid, fid, amt, currency, date, note ? note : "", NULL});
        if (!res || csilk_json_array_size(res) == 0) {
            csilk_db_exec(pool, "ROLLBACK");
            if (res) {
                csilk_json_free(res);
            }
            csilk_json_free(body);
            respond_error(c, 500, "记录划转交易失败");
            return;
        }
        csilk_json_free(res);
    }

    /* Record transfer_in transaction */
    {
        char uid[32], amt[64], tid[32];
        snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
        snprintf(amt, sizeof(amt), "%.6f", amount);
        snprintf(tid, sizeof(tid), "%lld", (long long)to_id);
        csilk_json_t* res = csilk_db_query_param_json(
            pool,
            "INSERT INTO transactions (user_id, asset_id, source_type, transaction_type, amount, "
            "currency, transaction_date, note) "
            "VALUES (?, ?, 'income', 'transfer_in', ?, ?, ?, ?) RETURNING id",
            (const char*[]){uid, tid, amt, currency, date, note ? note : "", NULL});
        if (!res || csilk_json_array_size(res) == 0) {
            csilk_db_exec(pool, "ROLLBACK");
            if (res) {
                csilk_json_free(res);
            }
            csilk_json_free(body);
            respond_error(c, 500, "记录划转交易失败");
            return;
        }
        csilk_json_free(res);
    }

    /* Balance linkage via Ledger Engine */
    currency_t cur = currency_from_str(currency);
    money_t    amt_m;
    money_from_double(amount, cur, &amt_m);

    if (ledger_apply_transfer(pool, user_id, from_id, to_id, amt_m, transfer_id, note) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(body);
        respond_bad_request(c, "资产余额更新失败");
        return;
    }

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(body);
    respond_ok_null(c);
}

void
register_transfer_routes(csilk_app_t* app)
{
    csilk_app_post_ext(app,
                       "/api/transfers",
                       transfers_create,
                       "transfer_req_t",
                       nullptr,
                       "Create transfer",
                       "Create a transfer between two assets (debit one, credit other)");
}
