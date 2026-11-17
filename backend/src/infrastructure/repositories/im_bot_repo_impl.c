/** @file im_bot_repo_impl.c @brief IM 机器人绑定关系仓储 (im_bot_bindings) */

#include "infrastructure/repositories/im_bot_repo_impl.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t
mf_im_bot_repo_lookup(csilk_db_pool_t* pool,
                      const char*      platform,
                      const char*      im_user_id,
                      char*            scope_out,
                      size_t           scope_sz)
{
    if (!pool || !platform || !platform[0] || !im_user_id || !im_user_id[0]) {
        return 0;
    }
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "SELECT mf_user_id, scope FROM im_bot_bindings "
                                  "WHERE platform=? AND im_user_id=? AND enabled=1 LIMIT 1",
                                  (const char*[]){platform, im_user_id, NULL});
    int64_t mf_user_id = 0;
    if (res && csilk_json_is_array(res) && csilk_json_array_size(res) > 0) {
        csilk_json_t* row = csilk_json_array_get(res, 0);
        mf_user_id = db_get_int(row, "mf_user_id");
        if (scope_out && scope_sz > 0) {
            const char* scope = csilk_json_get_string(row, "scope");
            snprintf(scope_out, scope_sz, "%s", scope ? scope : "readonly");
        }
    }
    if (res) {
        csilk_json_free(res);
    }
    return mf_user_id;
}

int
mf_im_bot_repo_upsert(csilk_db_pool_t* pool,
                      const char*      platform,
                      const char*      im_user_id,
                      int64_t          mf_user_id,
                      const char*      scope)
{
    if (!pool || !platform || platform[0] == 0 || !im_user_id || im_user_id[0] == 0 ||
        mf_user_id <= 0) {
        return -1;
    }
    const char* eff_scope = (scope && scope[0]) ? scope : "readonly";
    char        uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)mf_user_id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO im_bot_bindings (platform, im_user_id, mf_user_id, scope, enabled) "
        "VALUES (?, ?, ?, ?, 1) "
        "ON CONFLICT(platform, im_user_id) DO UPDATE SET mf_user_id = excluded.mf_user_id, "
        "scope = excluded.scope, enabled = 1",
        (const char*[]){platform, im_user_id, uid_str, eff_scope, NULL});
    int rc = 0;
    if (res) {
        csilk_json_free(res);
    }
    return rc;
}

csilk_json_t*
mf_im_bot_repo_list_for_user(csilk_db_pool_t* pool, int64_t mf_user_id)
{
    if (!pool || mf_user_id <= 0) {
        return csilk_json_array();
    }
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%lld", (long long)mf_user_id);
    return csilk_db_query_param_json(
        pool,
        "SELECT platform, im_user_id, scope, enabled, created_at "
        "FROM im_bot_bindings WHERE mf_user_id=? ORDER BY platform, im_user_id",
        (const char*[]){id_str, NULL});
}

int
mf_im_bot_repo_unbind(csilk_db_pool_t* pool,
                      const char*      platform,
                      const char*      im_user_id,
                      int64_t          mf_user_id)
{
    if (!pool || !platform || platform[0] == 0 || !im_user_id || im_user_id[0] == 0 ||
        mf_user_id <= 0) {
        return -1;
    }
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%lld", (long long)mf_user_id);
    int rc = csilk_db_exec_param(
        pool,
        "DELETE FROM im_bot_bindings WHERE platform=? AND im_user_id=? AND mf_user_id=?",
        (const char*[]){platform, im_user_id, id_str, NULL});
    return rc;
}
