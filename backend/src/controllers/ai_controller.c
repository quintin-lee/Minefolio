/**
 * @file ai_controller.c
 * @brief AI 智能助手、对话会话管理、配置及财务工作流控制器实现文件
 *
 * 遵循三层 C 架构规范，本控制器负责 AI 交互相关 HTTP 路由映射与调度：
 * - 对话与流式输出: 调度 services/ai_service.c 与 SSE 传输；
 * - 会话与消息持久化: 调度 repositories/ai_session_repo.h；
 * - 配置加解密与热重载: 调度 common/ai_config.h 与 config/key_manager.h；
 * - 智能财务工作流: 调度 services/ai_workflow_service.c。
 */

#include "controllers/ai_controller.h"
#include "services/ai_service.h"
#include "services/ai_workflow_service.h"
#include "repositories/ai_session_repo.h"
#include "repositories/ai_settings_repo.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/ai_config.h"
#include "config/key_manager.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 将 JSON 字符串数组解析转换为 C 字符串指针数组
 *
 * @param[in]  arr       JSON 数组对象指针
 * @param[out] out_ptrs  输出字符串数组指针
 * @param[out] out_count 输出数组元素个数
 */
static void
parse_string_array(const csilk_json_t* arr, char*** out_ptrs, int* out_count)
{
    if (!arr || !csilk_json_is_array(arr)) {
        *out_ptrs = NULL;
        *out_count = 0;
        return;
    }
    int n = csilk_json_array_size(arr);
    *out_ptrs = (char**)malloc(sizeof(char*) * (size_t)(n + 1));
    if (!*out_ptrs) {
        *out_count = 0;
        return;
    }
    *out_count = n;
    for (size_t i = 0; i < (size_t)n; i++) {
        const char* s = csilk_json_string_value(csilk_json_array_get(arr, i));
        (*out_ptrs)[i] = s ? strdup(s) : strdup("");
    }
    (*out_ptrs)[n] = NULL;
}

