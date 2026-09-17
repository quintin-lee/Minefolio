#include "application/mcp/usecases.h"
#include "domain/mcp/entity.h"
#include "domain/mcp/rules.h"
#include "domain/mcp/repository.h"
#include "infrastructure/repositories/mcp_server_repo_impl.h"
#include "config/secret.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 是否允许内网/回环地址（MINEFOLIO_MCP_ALLOW_LOCAL=1） */
static bool
mcp_allow_local(void)
{
    const char* v = config_env_get("MINEFOLIO_MCP_ALLOW_LOCAL", NULL, 0, "0");
    return v && strcmp(v, "1") == 0;
}

/* 校验创建/更新命令中 transport 与相关字段的组合合法性 */
static int
validate_cmd_common(const char*              name,
                    const char*              transport,
                    const char*              url,
                    const char*              command,
                    int                      timeout_ms,
                    mf_mcp_usecase_result_t* res)
{
    if (!mf_mcp_rule_validate_name(name)) {
        res->code = 1002;
        snprintf(res->message, sizeof(res->message), "MCP 名称非法：需 1-64 位小写字母/数字/_/-");
        return -1;
    }
    if (!mf_mcp_rule_validate_transport(transport)) {
        res->code = 1002;
        snprintf(res->message, sizeof(res->message), "transport 必须为 http 或 stdio");
        return -1;
    }
    if (strcmp(transport, "http") == 0) {
        if (!mf_mcp_rule_validate_url(url, mcp_allow_local())) {
            res->code = 1002;
            snprintf(res->message,
                     sizeof(res->message),
                     "url 非法：需 http/https，且 host 不得为内网/回环地址（除非 "
                     "MINEFOLIO_MCP_ALLOW_LOCAL=1）");
            return -1;
        }
    } else {
        if (!mf_mcp_rule_validate_command(command)) {
            res->code = 1002;
            snprintf(res->message,
                     sizeof(res->message),
                     "command 非法：长度 ≤ 512 且禁止 shell 元字符 ; & | ` $ > <");
            return -1;
        }
    }
    if (!mf_mcp_rule_validate_timeout_ms(timeout_ms)) {
        res->code = 1002;
        snprintf(res->message, sizeof(res->message), "timeout_ms 需在 1000-120000 范围内");
        return -1;
    }
    return 0;
}

/* 把 mcp_server_t 转为 JSON 对象。tool_count 由调用方经一次 GROUP BY 查询预取，
   本函数不再逐 server 全量 load 工具缓存（旧实现 N 个 server = N 次全量 load）。 */
static csilk_json_t*
server_to_json(const mf_mcp_server_t* s, size_t tool_count)
{
    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_number(obj, "id", (double)s->id);
    csilk_json_add_number(obj, "user_id", (double)s->user_id);
    csilk_json_add_string(obj, "name", s->name);
    csilk_json_add_string(
        obj, "transport", (s->transport == MCP_TRANSPORT_STDIO) ? "stdio" : "http");
    csilk_json_add_string(obj, "url", s->url[0] ? s->url : "");
    csilk_json_add_string(obj, "command", s->command[0] ? s->command : "");
    csilk_json_add_string(obj, "args", s->args_json);
    csilk_json_add_string(obj, "env", s->env_json);
    csilk_json_add_string(obj, "headers", s->headers_json);
    csilk_json_add_string(obj, "secret_ref", s->secret_ref);
    csilk_json_add_number(obj, "enabled", s->enabled ? 1.0 : 0.0);
    csilk_json_add_number(obj, "timeout_ms", (double)s->timeout_ms);
    csilk_json_add_string(obj, "discovered_at", s->discovered_at);
    csilk_json_add_string(obj, "created_at", s->created_at);
    csilk_json_add_string(obj, "updated_at", s->updated_at);
    csilk_json_add_number(obj, "tool_count", (double)tool_count);

    return obj;
}

/* 在 counts 映射中查找某 server 的工具数；缺失视为 0。 */
static size_t
count_for_server(const mf_mcp_tool_count_t* pairs, size_t pair_count, int64_t server_id)
{
    for (size_t i = 0; i < pair_count; i++) {
        if (pairs[i].server_id == server_id) {
            return pairs[i].count;
        }
    }
    return 0;
}

