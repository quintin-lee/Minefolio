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

    char asset_id_str[32], user_id_str[32];
    snprintf(asset_id_str, sizeof(asset_id_str), "%lld", (long long)asset_id);
    snprintf(user_id_str, sizeof(user_id_str), "%lld", (long long)user_id);

    // 1. 查询资产归属与类型（asset_type 存于 categories，必须 JOIN）
    const char* params1[] = { asset_id_str, user_id_str, NULL };
    csilk_json_t* row = csilk_db_query_param_json(pool,
        "SELECT a.current_value, c.asset_type "
        "FROM assets a JOIN categories c ON a.category_id = c.id "
        "WHERE a.id=? AND a.user_id=?", params1);
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
    char signed_delta_str[64];
    snprintf(signed_delta_str, sizeof(signed_delta_str), "%.6f", signed_delta);
    const char* params3[] = { signed_delta_str, asset_id_str, user_id_str, NULL };
    csilk_json_t* res3 = csilk_db_query_param_json(pool,
        "UPDATE assets SET current_value = current_value + ?, "
        "updated_at = CURRENT_TIMESTAMP WHERE id=? AND user_id=?", params3);
    if (res3) csilk_json_free(res3);

    // 4. 读取变动后余额（balance_after 快照）
    const char* params4[] = { asset_id_str, user_id_str, NULL };
    csilk_json_t* after = csilk_db_query_param_json(pool,
        "SELECT current_value FROM assets WHERE id=? AND user_id=?", params4);
    if (!after || csilk_json_array_size(after) == 0) {
        if (after) csilk_json_free(after);
        return -2;
    }
    double balance_after = db_get_num(csilk_json_array_get(after, 0), "current_value");
    csilk_json_free(after);

    // 5. 写审计日志（delta 存已反转的 signed_delta）
    char balance_after_str[64], source_id_str[32];
    snprintf(balance_after_str, sizeof(balance_after_str), "%.6f", balance_after);
    snprintf(source_id_str, sizeof(source_id_str), "%lld", (long long)source_id);
    const char* params5[] = {
        asset_id_str, user_id_str, signed_delta_str, balance_after_str,
        source_type, source_id_str, note ? note : "", NULL
    };
    csilk_json_t* res5 = csilk_db_query_param_json(pool,
        "INSERT INTO asset_balance_logs (asset_id, user_id, delta, balance_after, "
        "source_type, source_id, note) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)", params5);
    if (res5) csilk_json_free(res5);

    return 0;
}
