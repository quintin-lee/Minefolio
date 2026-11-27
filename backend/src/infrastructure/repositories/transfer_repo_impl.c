/**
 * @file transfer_repo_impl.c
 * @brief 转账仓储 SQL 实现 (Infrastructure Transfer Repository)
 */

#include "infrastructure/repositories/transfer_repo_impl.h"
#include "common/db.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int
mf_transfer_repo_asset_check(void* db_pool, int64_t user_id, int64_t from_id, int64_t to_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid[32], fid[32], tid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(fid, sizeof(fid), "%lld", (long long)from_id);
    snprintf(tid, sizeof(tid), "%lld", (long long)to_id);

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT COUNT(*) as cnt FROM assets a "
        "WHERE a.id IN (?, ?) AND (a.user_id=? OR (a.ledger_id > 0 AND EXISTS "
        "(SELECT 1 FROM ledger_members lm WHERE lm.ledger_id = a.ledger_id AND lm.user_id=?)))",
        (const char*[]){fid, tid, uid, uid, NULL});
    int ok = 0;
    if (res && csilk_json_array_size(res) > 0) {
        ok = (db_get_num(csilk_json_array_get(res, 0), "cnt") == 2);
    }
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int64_t
mf_transfer_repo_insert(void* db_pool, int64_t user_id, const mf_transfer_t* transfer)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid[32], fid[32], tid[32], amt[64], lid[32], from_tx[32], to_tx[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(fid, sizeof(fid), "%lld", (long long)transfer->from_asset_id);
    snprintf(tid, sizeof(tid), "%lld", (long long)transfer->to_asset_id);
    snprintf(amt, sizeof(amt), "%.6f", transfer->amount);
    snprintf(lid, sizeof(lid), "%lld", (long long)transfer->ledger_id);
    snprintf(from_tx, sizeof(from_tx), "%lld", (long long)transfer->from_tx_id);
    snprintf(to_tx, sizeof(to_tx), "%lld", (long long)transfer->to_tx_id);

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO transfers (user_id, from_asset_id, to_asset_id, amount, currency, "
        "transfer_date, note, ledger_id, from_tx_id, to_tx_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, "
        "COALESCE(NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0), (SELECT ledger_id FROM assets WHERE "
        "id=?)), "
        "NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0), "
        "NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0)) "
        "RETURNING id",
        (const char*[]){uid,
                        fid,
                        tid,
                        amt,
                        transfer->currency[0] ? transfer->currency : "CNY",
                        transfer->transfer_date,
                        transfer->note[0] ? transfer->note : "",
                        lid,
                        fid,
                        from_tx,
                        to_tx,
                        NULL});
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return id;
}

int64_t
mf_transfer_repo_insert_out_transaction_fx(void*       db_pool,
                                           int64_t     user_id,
                                           int64_t     asset_id,
                                           int64_t     linked_asset_id,
                                           double      amount,
                                           const char* currency,
                                           double      fx_rate_to_cny,
                                           const char* date,
                                           const char* note,
                                           int64_t     ledger_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid[32], aid[32], laid[32], amt[64], fx_buf[32], lid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(aid, sizeof(aid), "%lld", (long long)asset_id);
    snprintf(laid, sizeof(laid), "%lld", (long long)linked_asset_id);
    snprintf(amt, sizeof(amt), "%.6f", amount);
    double safe_fx = (isfinite(fx_rate_to_cny) && fx_rate_to_cny > 0.0) ? fx_rate_to_cny : 1.0;
    snprintf(fx_buf, sizeof(fx_buf), "%.8f", safe_fx);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO transactions (user_id, asset_id, linked_asset_id, source_type, "
        "transaction_type, direction, linked_direction, amount, currency, fx_rate_to_cny, "
        "transaction_date, note, ledger_id) "
        "VALUES (?, ?, ?, 'expense', 'transfer_out', 'out', 'in', ?, ?, ?, ?, ?, "
        "COALESCE(NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0), (SELECT ledger_id FROM assets WHERE "
        "id=?))) "
        "RETURNING id",
        (const char*[]){
            uid, aid, laid, amt, currency, fx_buf, date, note ? note : "", lid, aid, NULL});
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return id;
}