int
mf_mcp_usecase_list(void* pool, int64_t user_id, csilk_json_t** out_list)
{
    mf_mcp_server_t* servers = NULL;
    size_t           count = 0;
    if (mf_mcp_server_repo_list(pool, user_id, &servers, &count) != 0) {
        return -1;
    }

    /* 一次 GROUP BY 取回所有服务器的工具数，替代旧的逐 server 全量 load。 */
    mf_mcp_tool_count_t* pairs = NULL;
    size_t               pair_count = 0;
    mf_mcp_server_tool_repo_counts_for_user(pool, user_id, &pairs, &pair_count);

    csilk_json_t* arr = csilk_json_array();
    for (size_t i = 0; i < count; i++) {
        size_t tc = count_for_server(pairs, pair_count, servers[i].id);
        csilk_json_add_item(arr, server_to_json(&servers[i], tc));
    }
    mf_mcp_tool_counts_free(pairs, pair_count);
    mf_mcp_server_repo_free_list(servers, count);
    *out_list = arr;
    return 0;
}

int
mf_mcp_usecase_get(void* pool, int64_t user_id, int64_t server_id, csilk_json_t** out_obj)
{
    mf_mcp_server_t s = {0};
    int             rc = mf_mcp_server_repo_find_by_id(pool, user_id, server_id, &s);
    if (rc == 1) {
        *out_obj = NULL;
        return 1; /* 不存在 */
    }
    if (rc != 0) {
        return -1;
    }
    /* 单条 get 也走同一 GROUP BY 映射（仅该用户全部缓存），取该 server 计数。 */
    mf_mcp_tool_count_t* pairs = NULL;
    size_t               pair_count = 0;
    mf_mcp_server_tool_repo_counts_for_user(pool, user_id, &pairs, &pair_count);
    size_t tc = count_for_server(pairs, pair_count, s.id);
    *out_obj = server_to_json(&s, tc);
    mf_mcp_tool_counts_free(pairs, pair_count);
    return 0;
}

int
mf_mcp_usecase_create(void*                      pool,
                      const mf_mcp_create_cmd_t* cmd,
                      int64_t*                   out_id,
                      mf_mcp_usecase_result_t*   out_res)
{
    mf_mcp_usecase_result_t local = {0};
    if (!out_res) {
        out_res = &local;
    }

    int rc = validate_cmd_common(
        cmd->name, cmd->transport, cmd->url, cmd->command, cmd->timeout_ms, out_res);
    if (rc != 0) {
        return rc;
    }

    mf_mcp_server_t s = {0};
    strncpy(s.name, cmd->name, sizeof(s.name) - 1);
    s.name[sizeof(s.name) - 1] = '\0';
    s.transport = (strcmp(cmd->transport, "stdio") == 0) ? MCP_TRANSPORT_STDIO : MCP_TRANSPORT_HTTP;
    if (cmd->url) {
        strncpy(s.url, cmd->url, sizeof(s.url) - 1);
    }
    if (cmd->command) {
        strncpy(s.command, cmd->command, sizeof(s.command) - 1);
    }
    if (cmd->args_json) {
        strncpy(s.args_json, cmd->args_json, sizeof(s.args_json) - 1);
    }
    if (cmd->env_json) {
        strncpy(s.env_json, cmd->env_json, sizeof(s.env_json) - 1);
    }
    if (cmd->headers_json) {
        strncpy(s.headers_json, cmd->headers_json, sizeof(s.headers_json) - 1);
    }
    if (cmd->secret_ref) {
        strncpy(s.secret_ref, cmd->secret_ref, sizeof(s.secret_ref) - 1);
    }
    s.enabled = true;
    s.timeout_ms = cmd->timeout_ms > 0 ? cmd->timeout_ms : 30000;

    if (mf_mcp_server_repo_create(pool, cmd->user_id, &s, out_id) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "创建 MCP 服务器失败");
        return -1;
    }
    out_res->code = 0;
    return 0;
}

