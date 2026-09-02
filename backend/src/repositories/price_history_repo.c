/**
 * @file price_history_repo.c
 * @brief 投资资产历史价格走势数据访问层具体实现
 *
 * 实现了适配 PostgreSQL / SQLite 差异的幂等 UPSERT 历史净值写入，
 * 以及带租户鉴权校验的时间序列价格数据升序检索。
 */

#include "repositories/price_history_repo.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief 记录或更新资产历史价格快照 (UPSERT)
 *
 * 根据 `db_is_postgres()` 动态选择 SQL 语法分支：
 * - PostgreSQL: 带 `CAST(? AS DATE)` 和 `CAST(? AS DOUBLE PRECISION)` 显式类型转换及 `EXCLUDED.price`。
 * - SQLite: 原生类型推导与 `excluded.price`。
 *
 * @param pool 数据库连接池指针
 * @param asset_id 资产 ID
 * @param price_date 价格对应日期
 * @param price 单价/单位净值
 * @param currency 货币单位
 * @return int 成功返回 0，失败返回 -1
 */
int
price_history_record(csilk_db_pool_t* pool,
                     int64_t          asset_id,
                     const char*      price_date,
                     double           price,
                     const char*      currency)
{
    char aid_str[32], price_str[64];
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    snprintf(price_str, sizeof(price_str), "%.4f", price);
    const char* cur = (currency && currency[0]) ? currency : "CNY";

    if (db_is_postgres()) {
        const char* sql = "INSERT INTO asset_price_history (asset_id, price_date, price, currency) "
                          "VALUES (?, CAST(? AS DATE), CAST(? AS DOUBLE PRECISION), ?) "
                          "ON CONFLICT(asset_id, price_date) DO UPDATE SET price=EXCLUDED.price, "
                          "currency=EXCLUDED.currency";
        const char* params[] = {aid_str, price_date, price_str, cur, NULL};
        return csilk_db_exec_param(pool, sql, params);
    } else {
        const char* sql = "INSERT INTO asset_price_history (asset_id, price_date, price, currency) "
                          "VALUES (?, ?, ?, ?) "
                          "ON CONFLICT(asset_id, price_date) DO UPDATE SET price=excluded.price, "
                          "currency=excluded.currency";
        const char* params[] = {aid_str, price_date, price_str, cur, NULL};
        return csilk_db_exec_param(pool, sql, params);
    }
}

/**
 * @brief 按时间正序查询资产历史走势曲线
 *
 * 执行 JOIN 查询关联 `assets` 校验 `a.user_id = ?`，按 `h.price_date ASC` 升序获取指定条数。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param asset_id 资产 ID
 * @param limit 返回最大数据点数（默认 90）
 * @return csilk_json_t* 历史价格序列 JSON 数组
 */
csilk_json_t*
price_history_list_by_asset(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id, int limit)
{
    char uid_str[32], aid_str[32], lim_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    snprintf(lim_str, sizeof(lim_str), "%d", limit > 0 ? limit : 90);

    const char* sql = "SELECT h.id, h.asset_id, "
                      "CAST(h.price_date AS TEXT) as price_date, "
                      "COALESCE(CAST(h.price AS REAL), 0.0) as price, "
                      "h.currency, "
                      "CAST(h.created_at AS TEXT) as created_at "
                      "FROM asset_price_history h "
                      "JOIN assets a ON h.asset_id = a.id "
                      "WHERE a.user_id = ? AND h.asset_id = ? "
                      "ORDER BY h.price_date ASC LIMIT ?";
    return csilk_db_query_param_json(pool, sql, (const char*[]){uid_str, aid_str, lim_str, NULL});
}
