#include "application/ai/usecases.h"
#include "domain/ai/entity.h"
#include "domain/ai/rules.h"
#include "repositories/ai_session_repo.h"
#include "repositories/ai_settings_repo.h"
#include "repositories/ai_trace_repo.h"
#include "services/ai_workflow_service.h"
#include "services/ai_service.h"
#include "common/ai_config.h"
#include "config/key_manager.h"
#include "config/secret.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int
ai_usecase_models_list(csilk_json_t** out_data, ai_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!out_data) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    ai_config_t* cfg = ai_get_config();
    if (!cfg) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "AI config not loaded");
        return -1;
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

    *out_data = out;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
ai_usecase_sessions_list(void*                pool,
                         int64_t              user_id,
                         int64_t              page,
                         int64_t              page_size,
                         csilk_json_t**       out_data,
                         int64_t*             out_total,
                         ai_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || user_id <= 0 || !out_data || !out_total) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    int64_t       total = 0;
    csilk_json_t* list = ai_session_list((csilk_db_pool_t*)pool, user_id, page, page_size, &total);
    if (!list) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询失败");
        return -1;
    }

    *out_data = list;
    *out_total = total;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
ai_usecase_sessions_create(void*                          pool,
                           const ai_create_session_cmd_t* cmd,
                           int64_t*                       out_id,
                           ai_usecase_result_t*           out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !cmd || cmd->user_id <= 0 || !out_id) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    char title_buf[128];
    char err[256];
    if (mf_ai_rule_validate_session_title(
            cmd->title, title_buf, sizeof(title_buf), err, sizeof(err)) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "%s", err);
        return -1;
    }

    int64_t id = ai_session_insert(
        (csilk_db_pool_t*)pool, cmd->user_id, title_buf, cmd->model, cmd->provider);
    if (id <= 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "创建失败");
        return -1;
    }

    *out_id = id;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
ai_usecase_sessions_get(
    void* pool, int64_t user_id, int64_t id, csilk_json_t** out_data, ai_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || user_id <= 0 || id <= 0 || !out_data) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    csilk_json_t* r = ai_session_get((csilk_db_pool_t*)pool, user_id, id);
    if (!r) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "会话不存在");
        return -1;
    }

    *out_data = r;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
ai_usecase_sessions_update(void*                          pool,
                           const ai_update_session_cmd_t* cmd,
                           ai_usecase_result_t*           out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !cmd || cmd->user_id <= 0 || cmd->id <= 0) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    int ok =
        ai_session_update((csilk_db_pool_t*)pool, cmd->user_id, cmd->id, cmd->title, cmd->model);
    if (!ok) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "会话不存在");
        return -1;
    }

    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
ai_usecase_sessions_delete(void* pool, int64_t user_id, int64_t id, ai_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || user_id <= 0 || id <= 0) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    int ok = ai_session_delete((csilk_db_pool_t*)pool, user_id, id);
    if (!ok) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "会话不存在");
        return -1;
    }

    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
ai_usecase_messages_list(void*                pool,
                         int64_t              user_id,
                         int64_t              session_id,
                         int64_t              page,
                         int64_t              page_size,
                         csilk_json_t**       out_data,
                         int64_t*             out_total,
                         ai_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || user_id <= 0 || session_id <= 0 || !out_data || !out_total) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    csilk_json_t* sess = ai_session_get((csilk_db_pool_t*)pool, user_id, session_id);
    if (!sess) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "会话不存在");
        return -1;
    }
    csilk_json_free(sess);

    int64_t       total = 0;
    csilk_json_t* list =
        ai_message_list((csilk_db_pool_t*)pool, session_id, page, page_size, &total);

    *out_data = list;
    *out_total = total;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
ai_usecase_settings_get(csilk_json_t** out_data, ai_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!out_data) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    ai_config_t* cfg = ai_get_config();
    if (!cfg) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "AI config not loaded");
        return -1;
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

    *out_data = out;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
ai_usecase_settings_update(void* pool, const csilk_json_t* body, ai_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!body) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "请求体必须为 JSON");
        return -1;
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    ai_config_t      cfg = {0};
    char*            db_json = db_pool ? ai_settings_load(db_pool) : NULL;
    if (db_json) {
        ai_config_load_json(db_json, &cfg);
        free(db_json);
    } else {
        const char* cfg_path = config_env_get("AI_CONFIG", NULL, 0, "config/ai.json");
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
        cfg.context_size = mf_ai_rule_clamp_context_size((int)v);
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
    if (json && db_pool) {
        db_save_ok = ai_settings_save(db_pool, json);
        free(json);
    }

    const char* cfg_path = config_env_get("AI_CONFIG", NULL, 0, "config/ai.json");
    int         file_save_ok = ai_config_save(cfg_path, &cfg);

    ai_config_free(&cfg);

    if (db_save_ok != 0 && file_save_ok != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "保存配置失败");
        return -1;
    }

    extern void ai_init(csilk_db_pool_t * pool);
    extern void ai_shutdown(void);
    ai_shutdown();
    ai_init(db_pool);

    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
