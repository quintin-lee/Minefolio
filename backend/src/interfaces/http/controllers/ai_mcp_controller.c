#include "interfaces/http/controllers/ai_mcp_controller.h"
#include "application/mcp/usecases.h"
#include "application/mcp/commands.h"
#include "services/ai/tools/mcp/mcp_bridge.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

/* Helper: 从请求体 JSON 读取可选字符串字段（缺失返回 NULL） */
static const char*
body_get_str(const csilk_json_t* body, const char* key)
{
    const csilk_json_t* v = csilk_json_get(body, key);
    if (v && csilk_json_is_string(v)) {
        return csilk_json_string_value(v);
    }
    return NULL;
}

/* 创建 MCP 服务器 */
void
api_ai_mcp_servers_create(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    /* 读取 timeout_ms，缺省 30000 */
    int timeout_ms = (int)db_get_num(body, "timeout_ms");
    if (timeout_ms <= 0) {
        timeout_ms = 30000;
    }

    mf_mcp_create_cmd_t cmd = {
        .user_id = user_id,
        .name = body_get_str(body, "name"),
        .transport = body_get_str(body, "transport"),
        .url = body_get_str(body, "url"),
        .command = body_get_str(body, "command"),
        .args_json = body_get_str(body, "args"),
        .env_json = body_get_str(body, "env"),
        .headers_json = body_get_str(body, "headers"),
        .secret_ref = body_get_str(body, "secret_ref"),
        .timeout_ms = timeout_ms,
    };

    int64_t                 new_id = 0;
    mf_mcp_usecase_result_t res = {0};
    int                     rc = mf_mcp_usecase_create(db_get_pool(), &cmd, &new_id, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        csilk_json_t* resp = csilk_json_object();
        csilk_json_add_number(resp, "id", (double)new_id);
        respond_ok(c, resp);
    } else {
        respond_error(
            c, res.code ? res.code : 500, res.message[0] ? res.message : "创建 MCP 服务器失败");
    }
}

/* 更新 MCP 服务器 */
void
api_ai_mcp_servers_update(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    int timeout_ms = (int)db_get_num(body, "timeout_ms");
    if (timeout_ms <= 0) {
        timeout_ms = 30000;
    }

    mf_mcp_update_cmd_t cmd = {
        .user_id = user_id,
        .server_id = atoll(id_str),
        .name = body_get_str(body, "name"),
        .transport = body_get_str(body, "transport"),
        .url = body_get_str(body, "url"),
        .command = body_get_str(body, "command"),
        .args_json = body_get_str(body, "args"),
        .env_json = body_get_str(body, "env"),
        .headers_json = body_get_str(body, "headers"),
        .secret_ref = body_get_str(body, "secret_ref"),
        .timeout_ms = timeout_ms,
        .enabled = db_get_bool(body, "enabled") ? true : false,
    };

    mf_mcp_usecase_result_t res = {0};
    int                     rc = mf_mcp_usecase_update(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_error(
            c, res.code ? res.code : 500, res.message[0] ? res.message : "更新 MCP 服务器失败");
    }
}

/* 删除 MCP 服务器 */
void
api_ai_mcp_servers_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }

    mf_mcp_delete_cmd_t cmd = {
        .user_id = user_id,
        .server_id = atoll(id_str),
    };

    mf_mcp_usecase_result_t res = {0};
    int                     rc = mf_mcp_usecase_delete(db_get_pool(), &cmd, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_error(
            c, res.code ? res.code : 500, res.message[0] ? res.message : "删除 MCP 服务器失败");
    }
}

/* 列表（含每个服务器的工具缓存数） */
void
api_ai_mcp_servers_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    csilk_json_t* list = NULL;
    if (mf_mcp_usecase_list(db_get_pool(), user_id, &list) != 0) {
        respond_error(c, 500, "查询 MCP 服务器列表失败");
        return;
    }
    respond_ok(c, list ? list : csilk_json_array());
}

