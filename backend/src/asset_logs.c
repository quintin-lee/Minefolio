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

    csilk_db_pool_t* pool = db_get_pool();

    // Optional filter: asset_id
    const char* asset_id_str = csilk_get_query(c, "asset_id");
    // Pagination: limit (default 100), page (default 1)
    const char* limit_str = csilk_get_query(c, "limit");
    const char* page_str = csilk_get_query(c, "page");

    long limit = 100;
    long page = 1;
    if (limit_str) limit = strtol(limit_str, NULL, 10);
    if (page_str) page = strtol(page_str, NULL, 10);
    if (limit <= 0) limit = 100;
    if (page <= 0) page = 1;
    long offset = (page - 1) * limit;

    char sql[512];
    if (asset_id_str && strlen(asset_id_str) > 0) {
        int64_t aid = atoll(asset_id_str);
        snprintf(sql, sizeof(sql),
            "SELECT abl.id, abl.asset_id, a.name AS asset_name, abl.user_id, "
            "abl.delta, abl.balance_after, abl.source_type, abl.source_id, "
            "abl.note, abl.created_at "
            "FROM asset_balance_logs abl "
            "LEFT JOIN assets a ON abl.asset_id = a.id "
            "WHERE abl.user_id=%lld AND abl.asset_id=%lld "
            "ORDER BY abl.created_at DESC LIMIT %ld OFFSET %ld",
            (long long)user_id, aid, limit, offset);
    } else {
        snprintf(sql, sizeof(sql),
            "SELECT abl.id, abl.asset_id, a.name AS asset_name, abl.user_id, "
            "abl.delta, abl.balance_after, abl.source_type, abl.source_id, "
            "abl.note, abl.created_at "
            "FROM asset_balance_logs abl "
            "LEFT JOIN assets a ON abl.asset_id = a.id "
            "WHERE abl.user_id=%lld "
            "ORDER BY abl.created_at DESC LIMIT %ld OFFSET %ld",
            (long long)user_id, limit, offset);
    }

    csilk_json_t* result = csilk_db_query_json(pool, sql);
    if (!result) { respond_error(c, 500, "查询失败"); return; }
    respond_ok(c, result);
}
