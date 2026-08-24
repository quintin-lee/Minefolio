#include "controllers/ai_controller.h"
#include "services/ai_service.h"
#include "repositories/ai_session_repo.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/ai_config.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void parse_string_array(const csilk_json_t *arr, char ***out_ptrs, int *out_count) {
    if (!arr || !csilk_json_is_array(arr)) { *out_ptrs = NULL; *out_count = 0; return; }
    int n = csilk_json_array_size(arr);
    *out_ptrs = (char**)malloc(sizeof(char*) * (size_t)n + 1);
    if (!*out_ptrs) { *out_count = 0; return; }
    *out_count = n;
    for (size_t i = 0; i < (size_t)n; i++) {
        const char *s = csilk_json_string_value(csilk_json_array_get(arr, i));
        (*out_ptrs)[i] = s ? strdup(s) : strdup("");
    }
    (*out_ptrs)[n] = NULL;
}

void ai_models_handler(csilk_ctx_t* c) {
    ai_config_t* cfg = ai_get_config();
    if (!cfg) { respond_error(c, 500, "AI config not loaded"); return; }

    csilk_json_t* out = csilk_json_array();
    for (int i = 0; i < cfg->provider_count; i++) {
        csilk_json_t* p = csilk_json_object();
        csilk_json_add_string(p, "provider_id", cfg->providers[i].id);
        csilk_json_add_string(p, "provider_name", cfg->providers[i].name);
        csilk_json_t* ml = csilk_json_array();
        for (int j = 0; j < cfg->providers[i].model_count; j++)
            csilk_json_add_item(ml, csilk_json_string_new(cfg->providers[i].models[j]));
        csilk_json_add_array(p, "models", ml);
        csilk_json_array_append(out, p);
    }
    respond_ok(c, out);
}

void sessions_list_handler(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;
    int64_t page = 1, page_size = 20;
    parse_page_params(c, &page, &page_size);
    int64_t total = 0;
    csilk_json_t* list = ai_session_list(db_get_pool(), user_id, page, page_size, &total);
    if (!list) { respond_error(c, 500, "查询失败"); return; }
    respond_page_ok(c, list, total, page, page_size);
}

void sessions_create_handler(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;
    csilk_json_t* body = csilk_bind_json(c);
    const char* model = csilk_json_get_string(body, "model");
    const char* provider = csilk_json_get_string(body, "provider");
    int64_t id = ai_session_insert(db_get_pool(), user_id, "新对话", model, provider);
    csilk_json_free(body);
    if (id <= 0) { respond_error(c, 500, "创建失败"); return; }
    csilk_json_t* r = csilk_json_object();
    csilk_json_add_number(r, "id", (double)id);
    respond_ok(c, r);
}

void sessions_get_handler(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;
    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }
    int64_t id = atoll(id_str);
    csilk_json_t* r = ai_session_get(db_get_pool(), user_id, id);
    if (!r) { respond_not_found(c); return; }
    respond_ok(c, r);
}

void sessions_update_handler(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;
    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }
    int64_t id = atoll(id_str);
    csilk_json_t* body = csilk_bind_json(c);
    const char* title = csilk_json_get_string(body, "title");
    const char* model = csilk_json_get_string(body, "model");
    int ok = ai_session_update(db_get_pool(), user_id, id, title, model);
    csilk_json_free(body);
    if (!ok) { respond_not_found(c); return; }
    respond_ok_null(c);
}

void sessions_delete_handler(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;
    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }
    int64_t id = atoll(id_str);
    int ok = ai_session_delete(db_get_pool(), user_id, id);
    if (!ok) { respond_not_found(c); return; }
    respond_ok_null(c);
}

void messages_list_handler(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;
    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 session_id"); return; }
    int64_t session_id = atoll(id_str);
    int64_t page = 1, page_size = 50;
    parse_page_params(c, &page, &page_size);
    int64_t total = 0;
    csilk_json_t* list = ai_message_list(db_get_pool(), session_id, page, page_size, &total);
    respond_page_ok(c, list, total, page, page_size);
}

void settings_ai_get_handler(csilk_ctx_t* c) {
    ai_config_t* cfg = ai_get_config();
    if (!cfg) { respond_error(c, 500, "AI config not loaded"); return; }

    csilk_json_t* out = csilk_json_object();
    csilk_json_t* prov_arr = csilk_json_array();
    for (int i = 0; i < cfg->provider_count; i++) {
        csilk_json_t* p = csilk_json_object();
        csilk_json_add_string(p, "id", cfg->providers[i].id);
        csilk_json_add_string(p, "name", cfg->providers[i].name);
        csilk_json_add_string(p, "base_url", cfg->providers[i].base_url);
        csilk_json_t* ml = csilk_json_array();
        for (int j = 0; j < cfg->providers[i].model_count; j++)
            csilk_json_add_item(ml, csilk_json_string_new(cfg->providers[i].models[j]));
        csilk_json_add_array(p, "models", ml);
        csilk_json_array_append(prov_arr, p);
    }
    csilk_json_add_object(out, "providers", prov_arr);
    csilk_json_add_string(out, "default_provider", cfg->default_provider);
    csilk_json_add_string(out, "default_model", cfg->default_model);
    csilk_json_add_number(out, "context_size", (double)cfg->context_size);
    csilk_json_add_string(out, "system_prompt", cfg->system_prompt);
    respond_ok(c, out);
}