/* 单个服务器 */
void
api_ai_mcp_servers_get(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }

    csilk_json_t* obj = NULL;
    int           rc = mf_mcp_usecase_get(db_get_pool(), user_id, atoll(id_str), &obj);
    if (rc == 1) {
        respond_not_found(c);
        return;
    }
    if (rc != 0 || !obj) {
        respond_error(c, 500, "查询 MCP 服务器失败");
        return;
    }
    respond_ok(c, obj);
}

/* 读取某服务器已缓存的工具（不重拉） */
void
api_ai_mcp_servers_tools(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }

    csilk_json_t* list = NULL;
    if (mf_mcp_usecase_tools(db_get_pool(), user_id, atoll(id_str), &list) != 0) {
        respond_error(c, 500, "查询 MCP 工具失败");
        return;
    }
    respond_ok(c, list ? list : csilk_json_array());
}

/* 探活 / 强制重拉：initialize + tools/list 回写缓存，返回工具数 + 状态 */
static void
mcp_server_refresh_response(csilk_ctx_t* c, int64_t user_id, int64_t server_id)
{
    size_t tool_count = 0;
    char*  err = NULL;
    int    rc = mcp_bridge_refresh_server(db_get_pool(), user_id, server_id, &tool_count, &err);

    if (rc != 0) {
        csilk_json_t* data = csilk_json_object();
        csilk_json_add_int(data, "server_id", server_id);
        csilk_json_add_string(data, "status", "failed");
        if (err) {
            csilk_json_add_string(data, "message", err);
            free(err);
        }
        respond_ok(c, data);
        return;
    }

    csilk_json_t* data = csilk_json_object();
    csilk_json_add_int(data, "server_id", server_id);
    csilk_json_add_string(data, "status", "ok");
    csilk_json_add_int(data, "tool_count", (int)tool_count);
    respond_ok(c, data);
}

void
api_ai_mcp_servers_test(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }
    mcp_server_refresh_response(c, user_id, atoll(id_str));
}

void
api_ai_mcp_servers_refresh(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }
    mcp_server_refresh_response(c, user_id, atoll(id_str));
}

/* -------------------------------------------------------------------------- */
/* Stdio 脚本上传与管理接口                                                    */
/* -------------------------------------------------------------------------- */

_Thread_local static char*  g_script_filename = NULL;
_Thread_local static char*  g_script_data = NULL;
_Thread_local static size_t g_script_data_len = 0;
_Thread_local static size_t g_script_data_cap = 0;

static void
script_part_handler(csilk_multipart_part_t* part)
{
    if (!g_script_filename && part->filename[0]) {
        g_script_filename = strdup(part->filename);
    }
    if (part->data && part->data_len > 0) {
        size_t need = g_script_data_len + part->data_len;
        if (need > 5 * 1024 * 1024) {
            return;
        }
        if (need > g_script_data_cap) {
            size_t cap = g_script_data_cap ? g_script_data_cap : 16384;
            while (cap < need) {
                cap *= 2;
            }
            char* nd = realloc(g_script_data, cap);
            if (!nd) {
                return;
            }
            g_script_data = nd;
            g_script_data_cap = cap;
        }
        memcpy(g_script_data + g_script_data_len, part->data, part->data_len);
        g_script_data_len += part->data_len;
    }
}

static const char*
script_clean_filename(const char* raw)
{
    if (!raw) {
        return NULL;
    }
    const char* p = strrchr(raw, '/');
    if (p) {
        raw = p + 1;
    }
    p = strrchr(raw, '\\');
    if (p) {
        raw = p + 1;
    }
    return raw;
}

static bool
script_detect_interpreter(const char* filename, const char** out_interp)
{
    if (!filename || !filename[0]) {
        return false;
    }
    size_t len = strlen(filename);
    if (len > 128 || strstr(filename, "..") != NULL) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        char c = filename[i];
        if (!isalnum((unsigned char)c) && c != '_' && c != '-' && c != '.') {
            return false;
        }
    }

    if (len >= 3 && strcmp(filename + len - 3, ".py") == 0) {
        if (out_interp) {
            *out_interp = "python3";
        }
        return true;
    }
    if (len >= 3 && strcmp(filename + len - 3, ".js") == 0) {
        if (out_interp) {
            *out_interp = "node";
        }
        return true;
    }
    if (len >= 4 && strcmp(filename + len - 4, ".mjs") == 0) {
        if (out_interp) {
            *out_interp = "node";
        }
        return true;
    }
    if (len >= 3 && strcmp(filename + len - 3, ".sh") == 0) {
        if (out_interp) {
            *out_interp = "bash";
        }
        return true;
    }
    return false;
}

