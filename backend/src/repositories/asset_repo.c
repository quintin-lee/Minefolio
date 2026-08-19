#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void asset_logs_list(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    int64_t page = 1, page_size = 20;
    parse_page_params(c, &page, &page_size);

    csilk_db_pool_t* pool = db_get_pool();

    // Optional filter: asset_id
    const char* asset_id_str = csilk_get_query(c, "asset_id");

    char uid_str[32], limit_buf[32], offset_buf[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(limit_buf, sizeof(limit_buf), "%lld", (long long)page_size);
    snprintf(offset_buf, sizeof(offset_buf), "%lld", (long long)((page - 1) * page_size));

    char count_sql[256];
    const char* cnt_params[4];
    snprintf(count_sql, sizeof(count_sql),
        "SELECT COUNT(*) AS cnt FROM asset_balance_logs abl WHERE abl.user_id=?");
    cnt_params[0] = uid_str;
    int cnt_pidx = 1;

    csilk_json_t* result = NULL;
    if (asset_id_str && strlen(asset_id_str) > 0) {
        char aid_buf[32];
        snprintf(aid_buf, sizeof(aid_buf), "%lld", atoll(asset_id_str));
        const char* params[] = { uid_str, aid_buf, limit_buf, offset_buf, NULL };
        result = csilk_db_query_param_json(pool,
            "SELECT abl.id, abl.asset_id, a.name AS asset_name, abl.user_id, "
            "abl.delta, abl.balance_after, abl.source_type, abl.source_id, "
            "abl.note, abl.created_at "
            "FROM asset_balance_logs abl "
            "LEFT JOIN assets a ON abl.asset_id = a.id "
            "WHERE abl.user_id=? AND abl.asset_id=? "
            "ORDER BY abl.created_at DESC LIMIT ? OFFSET ?", params);
        snprintf(count_sql + strlen(count_sql), sizeof(count_sql) - strlen(count_sql),
            " AND abl.asset_id=?");
        cnt_params[cnt_pidx++] = aid_buf;
    } else {
        const char* params[] = { uid_str, limit_buf, offset_buf, NULL };
        result = csilk_db_query_param_json(pool,
            "SELECT abl.id, abl.asset_id, a.name AS asset_name, abl.user_id, "
            "abl.delta, abl.balance_after, abl.source_type, abl.source_id, "
            "abl.note, abl.created_at "
            "FROM asset_balance_logs abl "
            "LEFT JOIN assets a ON abl.asset_id = a.id "
            "WHERE abl.user_id=? "
            "ORDER BY abl.created_at DESC LIMIT ? OFFSET ?", params);
    }
    cnt_params[cnt_pidx] = NULL;

    if (!result) { respond_error(c, 500, "查询失败"); return; }

    csilk_json_t* cnt_res = csilk_db_query_param_json(pool, count_sql, cnt_params);
    int64_t total = 0;
    if (cnt_res && csilk_json_array_size(cnt_res) > 0) {
        total = db_get_int(csilk_json_array_get(cnt_res, 0), "cnt");
    }
    if (cnt_res) csilk_json_free(cnt_res);

    respond_page_ok(c, result, total, page, page_size);
}

void register_asset_repo_routes(csilk_app_t* app) {
    csilk_app_get_ext(app, "/api/asset-balance-logs", asset_logs_list,
                      nullptr, nullptr, "Asset balance logs",
                      "Returns paginated asset balance change logs with optional asset_id filter");
}