/**
 * @brief 获取已配置的所有 AI 供应商及其可用模型列表
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/ai/models
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": [{"provider_id": "openai", "provider_name": "OpenAI", "models": ["gpt-4o", ...]}, ...]}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void
ai_models_handler(csilk_ctx_t* c)
{
    ai_config_t* cfg = ai_get_config();
    if (!cfg) {
        respond_error(c, 500, "AI config not loaded");
        return;
    }

    csilk_json_t* out = csilk_json_array();
    for (int i = 0; i < cfg->provider_count; i++) {
        csilk_json_t* p = csilk_json_object();
        csilk_json_add_string(p, "provider_id", cfg->providers[i].id);
        csilk_json_add_string(p, "provider_name", cfg->providers[i].name);
        csilk_json_t* ml = csilk_json_array();
        for (int j = 0; j < cfg->providers[i].model_count; j++) {
            csilk_json_add_item(ml, csilk_json_string_new(cfg->providers[i].models[j]));
        }
        csilk_json_add_array(p, "models", ml);
        csilk_json_array_append(out, p);
    }
    respond_ok(c, out);
}

/**
 * @brief 分页获取当前用户的 AI 会话列表
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/ai/sessions
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - page: 页码 (int, 可选, 默认 1)
 *          - page_size: 每页条数 (int, 可选, 默认 20)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"items": [{"id": 1, "title": "资产分析对话", "model": "gpt-4o", ...}], "total": 5}}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void
sessions_list_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    int64_t page = 1, page_size = 20;
    parse_page_params(c, &page, &page_size);
    int64_t       total = 0;
    csilk_json_t* list = ai_session_list(db_get_pool(), user_id, page, page_size, &total);
    if (!list) {
        respond_error(c, 500, "查询失败");
        return;
    }
    respond_page_ok(c, list, total, page, page_size);
}

/**
 * @brief 创建新的 AI 对话会话
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/ai/sessions
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求体 (JSON):
 *          - model: 指定对话模型 (string, 可选)
 *          - provider: 指定供应商标识 (string, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"id": 10}}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void
sessions_create_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    csilk_json_t* body = csilk_bind_json(c);
    const char*   model = csilk_json_get_string(body, "model");
    const char*   provider = csilk_json_get_string(body, "provider");
    int64_t       id = ai_session_insert(db_get_pool(), user_id, "新对话", model, provider);
    csilk_json_free(body);
    if (id <= 0) {
        respond_error(c, 500, "创建失败");
        return;
    }
    csilk_json_t* r = csilk_json_object();
    csilk_json_add_number(r, "id", (double)id);
    respond_ok(c, r);
}

/**
 * @brief 获取单个 AI 会话详情
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/ai/sessions/:id
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 会话 ID (int64)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"id": 1, "title": "...", "model": "...", "provider": "..."}}
 *          - 404 Not Found: 会话不存在或无权访问 (code: 1003)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void
sessions_get_handler(csilk_ctx_t* c)
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
    int64_t       id = atoll(id_str);
    csilk_json_t* r = ai_session_get(db_get_pool(), user_id, id);
    if (!r) {
        respond_not_found(c);
        return;
    }
    respond_ok(c, r);
}

/**
 * @brief 更新指定 AI 会话元数据（标题、模型）
 *
 * @details HTTP 方法: PUT
 *          REST 路径: /api/ai/sessions/:id
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 会话 ID (int64)
 *          请求体 (JSON):
 *          - title: 新会话标题 (string, 可选)
 *          - model: 新选定模型 (string, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *          - 404 Not Found: 会话不存在 (code: 1003)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void
sessions_update_handler(csilk_ctx_t* c)
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
    int64_t       id = atoll(id_str);
    csilk_json_t* body = csilk_bind_json(c);
    const char*   title = csilk_json_get_string(body, "title");
    const char*   model = csilk_json_get_string(body, "model");
    int           ok = ai_session_update(db_get_pool(), user_id, id, title, model);
    csilk_json_free(body);
    if (!ok) {
        respond_not_found(c);
        return;
    }
    respond_ok_null(c);
}

/**
 * @brief 删除指定 AI 会话及其所有消息历史
 *
 * @details HTTP 方法: DELETE
 *          REST 路径: /api/ai/sessions/:id
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 会话 ID (int64)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *          - 404 Not Found: 会话不存在 (code: 1003)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void
sessions_delete_handler(csilk_ctx_t* c)
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
    int64_t id = atoll(id_str);
    int     ok = ai_session_delete(db_get_pool(), user_id, id);
    if (!ok) {
        respond_not_found(c);
        return;
    }
    respond_ok_null(c);
}

/**
 * @brief 获取指定会话下的消息历史记录（分页）
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/ai/sessions/:id/messages
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 会话 ID (int64)
 *          查询参数 (Query Parameters):
 *          - page: 页码 (int, 可选, 默认 1)
 *          - page_size: 每页条数 (int, 可选, 默认 50)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"items": [{"id": 1, "role": "user", "content": "..."}, {"id": 2, "role": "assistant", "content": "..."}], "total": 2}}
 *          - 404 Not Found: 会话不存在 (code: 1003)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void
messages_list_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 session_id");
        return;
    }
    int64_t       session_id = atoll(id_str);
    csilk_json_t* sess = ai_session_get(db_get_pool(), user_id, session_id);
    if (!sess) {
        respond_not_found(c);
        return;
    }
    csilk_json_free(sess);
    int64_t page = 1, page_size = 50;
    parse_page_params(c, &page, &page_size);
    int64_t       total = 0;
    csilk_json_t* list = ai_message_list(db_get_pool(), session_id, page, page_size, &total);
    respond_page_ok(c, list, total, page, page_size);
}

/**
 * @brief 获取 AI 供应商全局配置（API Key 脱敏）
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/settings/ai
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求参数: 无
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"providers": [{"id": "openai", "has_api_key": true, "base_url": "https://api.openai.com/v1", "models": [...]}, ...], "default_provider": "openai", "default_model": "gpt-4o", "context_size": 20, "system_prompt": "..."}}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void
settings_ai_get_handler(csilk_ctx_t* c)
{
    ai_config_t* cfg = ai_get_config();
    if (!cfg) {
        respond_error(c, 500, "AI config not loaded");
        return;
    }

    csilk_json_t* out = csilk_json_object();
    csilk_json_t* prov_arr = csilk_json_array();
    for (int i = 0; i < cfg->provider_count; i++) {
        csilk_json_t* p = csilk_json_object();
        csilk_json_add_string(p, "id", cfg->providers[i].id);
        csilk_json_add_string(p, "name", cfg->providers[i].name);
        csilk_json_add_string(p, "base_url", cfg->providers[i].base_url);
        csilk_json_add_bool(p, "has_api_key", cfg->providers[i].api_key[0] != '\0');
        csilk_json_t* ml = csilk_json_array();
        for (int j = 0; j < cfg->providers[i].model_count; j++) {
            csilk_json_add_item(ml, csilk_json_string_new(cfg->providers[i].models[j]));
        }
        csilk_json_add_array(p, "models", ml);
        csilk_json_array_append(prov_arr, p);
    }
    csilk_json_add_array(out, "providers", prov_arr);
    csilk_json_add_string(out, "default_provider", cfg->default_provider);
    csilk_json_add_string(out, "default_model", cfg->default_model);
    csilk_json_add_number(out, "context_size", (double)cfg->context_size);
    csilk_json_add_string(out, "system_prompt", cfg->system_prompt);
    respond_ok(c, out);
}

/**
 * @brief 更新并持久化 AI 供应商配置（支持 RSA 密文解密与热重载）
 *
 * @details HTTP 方法: PUT
 *          REST 路径: /api/settings/ai
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求体 (JSON):
 *          - providers: 供应商配置数组 (包含 id, name, base_url, api_key_enc/api_key, models)
 *          - default_provider: 默认供应商 ID (string)
 *          - default_model: 默认模型标识 (string)
 *          - context_size: 上下文窗口轮数 (int)
 *          - system_prompt: 全局系统提示词 (string)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *          - 400 Bad Request: 请求体解析错误 (code: 1002)
 *
 *          配置持久化至数据库/配置文件后，会自动调用 ai_shutdown() 与 ai_init() 进行零停机热加载。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void
settings_ai_update_handler(csilk_ctx_t* c)

{
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    ai_config_t      cfg = {0};
    char*            db_json = pool ? ai_settings_load(pool) : NULL;
    if (db_json) {
        ai_config_load_json(db_json, &cfg);
        free(db_json);
    } else {
        const char* cfg_path = getenv("MINEFOLIO_AI_CONFIG") ?: "config/ai.json";
        ai_config_load(cfg_path, &cfg);
    }

    const csilk_json_t* prov_arr = csilk_json_get(body, "providers");
    if (prov_arr && csilk_json_is_array(prov_arr)) {
        int            pc = (int)csilk_json_array_size(prov_arr);
        ai_provider_t* new_provs = (ai_provider_t*)malloc(sizeof(ai_provider_t) * (size_t)pc);
        if (new_provs) {
            memset(new_provs, 0, sizeof(ai_provider_t) * (size_t)pc);
            for (int i = 0; i < pc; i++) {
                const csilk_json_t* p = csilk_json_array_get(prov_arr, i);
                const char*         pid = csilk_json_get_string(p, "id") ?: "";
                strncpy(new_provs[i].id, pid, sizeof(new_provs[i].id) - 1);
                strncpy(new_provs[i].name,
                        csilk_json_get_string(p, "name") ?: "",
                        sizeof(new_provs[i].name) - 1);
                const char* k_enc = csilk_json_get_string(p, "api_key_enc");
                const char* k = csilk_json_get_string(p, "api_key");
                const char* raw_k = (k_enc && k_enc[0]) ? k_enc : k;
                if (raw_k && raw_k[0]) {
                    char   dec_buf[512] = {0};
                    size_t dec_len = sizeof(dec_buf);
                    if (auth_key_decrypt(raw_k, dec_buf, &dec_len) == 0) {
                        size_t _copy_len = dec_len < sizeof(new_provs[i].api_key) - 1
                                               ? dec_len
                                               : sizeof(new_provs[i].api_key) - 1;
                        memcpy(new_provs[i].api_key, dec_buf, _copy_len);
                        new_provs[i].api_key[_copy_len] = '\0';
                    } else {
                        /* Fallback to raw key if not encrypted */
                        size_t _rk_len = strlen(raw_k);
                        size_t _copy_len = _rk_len < sizeof(new_provs[i].api_key) - 1
                                               ? _rk_len
                                               : sizeof(new_provs[i].api_key) - 1;
                        memcpy(new_provs[i].api_key, raw_k, _copy_len);
                        new_provs[i].api_key[_copy_len] = '\0';
                    }
                } else {
                    ai_provider_t* old_p = ai_config_find_provider(&cfg, pid);
                    if (!old_p || old_p->api_key[0] == '\0') {
                        old_p = ai_config_find_provider(ai_get_config(), pid);
                    }
                    if (old_p && old_p->api_key[0]) {
                        size_t _src_len = strlen(old_p->api_key);
                        size_t _copy_len = _src_len < sizeof(new_provs[i].api_key) - 1
                                               ? _src_len
                                               : sizeof(new_provs[i].api_key) - 1;
                        memcpy(new_provs[i].api_key, old_p->api_key, _copy_len);
                        new_provs[i].api_key[_copy_len] = '\0';
                    }
                }
                strncpy(new_provs[i].base_url,
                        csilk_json_get_string(p, "base_url") ?: "",
                        sizeof(new_provs[i].base_url) - 1);
                parse_string_array(
                    csilk_json_get(p, "models"), &new_provs[i].models, &new_provs[i].model_count);
            }
            if (cfg.providers) {
                for (int i = 0; i < cfg.provider_count; i++) {
                    if (cfg.providers[i].models) {
                        for (int j = 0; cfg.providers[i].models[j]; j++) {
                            free(cfg.providers[i].models[j]);
                        }
                        free(cfg.providers[i].models);
                    }
                }
                free(cfg.providers);
            }
            cfg.providers = new_provs;
            cfg.provider_count = pc;
        }
    }

    const char* dp = csilk_json_get_string(body, "default_provider");
    if (dp) {
        strncpy(cfg.default_provider, dp, sizeof(cfg.default_provider) - 1);
    }
    const char* dm = csilk_json_get_string(body, "default_model");
    if (dm) {
        strncpy(cfg.default_model, dm, sizeof(cfg.default_model) - 1);
    }
    const csilk_json_t* csv = csilk_json_get(body, "context_size");
    if (csv) {
        double v = csilk_json_number_value(csv);
        if (v >= 5) {
            cfg.context_size = (int)v;
        }
    }
    const char* sp = csilk_json_get_string(body, "system_prompt");
    if (sp) {
        strncpy(cfg.system_prompt, sp, sizeof(cfg.system_prompt) - 1);
    }

    csilk_json_t* root = csilk_json_object();
    csilk_json_t* prov_arr_out = csilk_json_array();
    for (int i = 0; i < cfg.provider_count; i++) {
        csilk_json_t* p = csilk_json_object();
        csilk_json_add_string(p, "id", cfg.providers[i].id);
        csilk_json_add_string(p, "name", cfg.providers[i].name);
        csilk_json_add_string(p, "api_key", cfg.providers[i].api_key);
        csilk_json_add_string(p, "base_url", cfg.providers[i].base_url);
        csilk_json_t* ml = csilk_json_array();
        for (int j = 0; j < cfg.providers[i].model_count; j++) {
            csilk_json_array_append(ml, csilk_json_string_new(cfg.providers[i].models[j]));
        }
        csilk_json_add_array(p, "models", ml);
        csilk_json_array_append(prov_arr_out, p);
    }
    csilk_json_add_array(root, "providers", prov_arr_out);
    csilk_json_add_string(root, "default_provider", cfg.default_provider);
    csilk_json_add_string(root, "default_model", cfg.default_model);
    csilk_json_add_number(root, "context_size", (double)cfg.context_size);
    csilk_json_add_string(root, "system_prompt", cfg.system_prompt);
    size_t slen = 0;
    char*  json = csilk_json_serialize(root, &slen);
    csilk_json_free(root);

    int db_save_ok = -1;
    if (json && pool) {
        db_save_ok = ai_settings_save(pool, json);
        free(json);
    }

    const char* cfg_path = getenv("MINEFOLIO_AI_CONFIG") ?: "config/ai.json";
    int         file_save_ok = ai_config_save(cfg_path, &cfg);

    ai_config_free(&cfg);
    csilk_json_free(body);

    if (db_save_ok != 0 && file_save_ok != 0) {
        respond_error(c, 500, "保存配置失败");
        return;
    }
    extern void ai_init(csilk_db_pool_t * pool);
    extern void ai_shutdown(void);
    ai_shutdown();
    ai_init(db_get_pool());
    respond_ok_null(c);
}

