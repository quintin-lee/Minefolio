#include "interfaces/http/controllers/mcp_server_controller.h"

#include "csilk/protocols/mcp.h"
#include "common/ctx.h"
#include "common/db.h"
#include "services/ai_tools.h"
#include "services/ai/tools/registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 取 JSON-RPC 请求体的 method 字符串；缺失返回 NULL */
static const char*
mcp_req_method(const csilk_json_t* body)
{
    const csilk_json_t* m = csilk_json_get(body, "method");
    if (csilk_json_is_string(m)) {
        return csilk_json_string_value(m);
    }
    return NULL;
}

/* 取 JSON-RPC 请求体顶层 "id"（JSON 值，可为 number/string/null）；缺失 NULL */
static const csilk_json_t*
mcp_req_id(const csilk_json_t* body)
{
    return csilk_json_get(body, "id");
}

/* 取 JSON-RPC 请求体 "params" 对象；缺失返回空对象（调用方 free） */
static csilk_json_t*
mcp_req_params(const csilk_json_t* body)
{
    const csilk_json_t* p = csilk_json_get(body, "params");
    return p ? csilk_json_copy(p) : csilk_json_object();
}

/* 组装 JSON-RPC 成功/错误响应并写出（csilk_json 接管所有权，不 free） */
static void
mcp_send_response(csilk_ctx_t*        c,
                  const csilk_json_t* id,
                  csilk_json_t*       result,
                  int                 error_code,
                  const char*         error_msg)
{
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "jsonrpc", "2.0");
    if (id && id != csilk_json_null()) {
        /* id 指向请求体子树（body 所有）；add_object 会夺取节点所有权，
           故先深拷贝再入 resp，避免在 body 释放后 use-after-free。 */
        csilk_json_add_object(resp, "id", csilk_json_copy((csilk_json_t*)id));
    } else {
        csilk_json_add_null(resp, "id");
    }
    if (error_code != 0) {
        csilk_json_t* err = csilk_json_object();
        csilk_json_add_int(err, "code", (int)error_code);
        csilk_json_add_string(err, "message", error_msg ? error_msg : "internal error");
        csilk_json_add_object(resp, "error", err);
    } else {
        csilk_json_add_object(resp, "result", result ? result : csilk_json_object());
    }
    csilk_json(c, CSILK_STATUS_OK, resp);
}

/* initialize：回服务器能力声明 */
static void
mcp_handle_initialize(csilk_ctx_t* c, const csilk_json_t* body)
{
    (void)body;
    csilk_json_t* result = csilk_json_object();
    csilk_json_add_string(result, "protocolVersion", "2024-11-05");

    csilk_json_t* server_info = csilk_json_object();
    csilk_json_add_string(server_info, "name", "minefolio");
    csilk_json_add_string(server_info, "version", "1.0.0");
    csilk_json_add_object(result, "serverInfo", server_info);

    csilk_json_t* caps = csilk_json_object();
    csilk_json_add_object(caps, "tools", csilk_json_object());
    csilk_json_add_object(result, "capabilities", caps);

    mcp_send_response(c, mcp_req_id(body), result, 0, NULL);
}

/* tools/list：枚举内置 ai_tool_t 注册表 */
static void
mcp_handle_tools_list(csilk_ctx_t* c, const csilk_json_t* body)
{
    (void)body;
    size_t                 count = 0;
    const csilk_ai_tool_t* defs = ai_tool_get_csilk_definitions(&count);

    csilk_json_t* result = csilk_json_object();
    csilk_json_t* tools = csilk_json_array();
    for (size_t i = 0; i < count; i++) {
        csilk_json_t* t = csilk_json_object();
        csilk_json_add_string(t, "name", defs[i].function.name ? defs[i].function.name : "");
        csilk_json_add_string(
            t, "description", defs[i].function.description ? defs[i].function.description : "");
        /* inputSchema = 工具的 parameters_schema（csilk_json_t*，经 void* 持有）。
           该节点属于静态注册表的 schema 子树；add_object 会夺取所有权，
           故深拷贝入 resp，避免破坏 registry 持有的静态 schema。 */
        csilk_json_t* schema = (csilk_json_t*)defs[i].function.parameters_json;
        csilk_json_add_object(
            t, "inputSchema", schema ? csilk_json_copy(schema) : csilk_json_object());
        csilk_json_add_item(tools, t);
    }
    csilk_json_add_object(result, "tools", tools);

    mcp_send_response(c, mcp_req_id(body), result, 0, NULL);
}