int64_t
mf_transfer_repo_insert_out_transaction(void*       db_pool,
                                        int64_t     user_id,
                                        int64_t     asset_id,
                                        int64_t     linked_asset_id,
                                        double      amount,
                                        const char* currency,
                                        const char* date,
                                        const char* note,
                                        int64_t     ledger_id)
{
    return mf_transfer_repo_insert_out_transaction_fx(
        db_pool, user_id, asset_id, linked_asset_id, amount, currency, 1.0, date, note, ledger_id);
}

int64_t
mf_transfer_repo_insert_in_transaction_fx(void*       db_pool,
                                          int64_t     user_id,
                                          int64_t     asset_id,
                                          int64_t     linked_asset_id,
                                          double      amount,
                                          const char* currency,
                                          double      fx_rate_to_cny,
                                          const char* date,
                                          const char* note,
                                          int64_t     parent_tx_id,
                                          int64_t     ledger_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid[32], aid[32], laid[32], amt[64], fx_buf[32], pid[32], lid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(aid, sizeof(aid), "%lld", (long long)asset_id);
    snprintf(laid, sizeof(laid), "%lld", (long long)linked_asset_id);
    snprintf(amt, sizeof(amt), "%.6f", amount);
    double safe_fx = (isfinite(fx_rate_to_cny) && fx_rate_to_cny > 0.0) ? fx_rate_to_cny : 1.0;
    snprintf(fx_buf, sizeof(fx_buf), "%.8f", safe_fx);
    snprintf(pid, sizeof(pid), "%lld", (long long)parent_tx_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO transactions (user_id, asset_id, linked_asset_id, source_type, "
        "transaction_type, direction, linked_direction, amount, currency, fx_rate_to_cny, "
        "transaction_date, note, parent_tx_id, ledger_id) "
        "VALUES (?, ?, ?, 'income', 'transfer_in', 'in', 'out', ?, ?, ?, ?, ?, "
        "NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0), "
        "COALESCE(NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0), (SELECT ledger_id FROM assets WHERE "
        "id=?))) "
        "RETURNING id",
        (const char*[]){
            uid, aid, laid, amt, currency, fx_buf, date, note ? note : "", pid, lid, aid, NULL});
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return id;
}

int64_t
mf_transfer_repo_insert_in_transaction(void*       db_pool,
                                       int64_t     user_id,
                                       int64_t     asset_id,
                                       int64_t     linked_asset_id,
                                       double      amount,
                                       const char* currency,
                                       const char* date,
                                       const char* note,
                                       int64_t     parent_tx_id,
                                       int64_t     ledger_id)
{
    return mf_transfer_repo_insert_in_transaction_fx(db_pool,
                                                     user_id,
                                                     asset_id,
                                                     linked_asset_id,
                                                     amount,
                                                     currency,
                                                     1.0,
                                                     date,
                                                     note,
                                                     parent_tx_id,
                                                     ledger_id);
}

int
transfer_repo_delete_paired(void*   pool,
                            int64_t user_id,
                            int64_t tx_id,
                            int64_t from_asset_id,
                            int64_t to_asset_id,
                            double  amount)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    char uid_str[32], tid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(tid_str, sizeof(tid_str), "%lld", (long long)tx_id);

    csilk_json_t* del_res = NULL;
    if (tx_id > 0) {
        del_res = csilk_db_query_param_json(
            (csilk_db_pool_t*)pool,
            "DELETE FROM transfers WHERE user_id=? AND (from_tx_id=? OR to_tx_id=?) RETURNING id",
            (const char*[]){uid_str, tid_str, tid_str, NULL});
    }

    if (!del_res || csilk_json_array_size(del_res) == 0) {
        if (from_asset_id > 0 && to_asset_id > 0) {
            char fa_str[32], ta_str[32], amt_str[64];
            snprintf(fa_str, sizeof(fa_str), "%lld", (long long)from_asset_id);
            snprintf(ta_str, sizeof(ta_str), "%lld", (long long)to_asset_id);
            snprintf(amt_str, sizeof(amt_str), "%.6f", amount);
            csilk_json_free(
                csilk_db_query_param_json((csilk_db_pool_t*)pool,
                                          "DELETE FROM transfers WHERE id IN ("
                                          "  SELECT id FROM transfers WHERE user_id=? AND "
                                          "from_asset_id=? AND to_asset_id=? AND amount=? "
                                          "  ORDER BY id DESC LIMIT 1)",
                                          (const char*[]){uid_str, fa_str, ta_str, amt_str, NULL}));
        }
    }
    if (del_res) {
        csilk_json_free(del_res);
    }
    return 0;
}