void settings_ai_update_handler(csilk_ctx_t* c) {
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    ai_config_t cfg = {0};
    const char* cfg_path = getenv("MINEFOLIO_AI_CONFIG") ?: "config/ai.json";
    ai_config_load(cfg_path, &cfg);

    const csilk_json_t* prov_arr = csilk_json_get(body, "providers");
    if (prov_arr && csilk_json_is_array(prov_arr)) {
        int pc = (int)csilk_json_array_size(prov_arr);
        ai_provider_t* new_provs = (ai_provider_t*)malloc(sizeof(ai_provider_t) * (size_t)pc);
        if (new_provs) {
            memset(new_provs, 0, sizeof(ai_provider_t) * (size_t)pc);
            for (int i = 0; i < pc; i++) {
                const csilk_json_t* p = csilk_json_array_get(prov_arr, i);
                const char* pid = csilk_json_get_string(p, "id") ?: "";
                strncpy(new_provs[i].id, pid, sizeof(new_provs[i].id) - 1);
                strncpy(new_provs[i].name, csilk_json_get_string(p, "name") ?: "", sizeof(new_provs[i].name) - 1);
                const char* k = csilk_json_get_string(p, "api_key");
                if (k && k[0]) {
                    strncpy(new_provs[i].api_key, k, sizeof(new_provs[i].api_key) - 1);
                } else {
                    ai_provider_t* old_p = ai_config_find_provider(&cfg, pid);
                    if (old_p && old_p->api_key[0]) {
                        strncpy(new_provs[i].api_key, old_p->api_key, sizeof(new_provs[i].api_key) - 1);
                    }
                }
                strncpy(new_provs[i].base_url, csilk_json_get_string(p, "base_url") ?: "", sizeof(new_provs[i].base_url) - 1);
                parse_string_array(csilk_json_get(p, "models"), &new_provs[i].models, &new_provs[i].model_count);
            }
            if (cfg.providers) {
                for (int i = 0; i < cfg.provider_count; i++) {
                    if (cfg.providers[i].models) {
                        for (int j = 0; cfg.providers[i].models[j]; j++) free(cfg.providers[i].models[j]);
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
    if (dp) strncpy(cfg.default_provider, dp, sizeof(cfg.default_provider) - 1);
    const char* dm = csilk_json_get_string(body, "default_model");
    if (dm) strncpy(cfg.default_model, dm, sizeof(cfg.default_model) - 1);
    const csilk_json_t* csv = csilk_json_get(body, "context_size");
    if (csv) { double v = csilk_json_number_value(csv); if (v >= 5) cfg.context_size = (int)v; }
    const char* sp = csilk_json_get_string(body, "system_prompt");
    if (sp) strncpy(cfg.system_prompt, sp, sizeof(cfg.system_prompt) - 1);

    int ok = ai_config_save(cfg_path, &cfg);
    ai_config_free(&cfg);
    csilk_json_free(body);

    if (ok != 0) { respond_error(c, 500, "保存配置失败"); return; }
    extern void ai_init(csilk_db_pool_t* pool);
    extern void ai_shutdown(void);
    ai_shutdown();
    ai_init(db_get_pool());
    respond_ok_null(c);
}

void register_ai_routes(csilk_app_t* app) {
    csilk_app_get_ext(app, "/api/ai/models", ai_models_handler, NULL, NULL, "List AI models", "Returns available models from configured providers");
    csilk_app_post_ext(app, "/api/ai/chat", ai_chat_handler, NULL, NULL, "Chat (SSE)", "Streaming chat endpoint");
    csilk_app_get_ext(app, "/api/ai/sessions", sessions_list_handler, NULL, NULL, "List sessions", "");
    csilk_app_post_ext(app, "/api/ai/sessions", sessions_create_handler, NULL, NULL, "Create session", "");
    csilk_app_get_ext(app, "/api/ai/sessions/:id", sessions_get_handler, NULL, NULL, "Get session", "");
    csilk_app_put_ext(app, "/api/ai/sessions/:id", sessions_update_handler, NULL, NULL, "Update session", "");
    csilk_app_delete_ext(app, "/api/ai/sessions/:id", sessions_delete_handler, NULL, NULL, "Delete session", "");
    csilk_app_get_ext(app, "/api/ai/sessions/:id/messages", messages_list_handler, NULL, NULL, "List messages", "");
    csilk_app_get_ext(app, "/api/settings/ai", settings_ai_get_handler, NULL, NULL, "Get AI config", "");
    csilk_app_put_ext(app, "/api/settings/ai", settings_ai_update_handler, NULL, NULL, "Update AI config", "");
}