ai_usecase_workflows_list(csilk_json_t** out_data, ai_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!out_data) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    csilk_json_t* list = ai_workflow_get_definitions_json();
    *out_data = list;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
ai_usecase_trace_list(void*                pool,
                      int64_t              user_id,
                      int64_t              page,
                      int64_t              page_size,
                      const char*          provider,
                      const char*          model,
                      csilk_json_t**       out_data,
                      int64_t*             out_total,
                      ai_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || user_id <= 0 || !out_data || !out_total) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    int64_t       total = 0;
    csilk_json_t* list =
        ai_trace_list((csilk_db_pool_t*)pool, user_id, page, page_size, provider, model, &total);
    if (!list) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询 Trace 失败");
        return -1;
    }

    *out_data = list;
    *out_total = total;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
ai_usecase_trace_stats(void*                pool,
                       int64_t              user_id,
                       csilk_json_t**       out_data,
                       ai_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || user_id <= 0 || !out_data) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    csilk_json_t* r = ai_trace_stats((csilk_db_pool_t*)pool, user_id);
    if (!r) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询统计失败");
        return -1;
    }

    csilk_json_t* stats = csilk_json_array_get(r, 0);
    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "total_traces", db_get_int(stats, "total_traces"));
    csilk_json_add_number(out, "total_tokens", db_get_int(stats, "total_tokens"));
    csilk_json_add_number(out, "avg_latency_ms", db_get_num(stats, "avg_latency_ms"));
    csilk_json_add_number(out, "avg_first_token_ms", db_get_num(stats, "avg_first_token_ms"));
    csilk_json_free(r);

    *out_data = out;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
ai_usecase_trace_get(
    void* pool, int64_t user_id, int64_t id, csilk_json_t** out_data, ai_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || user_id <= 0 || id <= 0 || !out_data) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    csilk_json_t* r = ai_trace_get((csilk_db_pool_t*)pool, user_id, id);
    if (!r || csilk_json_array_size(r) == 0) {
        if (r) {
            csilk_json_free(r);
        }
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "Trace 不存在");
        return -1;
    }

    csilk_json_t* row = csilk_json_array_get(r, 0);
    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "id", db_get_num(row, "id"));
    csilk_json_add_number(out, "user_id", db_get_num(row, "user_id"));
    csilk_json_add_number(out, "session_id", db_get_num(row, "session_id"));
    csilk_json_add_string(out, "provider", csilk_json_get_string(row, "provider"));
    csilk_json_add_string(out, "model", csilk_json_get_string(row, "model"));
    csilk_json_add_string(out, "input_messages", csilk_json_get_string(row, "input_messages"));
    csilk_json_add_string(out, "output_content", csilk_json_get_string(row, "output_content"));
    csilk_json_add_string(out, "system_prompt", csilk_json_get_string(row, "system_prompt"));
    csilk_json_add_number(out, "prompt_tokens", db_get_num(row, "prompt_tokens"));
    csilk_json_add_number(out, "completion_tokens", db_get_num(row, "completion_tokens"));
    csilk_json_add_number(out, "total_tokens", db_get_num(row, "total_tokens"));
    csilk_json_add_number(out, "latency_ms", db_get_num(row, "latency_ms"));
    csilk_json_add_number(out, "first_token_ms", db_get_num(row, "first_token_ms"));
    csilk_json_add_number(out, "tokens_per_sec", db_get_num(row, "tokens_per_sec"));
    csilk_json_add_number(out, "cost_usd", db_get_num(row, "cost_usd"));
    csilk_json_add_number(out, "temperature", db_get_num(row, "temperature"));
    csilk_json_add_number(out, "max_tokens", db_get_num(row, "max_tokens"));
    csilk_json_add_number(out, "top_p", db_get_num(row, "top_p"));
    csilk_json_add_string(out, "status", csilk_json_get_string(row, "status"));
    csilk_json_add_string(out, "error_message", csilk_json_get_string(row, "error_message"));
    csilk_json_add_string(out, "metadata", csilk_json_get_string(row, "metadata"));
    csilk_json_add_string(out, "created_at", csilk_json_get_string(row, "created_at"));
    csilk_json_free(r);

    *out_data = out;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}