void
api_ai_mcp_scripts_upload(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    free(g_script_filename);
    free(g_script_data);
    g_script_filename = NULL;
    g_script_data = NULL;
    g_script_data_len = 0;
    g_script_data_cap = 0;

    csilk_multipart_parse(c, script_part_handler);

    if (!g_script_filename || !g_script_data || g_script_data_len == 0) {
        free(g_script_filename);
        free(g_script_data);
        g_script_filename = NULL;
        g_script_data = NULL;
        g_script_data_len = 0;
        g_script_data_cap = 0;
        respond_bad_request(c, "未接收到有效脚本文件内容");
        return;
    }

    const char* raw_name = script_clean_filename(g_script_filename);
    const char* interp = NULL;
    if (!raw_name || !script_detect_interpreter(raw_name, &interp)) {
        free(g_script_filename);
        free(g_script_data);
        g_script_filename = NULL;
        g_script_data = NULL;
        g_script_data_len = 0;
        g_script_data_cap = 0;
        respond_bad_request(c, "非法脚本类型，仅支持 .py, .js, .mjs, .sh");
        return;
    }

    /* 确保存储目录存在 */
    mkdir("./data", 0755);
    mkdir("./data/mcp_scripts", 0755);
    char user_dir[512];
    snprintf(user_dir, sizeof(user_dir), "./data/mcp_scripts/%lld", (long long)user_id);
    mkdir(user_dir, 0755);

    char abs_dir[1024];
    if (!realpath(user_dir, abs_dir)) {
        snprintf(abs_dir, sizeof(abs_dir), "%s", user_dir);
    }

    char full_path[1280];
    snprintf(full_path, sizeof(full_path), "%s/%s", abs_dir, raw_name);

    FILE* fp = fopen(full_path, "wb");
    if (!fp) {
        free(g_script_filename);
        free(g_script_data);
        g_script_filename = NULL;
        g_script_data = NULL;
        g_script_data_len = 0;
        g_script_data_cap = 0;
        respond_error(c, 500, "保存脚本文件失败");
        return;
    }
    fwrite(g_script_data, 1, g_script_data_len, fp);
    fclose(fp);
    chmod(full_path, 0755);

    char cmd[1400];
    snprintf(cmd, sizeof(cmd), "%s %s", interp, full_path);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "filename", raw_name);
    csilk_json_add_string(resp, "path", full_path);
    csilk_json_add_string(resp, "command", cmd);
    csilk_json_add_number(resp, "size", (double)g_script_data_len);

    free(g_script_filename);
    free(g_script_data);
    g_script_filename = NULL;
    g_script_data = NULL;
    g_script_data_len = 0;
    g_script_data_cap = 0;

    respond_ok(c, resp);
}

void
api_ai_mcp_scripts_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    char user_dir[512];
    snprintf(user_dir, sizeof(user_dir), "./data/mcp_scripts/%lld", (long long)user_id);

    csilk_json_t* arr = csilk_json_array();
    DIR*          dir = opendir(user_dir);
    if (!dir) {
        respond_ok(c, arr);
        return;
    }

    char abs_dir[1024];
    if (!realpath(user_dir, abs_dir)) {
        snprintf(abs_dir, sizeof(abs_dir), "%s", user_dir);
    }

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        const char* interp = NULL;
        if (!script_detect_interpreter(ent->d_name, &interp)) {
            continue;
        }

        char full_path[1280];
        snprintf(full_path, sizeof(full_path), "%s/%s", abs_dir, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }

        char cmd[1400];
        snprintf(cmd, sizeof(cmd), "%s %s", interp, full_path);

        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "filename", ent->d_name);
        csilk_json_add_string(item, "path", full_path);
        csilk_json_add_string(item, "command", cmd);
        csilk_json_add_number(item, "size", (double)st.st_size);
        csilk_json_add_number(item, "updated_at", (double)st.st_mtime);
        csilk_json_array_append(arr, item);
    }
    closedir(dir);

    respond_ok(c, arr);
}

