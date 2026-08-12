#include "common/balance.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <string.h>

int balance_direction(const char* asset_type) {
    if (!asset_type) return 1;
    if (strcmp(asset_type, "loan") == 0 ||
        strcmp(asset_type, "credit_card") == 0 ||
        strcmp(asset_type, "other_liability") == 0) {
        return -1;
    }
    return 1;
}

int balance_apply_delta(csilk_db_pool_t* pool,
                        int64_t asset_id, int64_t user_id, double delta,
                        const char* source_type, int64_t source_id,
                        const char* note) {
    if (!pool || asset_id <= 0 || user_id <= 0 || !source_type) return -1;

    char sql[512];

    // 1. 查询资产归属与类型（asset_type 存于 categories，必须 JOIN）
    snprintf(sql, sizeof(sql),
        "SELECT a.current_value, c.asset_type "
        "FROM assets a JOIN categories c ON a.category_id = c.id "
        "WHERE a.id=%lld AND a.user_id=%lld",
        (long long)asset_id, (long long)user_id);
    csilk_json_t* row = csilk_db_query_json(pool, sql);
    if (!row) return -2;
    if (csilk_json_array_size(row) == 0) {
        csilk_json_free(row);
        return -1;  // 资产不存在或不属于该用户
    }
    const csilk_json_t* asset = csilk_json_array_get(row, 0);
    const char* asset_type = csilk_json_get_string(asset, "asset_type");

    // 2. 归一化 delta（负债方向反转）
    double signed_delta = delta * balance_direction(asset_type);
    csilk_json_free(row);

    // 3. 原子更新余额（避免读改写竞态）
    snprintf(sql, sizeof(sql),
        "UPDATE assets SET current_value = current_value + %.2f, "
        "updated_at = CURRENT_TIMESTAMP WHERE id=%lld AND user_id=%lld",
        signed_delta, (long long)asset_id, (long long)user_id);
    if (csilk_db_exec(pool, sql) != 0) return -2;

    // 4. 读取变动后余额（balance_after 快照）
    snprintf(sql, sizeof(sql),
        "SELECT current_value FROM assets WHERE id=%lld AND user_id=%lld",
        (long long)asset_id, (long long)user_id);
    csilk_json_t* after = csilk_db_query_json(pool, sql);
    if (!after || csilk_json_array_size(after) == 0) {
        if (after) csilk_json_free(after);
        return -2;
    }
    double balance_after = db_get_num(csilk_json_array_get(after, 0), "current_value");
    csilk_json_free(after);

    // 5. 写审计日志（delta 存已反转的 signed_delta）
    snprintf(sql, sizeof(sql),
        "INSERT INTO asset_balance_logs (asset_id, user_id, delta, balance_after, "
        "source_type, source_id, note) "
        "VALUES (%lld, %lld, %.2f, %.2f, '%s', %lld, '%s')",
        (long long)asset_id, (long long)user_id, signed_delta, balance_after,
        source_type, (long long)source_id, note ? note : "");
    if (csilk_db_exec(pool, sql) != 0) return -2;

    return 0;
}
