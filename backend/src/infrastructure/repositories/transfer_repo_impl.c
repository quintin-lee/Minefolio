/**
 * @file transfer_repo_impl.c
 * @brief 转账仓储 SQL 实现 (Infrastructure Transfer Repository)
 */

#include "infrastructure/repositories/transfer_repo_impl.h"
#include "common/db.h"
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
        "SELECT COUNT(*) as cnt FROM assets WHERE id IN (?, ?) AND user_id=?",
        (const char*[]){fid, tid, uid, NULL});
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
    char             uid[32], fid[32], tid[32], amt[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(fid, sizeof(fid), "%lld", (long long)transfer->from_asset_id);
    snprintf(tid, sizeof(tid), "%lld", (long long)transfer->to_asset_id);
    snprintf(amt, sizeof(amt), "%.6f", transfer->amount);

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO transfers (user_id, from_asset_id, to_asset_id, amount, currency, "
        "transfer_date, note) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) RETURNING id",
        (const char*[]){uid,
                        fid,
                        tid,
                        amt,
                        transfer->currency[0] ? transfer->currency : "CNY",
                        transfer->transfer_date,
                        transfer->note[0] ? transfer->note : "",
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

int
mf_transfer_repo_insert_out_transaction(void*       db_pool,
                                        int64_t     user_id,
                                        int64_t     asset_id,
                                        double      amount,
                                        const char* currency,
                                        const char* date,
                                        const char* note)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid[32], aid[32], amt[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(aid, sizeof(aid), "%lld", (long long)asset_id);
    snprintf(amt, sizeof(amt), "%.6f", amount);

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO transactions (user_id, asset_id, source_type, transaction_type, amount, "
        "currency, transaction_date, note) "
        "VALUES (?, ?, 'expense', 'transfer_out', ?, ?, ?, ?) RETURNING id",
        (const char*[]){uid, aid, amt, currency, date, note ? note : "", NULL});
    int ok = (res && csilk_json_array_size(res) > 0) ? 0 : -1;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int
mf_transfer_repo_insert_in_transaction(void*       db_pool,
                                       int64_t     user_id,
                                       int64_t     asset_id,
                                       double      amount,
                                       const char* currency,
                                       const char* date,
                                       const char* note)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid[32], aid[32], amt[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(aid, sizeof(aid), "%lld", (long long)asset_id);
    snprintf(amt, sizeof(amt), "%.6f", amount);

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO transactions (user_id, asset_id, source_type, transaction_type, amount, "
        "currency, transaction_date, note) "
        "VALUES (?, ?, 'income', 'transfer_in', ?, ?, ?, ?) RETURNING id",
        (const char*[]){uid, aid, amt, currency, date, note ? note : "", NULL});
    int ok = (res && csilk_json_array_size(res) > 0) ? 0 : -1;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}