void
api_ai_mcp_scripts_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* filename = csilk_get_param(c, "filename");
    if (!filename || !filename[0]) {
        respond_bad_request(c, "缺少文件名");
        return;
    }

    const char* raw_name = script_clean_filename(filename);
    const char* interp = NULL;
    if (!raw_name || !script_detect_interpreter(raw_name, &interp)) {
        respond_bad_request(c, "非法文件名");
        return;
    }

    char user_dir[512];
    snprintf(user_dir, sizeof(user_dir), "./data/mcp_scripts/%lld", (long long)user_id);
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s/%s", user_dir, raw_name);

    if (unlink(full_path) == 0) {
        csilk_json_t* resp = csilk_json_object();
        csilk_json_add_bool(resp, "deleted", true);
        respond_ok(c, resp);
    } else {
        respond_not_found(c);
    }
}

void
register_ai_mcp_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/ai/mcp/servers",
                      api_ai_mcp_servers_list,
                      nullptr,
                      "mcp_server_list_resp_t",
                      "List MCP servers",
                      "Returns all MCP servers for the current user with tool cache counts");

    csilk_app_post_ext(app,
                       "/api/ai/mcp/servers",
                       api_ai_mcp_servers_create,
                       "mcp_server_req_t",
                       "mcp_server_create_resp_t",
                       "Create MCP server",
                       "Create a new MCP server configuration");

    csilk_app_get_ext(app,
                      "/api/ai/mcp/servers/:id",
                      api_ai_mcp_servers_get,
                      nullptr,
                      "mcp_server_resp_t",
                      "Get MCP server",
                      "Get a single MCP server by ID");

    csilk_app_put_ext(app,
                      "/api/ai/mcp/servers/:id",
                      api_ai_mcp_servers_update,
                      "mcp_server_req_t",
                      "mcp_server_resp_t",
                      "Update MCP server",
                      "Update an existing MCP server");

    csilk_app_delete_ext(app,
                         "/api/ai/mcp/servers/:id",
                         api_ai_mcp_servers_delete,
                         nullptr,
                         nullptr,
                         "Delete MCP server",
                         "Delete an MCP server and its cached tools");

    csilk_app_get_ext(app,
                      "/api/ai/mcp/servers/:id/tools",
                      api_ai_mcp_servers_tools,
                      nullptr,
                      "mcp_tool_list_resp_t",
                      "List cached MCP tools",
                      "Returns cached tool definitions for the given server");

    csilk_app_post_ext(app,
                       "/api/ai/mcp/servers/:id/test",
                       api_ai_mcp_servers_test,
                       nullptr,
                       nullptr,
                       "Test MCP server connection",
                       "Probe the MCP server (initialize + tools/list); M2 implementation");

    csilk_app_post_ext(app,
                       "/api/ai/mcp/servers/:id/refresh",
                       api_ai_mcp_servers_refresh,
                       nullptr,
                       nullptr,
                       "Refresh MCP tools",
                       "Force re-fetch tools/list; M2 implementation");

    csilk_app_post_ext(app,
                       "/api/ai/mcp/scripts",
                       api_ai_mcp_scripts_upload,
                       nullptr,
                       "mcp_script_upload_resp_t",
                       "Upload MCP script",
                       "Upload a custom python/node/bash script for stdio MCP execution");

    csilk_app_get_ext(app,
                      "/api/ai/mcp/scripts",
                      api_ai_mcp_scripts_list,
                      nullptr,
                      "mcp_script_list_resp_t",
                      "List uploaded MCP scripts",
                      "List all custom scripts uploaded by current user");

    csilk_app_delete_ext(app,
                         "/api/ai/mcp/scripts/:filename",
                         api_ai_mcp_scripts_delete,
                         nullptr,
                         nullptr,
                         "Delete MCP script",
                         "Delete an uploaded custom MCP script");
}