int
mf_mcp_usecase_update(void* pool, const mf_mcp_update_cmd_t* cmd, mf_mcp_usecase_result_t* out_res)
{
    if (!out_res) {
        static mf_mcp_usecase_result_t local;
        out_res = &local;
    }

    int rc = validate_cmd_common(
        cmd->name, cmd->transport, cmd->url, cmd->command, cmd->timeout_ms, out_res);
    if (rc != 0) {
        return rc;
    }

    mf_mcp_server_t s = {0};
    strncpy(s.name, cmd->name, sizeof(s.name) - 1);
    s.name[sizeof(s.name) - 1] = '\0';
    s.transport = (strcmp(cmd->transport, "stdio") == 0) ? MCP_TRANSPORT_STDIO : MCP_TRANSPORT_HTTP;
    if (cmd->url) {
        strncpy(s.url, cmd->url, sizeof(s.url) - 1);
    }
    if (cmd->command) {
        strncpy(s.command, cmd->command, sizeof(s.command) - 1);
    }
    if (cmd->args_json) {
        strncpy(s.args_json, cmd->args_json, sizeof(s.args_json) - 1);
    }
    if (cmd->env_json) {
        strncpy(s.env_json, cmd->env_json, sizeof(s.env_json) - 1);
    }
    if (cmd->headers_json) {
        strncpy(s.headers_json, cmd->headers_json, sizeof(s.headers_json) - 1);
    }
    if (cmd->secret_ref) {
        strncpy(s.secret_ref, cmd->secret_ref, sizeof(s.secret_ref) - 1);
    }
    s.enabled = cmd->enabled;
    s.timeout_ms = cmd->timeout_ms > 0 ? cmd->timeout_ms : 30000;

    int up_rc = mf_mcp_server_repo_update(pool, cmd->user_id, cmd->server_id, &s);
    if (up_rc == 1) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "MCP 服务器不存在");
    } else if (up_rc != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "更新 MCP 服务器失败");
    }
    return up_rc == 0 ? 0 : up_rc;
}

int
mf_mcp_usecase_delete(void* pool, const mf_mcp_delete_cmd_t* cmd, mf_mcp_usecase_result_t* out_res)
{
    if (!out_res) {
        static mf_mcp_usecase_result_t local;
        out_res = &local;
    }

    int rc = mf_mcp_server_repo_delete(pool, cmd->user_id, cmd->server_id);
    if (rc == 1) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "MCP 服务器不存在");
    } else if (rc != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "删除 MCP 服务器失败");
    }
    return rc == 0 ? 0 : rc;
}

int
mf_mcp_usecase_tools(void* pool, int64_t user_id, int64_t server_id, csilk_json_t** out_list)
{
    mf_mcp_server_t s = {0};
    int             rc = mf_mcp_server_repo_find_by_id(pool, user_id, server_id, &s);
    if (rc != 0) {
        *out_list = NULL;
        return rc; /* 1=不存在, -1=数据库错误 */
    }

    mf_mcp_server_tool_t* tools = NULL;
    size_t                count = 0;
    if (mf_mcp_server_tool_repo_load(pool, user_id, server_id, &tools, &count) != 0) {
        *out_list = csilk_json_array();
        return 0;
    }

    csilk_json_t* arr = csilk_json_array();
    for (size_t i = 0; i < count; i++) {
        const mf_mcp_server_tool_t* t = &tools[i];
        csilk_json_t*               obj = csilk_json_object();
        csilk_json_add_number(obj, "id", (double)t->id);
        csilk_json_add_number(obj, "server_id", (double)t->server_id);
        csilk_json_add_string(obj, "tool_name", t->tool_name);
        csilk_json_add_string(obj, "qualified_name", t->qualified_name);
        csilk_json_add_string(obj, "description", t->description);
        csilk_json_add_string(obj, "input_schema", t->input_schema);
        csilk_json_add_number(obj, "is_mutation", t->is_mutation ? 1.0 : 0.0);
        csilk_json_add_string(obj, "risk_level", t->risk_level);
        csilk_json_add_string(obj, "fetched_at", t->fetched_at);
        csilk_json_add_item(arr, obj);
    }
    mf_mcp_server_tool_repo_free_list(tools, count);
    *out_list = arr;
    return 0;
}
