#include "infrastructure/repositories/mcp_server_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 字段映射辅助：把查询行填充进 mf_mcp_server_t */
static void
fill_server_from_row(const csilk_json_t* r, mf_mcp_server_t* s)
{
    s->id = db_get_int(r, "id");
    s->user_id = db_get_int(r, "user_id");
    const char* name = csilk_json_get_string(r, "name");
    strncpy(s->name, name ? name : "", sizeof(s->name) - 1);
    s->name[sizeof(s->name) - 1] = '\0';

    const char* transport = csilk_json_get_string(r, "transport");
    s->transport =
        (transport && strcmp(transport, "stdio") == 0) ? MCP_TRANSPORT_STDIO : MCP_TRANSPORT_HTTP;

    const char* url = csilk_json_get_string(r, "url");
    strncpy(s->url, url ? url : "", sizeof(s->url) - 1);
    s->url[sizeof(s->url) - 1] = '\0';

    const char* command = csilk_json_get_string(r, "command");
    strncpy(s->command, command ? command : "", sizeof(s->command) - 1);
    s->command[sizeof(s->command) - 1] = '\0';

    const char* args_json = csilk_json_get_string(r, "args");
    strncpy(s->args_json, args_json ? args_json : "", sizeof(s->args_json) - 1);
    s->args_json[sizeof(s->args_json) - 1] = '\0';

    const char* env_json = csilk_json_get_string(r, "env");
    strncpy(s->env_json, env_json ? env_json : "", sizeof(s->env_json) - 1);
    s->env_json[sizeof(s->env_json) - 1] = '\0';

    const char* headers_json = csilk_json_get_string(r, "headers");
    strncpy(s->headers_json, headers_json ? headers_json : "", sizeof(s->headers_json) - 1);
    s->headers_json[sizeof(s->headers_json) - 1] = '\0';

    const char* secret_ref = csilk_json_get_string(r, "secret_ref");
    strncpy(s->secret_ref, secret_ref ? secret_ref : "", sizeof(s->secret_ref) - 1);
    s->secret_ref[sizeof(s->secret_ref) - 1] = '\0';

    s->enabled = db_get_bool(r, "enabled") != 0;
    s->timeout_ms = (int)db_get_int(r, "timeout_ms");

    const char* discovered_at = csilk_json_get_string(r, "discovered_at");
    strncpy(s->discovered_at, discovered_at ? discovered_at : "", sizeof(s->discovered_at) - 1);
    s->discovered_at[sizeof(s->discovered_at) - 1] = '\0';

    const char* created_at = csilk_json_get_string(r, "created_at");
    strncpy(s->created_at, created_at ? created_at : "", sizeof(s->created_at) - 1);
    s->created_at[sizeof(s->created_at) - 1] = '\0';

    const char* updated_at = csilk_json_get_string(r, "updated_at");
    strncpy(s->updated_at, updated_at ? updated_at : "", sizeof(s->updated_at) - 1);
    s->updated_at[sizeof(s->updated_at) - 1] = '\0';
}

static void
fill_tool_from_row(const csilk_json_t* r, mf_mcp_server_tool_t* t)
{
    t->id = db_get_int(r, "id");
    t->server_id = db_get_int(r, "server_id");
    t->user_id = db_get_int(r, "user_id");
    const char* tool_name = csilk_json_get_string(r, "tool_name");
    strncpy(t->tool_name, tool_name ? tool_name : "", sizeof(t->tool_name) - 1);
    t->tool_name[sizeof(t->tool_name) - 1] = '\0';

    const char* qualified_name = csilk_json_get_string(r, "qualified_name");
    strncpy(t->qualified_name, qualified_name ? qualified_name : "", sizeof(t->qualified_name) - 1);
    t->qualified_name[sizeof(t->qualified_name) - 1] = '\0';

    const char* description = csilk_json_get_string(r, "description");
    strncpy(t->description, description ? description : "", sizeof(t->description) - 1);
    t->description[sizeof(t->description) - 1] = '\0';

    const char* input_schema = csilk_json_get_string(r, "input_schema");
    strncpy(t->input_schema, input_schema ? input_schema : "", sizeof(t->input_schema) - 1);
    t->input_schema[sizeof(t->input_schema) - 1] = '\0';

    t->is_mutation = db_get_bool(r, "is_mutation") != 0;
    const char* risk_level = csilk_json_get_string(r, "risk_level");
    strncpy(t->risk_level, risk_level ? risk_level : "medium", sizeof(t->risk_level) - 1);
    t->risk_level[sizeof(t->risk_level) - 1] = '\0';

    const char* fetched_at = csilk_json_get_string(r, "fetched_at");
    strncpy(t->fetched_at, fetched_at ? fetched_at : "", sizeof(t->fetched_at) - 1);
    t->fetched_at[sizeof(t->fetched_at) - 1] = '\0';
}