/**
 * @brief 获取内置财务分析工作流预设定义列表
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/ai/workflows
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求参数: 无
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": [{"id": "spending_analysis", "name": "收支健康诊断", "description": "...", "steps": [...]}, ...]}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void
workflows_list_handler(csilk_ctx_t* c)
{
    csilk_json_t* list = ai_workflow_get_definitions_json();
    respond_ok(c, list);
}

/**
 * @brief SSE 流式执行财务分析工作流
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/ai/workflows/run
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求体 (JSON):
 *          - workflow_id: 工作流标识 (string, 如 "spending_analysis" | "portfolio_rebalance")
 *          - parameters: 自定义分析参数对象 (object, 可选)
 *          返回包格式:
 *          - 200 OK: SSE 流式事件响应 (text/event-stream)
 *            逐步推送步骤执行进度、中间数据提取结果及最终 AI 综合诊断报告。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void
workflows_run_handler(csilk_ctx_t* c)
{
    ai_workflow_run_handler(c);
}

/**
 * @brief 注册 AI 助手模块相关的所有 HTTP 路由
 *
 * @details 挂载模型列表、SSE 流式对话、会话与历史消息、供应商配置管理及智能工作流等端点。
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void
register_ai_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/ai/models",
                      ai_models_handler,
                      NULL,
                      NULL,
                      "List AI models",
                      "Returns available models from configured providers");
    csilk_app_post_ext(
        app, "/api/ai/chat", ai_chat_handler, NULL, NULL, "Chat (SSE)", "Streaming chat endpoint");
    csilk_app_get_ext(
        app, "/api/ai/sessions", sessions_list_handler, NULL, NULL, "List sessions", "");
    csilk_app_post_ext(
        app, "/api/ai/sessions", sessions_create_handler, NULL, NULL, "Create session", "");
    csilk_app_get_ext(
        app, "/api/ai/sessions/:id", sessions_get_handler, NULL, NULL, "Get session", "");
    csilk_app_put_ext(
        app, "/api/ai/sessions/:id", sessions_update_handler, NULL, NULL, "Update session", "");
    csilk_app_delete_ext(
        app, "/api/ai/sessions/:id", sessions_delete_handler, NULL, NULL, "Delete session", "");
    csilk_app_get_ext(app,
                      "/api/ai/sessions/:id/messages",
                      messages_list_handler,
                      NULL,
                      NULL,
                      "List messages",
                      "");
    csilk_app_get_ext(
        app, "/api/settings/ai", settings_ai_get_handler, NULL, NULL, "Get AI config", "");
    csilk_app_put_ext(
        app, "/api/settings/ai", settings_ai_update_handler, NULL, NULL, "Update AI config", "");
    csilk_app_post_ext(app,
                       "/api/settings/ai/test",
                       ai_service_test_connection,
                       NULL,
                       NULL,
                       "Test AI connection",
                       "Tests connectivity to an AI provider");
    csilk_app_post_ext(app,
                       "/api/settings/ai/fetch-models",
                       ai_service_fetch_models,
                       NULL,
                       NULL,
                       "Fetch AI models",
                       "Fetches available models from provider");
    csilk_app_get_ext(app,
                      "/api/ai/workflows",
                      workflows_list_handler,
                      NULL,
                      NULL,
                      "List AI workflows",
                      "Returns preset financial workflows");
    csilk_app_post_ext(app,
                       "/api/ai/workflows/run",
                       workflows_run_handler,
                       NULL,
                       NULL,
                       "Run AI workflow (SSE)",
                       "Streaming financial workflow execution");
}
