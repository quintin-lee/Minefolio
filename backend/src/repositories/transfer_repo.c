/**
 * @file transfer_repo.c
 * @brief 账户间内部划转流水数据访问层具体实现
 *
 * 实现了双边账户所有权原子校验与划转流水表的插入持久化逻辑。
 */

#include "repositories/transfer_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief 校验转账双方资产账户的有效性与所有权
 *
 * 执行 SQL：`SELECT COUNT(*) as cnt FROM assets WHERE id IN (?, ?) AND user_id=?`
 * 判断 COUNT 返回是否严格等于 2。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param from_id 转出资产 ID
 * @param to_id 转入资产 ID
 * @return int 双方均存在且归属当前用户返回 1，否则返回 0
 */
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

/**
 * @brief 插入转账流水记录
 *
 * 执行 SQL：
 * `INSERT INTO transfers (user_id, from_asset_id, to_asset_id, amount, currency, transfer_date, note) VALUES (?, ?, ?, ?, ?, ?, ?) RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param from_id 转出账户 ID
 * @param to_id 转入账户 ID
 * @param amount 划转金额
 * @param currency 货币单位
 * @param date 转账日期
 * @param note 备注
 * @return int64_t 成功生成的主键 ID，失败返回 0
 */
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
