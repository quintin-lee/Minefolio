#include "infrastructure/repositories/skill_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

csilk_json_t*
skill_repo_list(csilk_db_pool_t* pool, int64_t user_id, const char* category, int enabled_only)
{
    if (!pool) {
        return NULL;
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    char sql[512];
    snprintf(sql,
             sizeof(sql),
             "SELECT id, user_id, name, description, license, compatibility, category, tags, "
             "enabled, instructions_l2, backend, exec_root, exec_path, exec_args, exec_env, "
             "timeout_ms, max_output_bytes, is_builtin, created_at, updated_at "
             "FROM ai_skills WHERE user_id = ?%s%s "
             "ORDER BY is_builtin DESC, name ASC",
             (category && category[0]) ? " AND category = ?" : "",
             enabled_only ? " AND enabled = 1" : "");

    if (category && category[0]) {
        return csilk_db_query_param_json(pool, sql, (const char*[]){uid_str, category, NULL});
    } else {
        return csilk_db_query_param_json(pool, sql, (const char*[]){uid_str, NULL});
    }
}

csilk_json_t*
skill_repo_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    if (!pool) {
        return NULL;
    }
    char uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);

    const char* sql =
        "SELECT id, user_id, name, description, license, compatibility, category, tags, "
        "enabled, instructions_l2, backend, exec_root, exec_path, exec_args, exec_env, "
        "timeout_ms, max_output_bytes, is_builtin, created_at, updated_at "
        "FROM ai_skills WHERE user_id = ? AND id = ?";

    csilk_json_t* arr =
        csilk_db_query_param_json(pool, sql, (const char*[]){uid_str, id_str, NULL});
    if (!arr) {
        return NULL;
    }
    if (csilk_json_array_size(arr) == 0) {
        csilk_json_free(arr);
        return NULL;
    }
    csilk_json_t* item = csilk_json_copy(csilk_json_array_get(arr, 0));
    csilk_json_free(arr);
    return item;
}

csilk_json_t*
skill_repo_get_by_name(csilk_db_pool_t* pool, int64_t user_id, const char* name)
{
    if (!pool || !name) {
        return NULL;
    }
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    const char* sql =
        "SELECT id, user_id, name, description, license, compatibility, category, tags, "
        "enabled, instructions_l2, backend, exec_root, exec_path, exec_args, exec_env, "
        "timeout_ms, max_output_bytes, is_builtin, created_at, updated_at "
        "FROM ai_skills WHERE user_id = ? AND name = ?";

    csilk_json_t* arr = csilk_db_query_param_json(pool, sql, (const char*[]){uid_str, name, NULL});
    if (!arr) {
        return NULL;
    }
    if (csilk_json_array_size(arr) == 0) {
        csilk_json_free(arr);
        return NULL;
    }
    csilk_json_t* item = csilk_json_copy(csilk_json_array_get(arr, 0));
    csilk_json_free(arr);
    return item;
}

int64_t
skill_repo_insert(csilk_db_pool_t* pool, int64_t user_id, const mf_skill_t* skill)
{
    if (!pool || !skill) {
        return -1;
    }

    char uid_str[32], enabled_str[16], timeout_str[16], max_out_str[16], builtin_str[16];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(enabled_str, sizeof(enabled_str), "%d", skill->enabled);
    snprintf(timeout_str,
             sizeof(timeout_str),
             "%d",
             skill->exec_cfg.timeout_ms > 0 ? skill->exec_cfg.timeout_ms : 30000);
    snprintf(max_out_str,
             sizeof(max_out_str),
             "%d",
             skill->exec_cfg.max_output_bytes > 0 ? skill->exec_cfg.max_output_bytes : 65536);
    snprintf(builtin_str, sizeof(builtin_str), "%d", skill->is_builtin ? 1 : 0);

    const char* backend_str = (skill->backend == MF_SKILL_BACKEND_REMOTE) ? "remote" : "local";

    const char* sql =
        "INSERT INTO ai_skills (user_id, name, description, license, compatibility, category, "
        "tags, "
        "enabled, instructions_l2, backend, exec_root, exec_path, exec_args, exec_env, "
        "timeout_ms, max_output_bytes, is_builtin, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP, "
        "CURRENT_TIMESTAMP) "
        "RETURNING id";

    const char* params[] = {uid_str,
                            skill->name,
                            skill->description,
                            skill->license[0] ? skill->license : "",
                            skill->compatibility[0] ? skill->compatibility : "",
                            skill->category[0] ? skill->category : "finance",
                            skill->tags[0] ? skill->tags : "",
                            enabled_str,
                            skill->instructions_l2 ? skill->instructions_l2 : "",
                            backend_str,
                            skill->exec_cfg.root[0] ? skill->exec_cfg.root : "",
                            skill->exec_cfg.path[0] ? skill->exec_cfg.path : "",
                            skill->exec_cfg.args[0] ? skill->exec_cfg.args : "",
                            skill->exec_cfg.env[0] ? skill->exec_cfg.env : "",
                            timeout_str,
                            max_out_str,
                            builtin_str,
                            NULL};

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    if (!res) {
        return -1;
    }

    int64_t new_id = -1;
    if (csilk_json_array_size(res) > 0) {
        csilk_json_t* row = csilk_json_array_get(res, 0);
        new_id = (int64_t)db_get_num(row, "id");
    }
    csilk_json_free(res);
    return new_id;
}