int
mf_mcp_server_repo_list(void*             db_pool,
                        int64_t           user_id,
                        mf_mcp_server_t** out_list,
                        size_t*           out_count)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* rows =
        csilk_db_query_param_json(pool,
                                  "SELECT id, user_id, name, transport, url, command, args, env, "
                                  "headers, secret_ref, enabled, timeout_ms, discovered_at, "
                                  "created_at, updated_at "
                                  "FROM mcp_server WHERE user_id=? ORDER BY name",
                                  (const char*[]){uid_str, NULL});

    if (!rows) {
        *out_list = NULL;
        *out_count = 0;
        return -1;
    }

    size_t           n = csilk_json_array_size(rows);
    mf_mcp_server_t* list = n > 0 ? (mf_mcp_server_t*)calloc(n, sizeof(mf_mcp_server_t)) : NULL;
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* r = csilk_json_array_get(rows, i);
        fill_server_from_row(r, &list[i]);
    }
    csilk_json_free(rows);
    *out_list = list;
    *out_count = n;
    return 0;
}

int
mf_mcp_server_repo_find_by_id(void* db_pool, int64_t user_id, int64_t id, mf_mcp_server_t* out)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);

    csilk_json_t* rows =
        csilk_db_query_param_json(pool,
                                  "SELECT id, user_id, name, transport, url, command, args, env, "
                                  "headers, secret_ref, enabled, timeout_ms, discovered_at, "
                                  "created_at, updated_at "
                                  "FROM mcp_server WHERE id=? AND user_id=?",
                                  (const char*[]){id_str, uid_str, NULL});

    if (!rows) {
        return -1;
    }
    if (csilk_json_array_size(rows) == 0) {
        csilk_json_free(rows);
        return 1;
    }

    fill_server_from_row(csilk_json_array_get(rows, 0), out);
    csilk_json_free(rows);
    return 0;
}

int
mf_mcp_server_repo_create(void* db_pool, int64_t user_id, const mf_mcp_server_t* s, int64_t* out_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    const char* transport = (s->transport == MCP_TRANSPORT_STDIO) ? "stdio" : "http";
    const char* enabled = s->enabled ? "1" : "0";
    char        timeout_str[16];
    snprintf(timeout_str, sizeof(timeout_str), "%d", s->timeout_ms > 0 ? s->timeout_ms : 30000);

    /* 空值字段传 ""（不能用 NULL——NULL 在参数数组中会终止列表，见 asset_repo 约定） */
    char url[512] = "";
    char command[512] = "";
    char args_json[1024] = "";
    char env_json[2048] = "";
    char headers_json[2048] = "";
    char secret_ref[128] = "";
    if (s->url[0]) {
        snprintf(url, sizeof(url), "%s", s->url);
    }
    if (s->command[0]) {
        snprintf(command, sizeof(command), "%s", s->command);
    }
    if (s->args_json[0]) {
        snprintf(args_json, sizeof(args_json), "%s", s->args_json);
    }
    if (s->env_json[0]) {
        snprintf(env_json, sizeof(env_json), "%s", s->env_json);
    }
    if (s->headers_json[0]) {
        snprintf(headers_json, sizeof(headers_json), "%s", s->headers_json);
    }
    if (s->secret_ref[0]) {
        snprintf(secret_ref, sizeof(secret_ref), "%s", s->secret_ref);
    }

    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "INSERT INTO mcp_server (user_id, name, transport, url, command, "
                                  "args, env, headers, secret_ref, enabled, timeout_ms) "
                                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id",
                                  (const char*[]){uid_str,
                                                  s->name,
                                                  transport,
                                                  url,
                                                  command,
                                                  args_json,
                                                  env_json,
                                                  headers_json,
                                                  secret_ref,
                                                  enabled,
                                                  timeout_str,
                                                  NULL});

    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return -1;
    }
    *out_id = db_get_int(csilk_json_array_get(res, 0), "id");
    csilk_json_free(res);
    return 0;
}

