#include "repositories/transfer_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

int
transfer_asset_check(csilk_db_pool_t* pool, int64_t user_id, int64_t from_id, int64_t to_id)
{
    char uid[32], fid[32], tid[32];
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
transfer_insert(csilk_db_pool_t* pool,
                int64_t          user_id,
                int64_t          from_id,
                int64_t          to_id,
                double           amount,
                const char*      currency,
                const char*      date,
                const char*      note)
{
    char uid[32], fid[32], tid[32], amt[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(fid, sizeof(fid), "%lld", (long long)from_id);
    snprintf(tid, sizeof(tid), "%lld", (long long)to_id);
    snprintf(amt, sizeof(amt), "%.6f", amount);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO transfers (user_id, from_asset_id, to_asset_id, amount, currency, "
        "transfer_date, note) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) RETURNING id",
        (const char*[]){
            uid, fid, tid, amt, currency ? currency : "CNY", date, note ? note : "", NULL});
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return id;
}