int
skill_repo_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const mf_skill_t* skill)
{
    if (!pool || !skill) {
        return -1;
    }

    char uid_str[32], id_str[32], timeout_str[16], max_out_str[16], en_str[16];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);
    snprintf(en_str, sizeof(en_str), "%d", skill->enabled ? 1 : 0);
    snprintf(timeout_str,
             sizeof(timeout_str),
             "%d",
             skill->exec_cfg.timeout_ms > 0 ? skill->exec_cfg.timeout_ms : 30000);
    snprintf(max_out_str,
             sizeof(max_out_str),
             "%d",
             skill->exec_cfg.max_output_bytes > 0 ? skill->exec_cfg.max_output_bytes : 65536);

    const char* backend_str = (skill->backend == MF_SKILL_BACKEND_REMOTE) ? "remote" : "local";

    const char* sql =
        "UPDATE ai_skills SET "
        "description = ?, license = ?, compatibility = ?, category = ?, tags = ?, "
        "enabled = ?, instructions_l2 = ?, backend = ?, exec_root = ?, exec_path = ?, exec_args = "
        "?, exec_env = ?, "
        "timeout_ms = ?, max_output_bytes = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE user_id = ? AND id = ? RETURNING id";

    const char* params[] = {skill->description,
                            skill->license[0] ? skill->license : "",
                            skill->compatibility[0] ? skill->compatibility : "",
                            skill->category[0] ? skill->category : "finance",
                            skill->tags[0] ? skill->tags : "",
                            en_str,
                            skill->instructions_l2 ? skill->instructions_l2 : "",
                            backend_str,
                            skill->exec_cfg.root[0] ? skill->exec_cfg.root : "",
                            skill->exec_cfg.path[0] ? skill->exec_cfg.path : "",
                            skill->exec_cfg.args[0] ? skill->exec_cfg.args : "",
                            skill->exec_cfg.env[0] ? skill->exec_cfg.env : "",
                            timeout_str,
                            max_out_str,
                            uid_str,
                            id_str,
                            NULL};

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    if (!res) {
        return -1;
    }
    int success = (csilk_json_array_size(res) > 0) ? 0 : -1;
    csilk_json_free(res);
    return success;
}

int
skill_repo_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    if (!pool) {
        return -1;
    }
    char uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);

    const char*   sql = "DELETE FROM ai_skills WHERE user_id = ? AND id = ? RETURNING id";
    csilk_json_t* res =
        csilk_db_query_param_json(pool, sql, (const char*[]){uid_str, id_str, NULL});
    if (!res) {
        return -1;
    }
    int success = (csilk_json_array_size(res) > 0) ? 0 : -1;
    csilk_json_free(res);
    return success;
}

int
skill_repo_toggle(csilk_db_pool_t* pool, int64_t user_id, int64_t id, int enabled)
{
    if (!pool) {
        return -1;
    }
    char uid_str[32], id_str[32], en_str[16];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);
    snprintf(en_str, sizeof(en_str), "%d", enabled ? 1 : 0);

    const char*   sql = "UPDATE ai_skills SET enabled = ?, updated_at = CURRENT_TIMESTAMP WHERE "
                        "user_id = ? AND id = ? RETURNING id";
    csilk_json_t* res =
        csilk_db_query_param_json(pool, sql, (const char*[]){en_str, uid_str, id_str, NULL});
    if (!res) {
        return -1;
    }
    int success = (csilk_json_array_size(res) > 0) ? 0 : -1;
    csilk_json_free(res);
    return success;
}