int
mf_mcp_server_repo_update(void* db_pool, int64_t user_id, int64_t id, const mf_mcp_server_t* s)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);

    const char* transport = (s->transport == MCP_TRANSPORT_STDIO) ? "stdio" : "http";
    const char* enabled = s->enabled ? "1" : "0";
    char        timeout_str[16];
    snprintf(timeout_str, sizeof(timeout_str), "%d", s->timeout_ms > 0 ? s->timeout_ms : 30000);

    /* 空值字段传 ""（NULL 会终止参数数组，见 asset_repo 约定） */
    char url[512] = "", command[512] = "", args_json[1024] = "", env_json[2048] = "";
    char headers_json[2048] = "", secret_ref[128] = "";
    if (s->url[0]) {
        snprintf(url, sizeof(url), "%s", s->url);
    }
    if (s->command[0]) {
        snprintf(command, sizeof(command), "%s", s->command);
    }
    if (s->args_json[0]) {
        snprintf(args_json, sizeof(args_json), "%s", s->args_json);
    }
    if (s->env_json[0]) {
        snprintf(env_json, sizeof(env_json), "%s", s->env_json);
    }
    if (s->headers_json[0]) {
        snprintf(headers_json, sizeof(headers_json), "%s", s->headers_json);
    }
    if (s->secret_ref[0]) {
        snprintf(secret_ref, sizeof(secret_ref), "%s", s->secret_ref);
    }

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "UPDATE mcp_server SET name=?, transport=?, url=?, command=?, "
        "args=?, env=?, headers=?, secret_ref=?, enabled=?, timeout_ms=?, "
        "updated_at=CURRENT_TIMESTAMP WHERE id=? AND user_id=? RETURNING id",
        (const char*[]){s->name,
                        transport,
                        url,
                        command,
                        args_json,
                        env_json,
                        headers_json,
                        secret_ref,
                        enabled,
                        timeout_str,
                        id_str,
                        uid_str,
                        NULL});

    int ok = (res && csilk_json_array_size(res) > 0) ? 0 : 1;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int
mf_mcp_server_repo_delete(void* db_pool, int64_t user_id, int64_t id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);

    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "DELETE FROM mcp_server WHERE id=? AND user_id=? RETURNING id",
                                  (const char*[]){id_str, uid_str, NULL});

    int ok = (res && csilk_json_array_size(res) > 0) ? 0 : 1;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

void
mf_mcp_server_repo_free_list(mf_mcp_server_t* list, size_t count)
{
    (void)count;
    free(list);
}

/* ---- mcp_server_tool 缓存操作 ---- */