/* tools/call：取 params.name/arguments → ai_tools_execute_parsed → 包 content */
static void
mcp_handle_tools_call(csilk_ctx_t* c, const csilk_json_t* body, int64_t user_id)
{
    csilk_json_t* params = mcp_req_params(body);

    const csilk_json_t* name_json = csilk_json_get(params, "name");
    const char* name = csilk_json_is_string(name_json) ? csilk_json_string_value(name_json) : NULL;
    if (!name || !name[0]) {
        csilk_json_free(params);
        mcp_send_response(c, mcp_req_id(body), NULL, -32602, "missing tool name in params");
        return;
    }

    /* arguments 为 params.arguments（csilk_json_t*，缺省 {}） */
    const csilk_json_t* args = csilk_json_get(params, "arguments");
    csilk_json_t* args_json = args ? csilk_json_copy((csilk_json_t*)args) : csilk_json_object();

    char* result_str = ai_tools_execute_parsed(db_get_pool(), user_id, 0, args_json, name);

    csilk_json_t* result = csilk_json_object();
    csilk_json_t* content = csilk_json_array();
    csilk_json_t* text_obj = csilk_json_object();
    csilk_json_add_string(text_obj, "type", "text");
    csilk_json_add_string(
        text_obj, "text", result_str ? result_str : "{\"error\":\"tool returned null\"}");
    csilk_json_add_item(content, text_obj);
    csilk_json_add_object(result, "content", content);

    free(result_str);
    csilk_json_free(args_json);
    csilk_json_free(params);

    mcp_send_response(c, mcp_req_id(body), result, 0, NULL);
}

/* POST /mcp — JSON-RPC 2.0 端点（streamable-HTTP transport） */
void
mcp_endpoint_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        /* 未认证：JSON-RPC error -32001（unauthorized），HTTP 仍 200 承载 JSON-RPC 体 */
        csilk_json_t* body = csilk_bind_json(c);
        mcp_send_response(c, body ? mcp_req_id(body) : NULL, NULL, -32001, "unauthorized");
        csilk_json_free(body);
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        mcp_send_response(c, NULL, NULL, -32700, "invalid JSON-RPC body");
        return;
    }

    const char* method = mcp_req_method(body);
    if (!method) {
        mcp_send_response(c, mcp_req_id(body), NULL, -32600, "missing method");
        csilk_json_free(body);
        return;
    }

    if (strcmp(method, "initialize") == 0) {
        mcp_handle_initialize(c, body);
    } else if (strcmp(method, "tools/list") == 0) {
        mcp_handle_tools_list(c, body);
    } else if (strcmp(method, "tools/call") == 0) {
        mcp_handle_tools_call(c, body, user_id);
    } else if (strcmp(method, "ping") == 0) {
        mcp_send_response(c, mcp_req_id(body), csilk_json_object(), 0, NULL);
    } else {
        mcp_send_response(c, mcp_req_id(body), NULL, -32601, "method not found");
    }
    csilk_json_free(body);
}

void
register_mcp_server_routes(csilk_app_t* app)
{
    csilk_app_post_ext(app,
                       "/mcp",
                       mcp_endpoint_handler,
                       NULL,
                       NULL,
                       "MCP Server",
                       "streamable-HTTP JSON-RPC 2.0 endpoint");
}