int
mf_mcp_server_tool_repo_load(void*                  db_pool,
                             int64_t                user_id,
                             int64_t                server_id,
                             mf_mcp_server_tool_t** out_list,
                             size_t*                out_count)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], sid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(sid_str, sizeof(sid_str), "%lld", (long long)server_id);

    csilk_json_t* rows = csilk_db_query_param_json(
        pool,
        "SELECT id, server_id, user_id, tool_name, qualified_name, description, input_schema, "
        "is_mutation, risk_level, fetched_at "
        "FROM mcp_server_tool WHERE user_id=? AND server_id=? ORDER BY tool_name",
        (const char*[]){uid_str, sid_str, NULL});

    if (!rows) {
        *out_list = NULL;
        *out_count = 0;
        return -1;
    }

    size_t                n = csilk_json_array_size(rows);
    mf_mcp_server_tool_t* list =
        n > 0 ? (mf_mcp_server_tool_t*)calloc(n, sizeof(mf_mcp_server_tool_t)) : NULL;
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* r = csilk_json_array_get(rows, i);
        fill_tool_from_row(r, &list[i]);
    }
    csilk_json_free(rows);
    *out_list = list;
    *out_count = n;
    return 0;
}

int
mf_mcp_server_tool_repo_upsert(void*                       db_pool,
                               int64_t                     user_id,
                               int64_t                     server_id,
                               const mf_mcp_server_tool_t* tools,
                               size_t                      count)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], sid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(sid_str, sizeof(sid_str), "%lld", (long long)server_id);

    /* 先删旧缓存 */
    if (csilk_db_exec_param(pool,
                            "DELETE FROM mcp_server_tool WHERE user_id=? AND server_id=?",
                            (const char*[]){uid_str, sid_str, NULL}) != 0) {
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        const mf_mcp_server_tool_t* t = &tools[i];
        const char*                 is_mutation = t->is_mutation ? "1" : "0";
        const char*                 risk_level = t->risk_level[0] ? t->risk_level : "medium";
        /* 空值传 ""（NULL 会终止参数数组；input_schema NOT NULL 故兜底 "{}"） */
        char desc_buf[512] = "";
        char schema_buf[4096] = "{}";
        if (t->description[0]) {
            snprintf(desc_buf, sizeof(desc_buf), "%s", t->description);
        }
        if (t->input_schema[0]) {
            snprintf(schema_buf, sizeof(schema_buf), "%s", t->input_schema);
        }

        if (csilk_db_exec_param(pool,
                                "INSERT INTO mcp_server_tool "
                                "(user_id, server_id, tool_name, qualified_name, description, "
                                "input_schema, is_mutation, risk_level, fetched_at) "
                                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)",
                                (const char*[]){uid_str,
                                                sid_str,
                                                t->tool_name,
                                                t->qualified_name,
                                                desc_buf,
                                                schema_buf,
                                                is_mutation,
                                                risk_level,
                                                NULL}) != 0) {
            return -1;
        }
    }
    return 0;
}

int
mf_mcp_server_tool_repo_load_for_user(void*                  db_pool,
                                      int64_t                user_id,
                                      mf_mcp_server_tool_t** out_list,
                                      size_t*                out_count)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    /* 仅返回启用服务器下的工具 */
    csilk_json_t* rows = csilk_db_query_param_json(
        pool,
        "SELECT t.id, t.server_id, t.user_id, t.tool_name, t.qualified_name, t.description, "
        "t.input_schema, t.is_mutation, t.risk_level, t.fetched_at "
        "FROM mcp_server_tool t JOIN mcp_server s ON t.server_id = s.id "
        "WHERE t.user_id=? AND s.enabled=1 ORDER BY t.qualified_name",
        (const char*[]){uid_str, NULL});

    if (!rows) {
        *out_list = NULL;
        *out_count = 0;
        return -1;
    }

    size_t                n = csilk_json_array_size(rows);
    mf_mcp_server_tool_t* list =
        n > 0 ? (mf_mcp_server_tool_t*)calloc(n, sizeof(mf_mcp_server_tool_t)) : NULL;
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* r = csilk_json_array_get(rows, i);
        fill_tool_from_row(r, &list[i]);
    }
    csilk_json_free(rows);
    *out_list = list;
    *out_count = n;
    return 0;
}

void
mf_mcp_server_tool_repo_free_list(mf_mcp_server_tool_t* list, size_t count)
{
    (void)count;
    free(list);
}
