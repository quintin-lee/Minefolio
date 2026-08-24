#include "services/ai_service.h"
#include "repositories/ai_session_repo.h"
#include "repositories/ai_settings_repo.h"
#include "common/ai_config.h"
#include "common/ai_trace.h"
#include "common/db.h"
#include "common/response.h"
#include "common/ctx.h"
#include "csilk/drivers/ai.h"
#include "csilk/protocols/sse.h"
#include "config/key_manager.h"
#include <curl/curl.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ai_config_t g_config = {0};
static csilk_ai_t* g_ai = NULL;
static const char* get_driver_name(const char* prov_id) {
    if (prov_id && strcmp(prov_id, "ollama") == 0) return "ollama";
    return "openai";
}

void ai_init(csilk_db_pool_t* pool) {
    char* json = pool ? ai_settings_load(pool) : NULL;
    if (json) {
        char path[] = "/tmp/ai_config_db.json";
        FILE* f = fopen(path, "w");
        if (f) { fwrite(json, 1, strlen(json), f); fclose(f); }
        free(json);
        if (ai_config_load(path, &g_config) == 0) {
            ai_provider_t* prov = ai_config_default_provider(&g_config);
            if (prov) {
                const char* key = (prov->api_key[0] != '\0') ? prov->api_key : "dummy";
                const char* dname = get_driver_name(prov->id);
                g_ai = csilk_ai_new(dname, key, prov->base_url[0] ? prov->base_url : NULL);
                if (g_ai) printf("AI initialized (DB): provider=%s model=%s\n", prov->id, g_config.default_model);
                else fprintf(stderr, "WARN: failed to create AI instance for provider '%s'\n", prov->id);
                return;
            }
        }
    }
    const char* cfg_path = getenv("MINEFOLIO_AI_CONFIG") ?: "config/ai.json";
    if (ai_config_load(cfg_path, &g_config) != 0) {
        fprintf(stderr, "WARN: could not load ai_config from %s\n", cfg_path);
        return;
    }
    ai_provider_t* prov = ai_config_default_provider(&g_config);
    if (!prov) {
        fprintf(stderr, "WARN: default provider '%s' not found in config\n", g_config.default_provider);
        return;
    }
    const char* key = (prov->api_key[0] != '\0') ? prov->api_key : "dummy";
    const char* dname = get_driver_name(prov->id);
    g_ai = csilk_ai_new(dname, key, prov->base_url[0] ? prov->base_url : NULL);
    if (g_ai) printf("AI initialized (file): provider=%s model=%s\n", prov->id, g_config.default_model);
    else fprintf(stderr, "WARN: failed to create AI instance for provider '%s'\n", prov->id);
}

void ai_shutdown(void) {
    if (g_ai) { csilk_ai_free(g_ai); g_ai = NULL; }
    ai_config_free(&g_config);
}

ai_config_t* ai_get_config(void) { return &g_config; }

typedef struct {
    csilk_ctx_t* ctx;
    char* accumulated;
    size_t cap;
    size_t len;
    ai_trace_t* trace;
} stream_context_t;

static void on_chunk(const char* delta, void* data) {
    stream_context_t* sc = (stream_context_t*)data;
    if (!delta || !sc || !sc->ctx) return;

    if (sc->trace) {
        ai_trace_record_first_token(sc->trace);
        ai_trace_append_output(sc->trace, delta);
    }

    size_t dlen = strlen(delta);
    if (sc->len + dlen + 1 > sc->cap) {
        size_t ncap = (sc->len + dlen + 1) * 2;
        if (ncap < 1024) ncap = 1024;
        char* nbuf = (char*)realloc(sc->accumulated, ncap);
        if (nbuf) {
            sc->accumulated = nbuf;
            sc->cap = ncap;
        }
    }
    if (sc->accumulated && sc->len + dlen < sc->cap) {
        memcpy(sc->accumulated + sc->len, delta, dlen);
        sc->len += dlen;
        sc->accumulated[sc->len] = '\0';
    }

    csilk_json_t* msg = csilk_json_object();
    csilk_json_add_string(msg, "content", delta);
    size_t slen = 0;
    char* s = csilk_json_serialize(msg, &slen);
    csilk_sse_send(sc->ctx, "delta", s ? s : "");
    free(s);
    csilk_json_free(msg);
}

static void send_done(csilk_ctx_t* c) {
    csilk_json_t* d = csilk_json_object();
    csilk_json_add_string(d, "finish_reason", "stop");
    size_t slen = 0;
    char* s = csilk_json_serialize(d, &slen);
    csilk_sse_send(c, "done", s ? s : "");
    free(s);
    csilk_json_free(d);
}

static void send_error(csilk_ctx_t* c, const char* err) {
    csilk_json_t* d = csilk_json_object();
    csilk_json_add_string(d, "message", err);
    size_t slen = 0;
    char* s = csilk_json_serialize(d, &slen);
    csilk_sse_send(c, "error", s ? s : "");
    free(s);
    csilk_json_free(d);
}

void ai_chat_handler(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    int64_t session_id = db_get_int(body, "session_id");
    const char* content = csilk_json_get_string(body, "content");
    const char* model_override = csilk_json_get_string(body, "model");
    const char* provider_override = csilk_json_get_string(body, "provider");
    int regenerate = csilk_json_get_bool(body, "regenerate");

    if (!content || content[0] == '\0') {
        csilk_json_free(body);
        respond_bad_request(c, "content 不能为空");
        return;
    }

    if (!g_ai) {
        csilk_json_free(body);
        respond_error(c, 500, "AI 服务未配置，请先在设置中配置供应商");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();

    ai_provider_t* prov = NULL;
    char provider_buf[64] = {0};
    char model_buf[128] = {0};
    if (provider_override && provider_override[0]) {
        strncpy(provider_buf, provider_override, sizeof(provider_buf) - 1);
        prov = ai_config_find_provider(&g_config, provider_buf);
    }
    if (!prov) prov = ai_config_default_provider(&g_config);
    if (!prov) {
        csilk_json_free(body);
        respond_error(c, 500, "未找到可用 AI 供应商");
        return;
    }
    strncpy(model_buf, model_override ?: (prov->models[0] ?: "default"), sizeof(model_buf) - 1);

    /* session: create if new */
    int64_t sid = session_id;
    if (sid <= 0) {
        char title[256];
        strncpy(title, content, sizeof(title) - 1); title[sizeof(title)-1] = '\0';
        if (strlen(title) > 30) {
            title[30] = '\0';
            size_t tlen = strlen(title);
            while (tlen > 0 && (unsigned char)title[tlen-1] > 0x7F) { title[--tlen] = '\0'; }
        }
        sid = ai_session_insert(pool, user_id, title, model_buf, prov->id);
        if (sid <= 0) {
            csilk_json_free(body);
            respond_error(c, 500, "创建会话失败");
            return;
        }
    } else {
        csilk_json_t* sess = ai_session_get(pool, user_id, sid);
        if (!sess) { csilk_json_free(body); respond_not_found(c); return; }
        csilk_json_free(sess);
        if (regenerate) {
            ai_message_delete_last_assistant(pool, sid);
        }
    }

    /* load history */
    int ctx_size = g_config.context_size > 0 ? g_config.context_size : 20;
    csilk_json_t* history = ai_message_recent(pool, sid, ctx_size);
    int hsz = history ? (int)csilk_json_array_size(history) : 0;

    /* build messages: [system, ...history, user] */
    int mc = 1 + hsz + 1;
    csilk_ai_message_t* msgs = (csilk_ai_message_t*)malloc(sizeof(csilk_ai_message_t) * (size_t)mc);
    if (!msgs) { csilk_json_free(history); csilk_json_free(body); respond_error(c, 500, "内存不足"); return; }
    int idx = 0;
    msgs[idx++] = (csilk_ai_message_t){.role="system", .content=g_config.system_prompt};
    if (history) {
        for (int i = 0; i < hsz; i++) {
            csilk_json_t* m = csilk_json_array_get(history, i);
            msgs[idx++] = (csilk_ai_message_t){
                .role = csilk_json_get_string(m, "role"),
                .content = csilk_json_get_string(m, "content"),
            };
        }
    }
    msgs[idx++] = (csilk_ai_message_t){.role="user", .content=content};

    stream_context_t sctx = {
        .ctx = c,
        .accumulated = NULL,
        .cap = 0,
        .len = 0,
        .trace = NULL,
    };

    ai_trace_t trace;
    ai_trace_init(&trace, user_id, sid);
    ai_trace_set_provider(&trace, prov->id, model_buf);
    ai_trace_set_system_prompt(&trace, g_config.system_prompt);
    sctx.trace = &trace;

    csilk_json_t* input_arr = csilk_json_array();
    csilk_json_t* sys_msg = csilk_json_object();
    csilk_json_add_string(sys_msg, "role", "system");
    csilk_json_add_string(sys_msg, "content", g_config.system_prompt ?: "");
    csilk_json_array_append(input_arr, sys_msg);
    for (int i = 0; i < hsz; i++) {
        csilk_json_t* m = csilk_json_array_get(history, i);
        csilk_json_t* im = csilk_json_object();
        csilk_json_add_string(im, "role", csilk_json_get_string(m, "role") ?: "");
        csilk_json_add_string(im, "content", csilk_json_get_string(m, "content") ?: "");
        csilk_json_array_append(input_arr, im);
    }
    csilk_json_t* usr_msg = csilk_json_object();
    csilk_json_add_string(usr_msg, "role", "user");
    csilk_json_add_string(usr_msg, "content", content);
    csilk_json_array_append(input_arr, usr_msg);
    ai_trace_serialize_messages(&trace, input_arr);
    csilk_json_free(input_arr);

    /* SSE init */
    csilk_sse_init(c);

    /* persist user message */
    ai_message_insert(pool, sid, "user", content, model_buf);

    csilk_ai_t* ai_inst = g_ai;
    int need_free_ai = 0;
    if (prov && (strcmp(prov->id, g_config.default_provider) != 0 || !ai_inst)) {
        const char* dname = get_driver_name(prov->id);
        const char* key = (prov->api_key[0] != '\0') ? prov->api_key : "dummy";
        csilk_ai_t* custom_ai = csilk_ai_new(dname, key, prov->base_url[0] ? prov->base_url : NULL);
        if (custom_ai) {
            ai_inst = custom_ai;
            need_free_ai = 1;
        }
    }

    /* streaming chat */
    csilk_ai_chat_request_t req = {
        .model = model_buf,
        .messages = msgs,
        .message_count = mc,
        .stream = 1,
        .on_chunk = on_chunk,
        .user_data = &sctx,
    };
    csilk_ai_chat_response_t ai_res = {0};
    int rc = csilk_ai_chat(ai_inst, &req, &ai_res);
    if (rc != 0) {
        ai_trace_finish(&trace, "error", "AI request failed");
        ai_trace_save(db_get_pool(), &trace);
        ai_trace_free(&trace);
        send_error(c, "AI request failed");
    } else {
        if (sctx.accumulated && sctx.len > 0) {
            ai_message_insert(pool, sid, "assistant", sctx.accumulated, model_buf);
        }
        ai_trace_finish(&trace, "ok", NULL);
        ai_trace_save(db_get_pool(), &trace);
        ai_trace_free(&trace);
        send_done(c);
    }
    csilk_ai_chat_response_free(&ai_res);

    if (need_free_ai && ai_inst) csilk_ai_free(ai_inst);
    if (sctx.accumulated) free(sctx.accumulated);
    csilk_sse_close(c);
    free(msgs);
    csilk_json_free(history);
    csilk_json_free(body);
}
void ai_service_test_connection(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* id = csilk_json_get_string(body, "id") ?: "openai";
    const char* base_url = csilk_json_get_string(body, "base_url");
    const char* k_enc = csilk_json_get_string(body, "api_key_enc");
    const char* k = csilk_json_get_string(body, "api_key");
    const char* model = csilk_json_get_string(body, "model");

    char key_buf[512] = {0};
    const char* raw_k = (k_enc && k_enc[0]) ? k_enc : k;
    if (raw_k && raw_k[0]) {
        size_t dec_len = sizeof(key_buf);
        if (auth_key_decrypt(raw_k, key_buf, &dec_len) != 0) {
            strncpy(key_buf, raw_k, sizeof(key_buf) - 1);
        }
    } else {
        ai_provider_t* old_p = ai_config_find_provider(&g_config, id);
        if (old_p && old_p->api_key[0]) {
            strncpy(key_buf, old_p->api_key, sizeof(key_buf) - 1);
        } else {
            strcpy(key_buf, "dummy");
        }
    }

    const char* dname = get_driver_name(id);
    csilk_ai_t* test_ai = csilk_ai_new(dname, key_buf, (base_url && base_url[0]) ? base_url : NULL);
    if (!test_ai) {
        csilk_json_free(body);
        csilk_json_t* resp = csilk_json_object();
        csilk_json_add_bool(resp, "success", false);
        csilk_json_add_number(resp, "latency_ms", 0);
        csilk_json_add_string(resp, "message", "创建 AI 驱动失败");
        respond_ok(c, resp);
        return;
    }

    struct timespec t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t1);

    csilk_ai_message_t msg = {.role = "user", .content = "ping"};
    csilk_ai_chat_request_t req = {
        .model = (model && model[0]) ? model : "model",
        .messages = &msg,
        .message_count = 1,
        .stream = 0,
        .timeout_ms = 10000,
    };
    csilk_ai_chat_response_t res = {0};
    int rc = csilk_ai_chat(test_ai, &req, &res);
    clock_gettime(CLOCK_MONOTONIC, &t2);

    long latency_ms = (t2.tv_sec - t1.tv_sec) * 1000 + (t2.tv_nsec - t1.tv_nsec) / 1000000;
    if (latency_ms < 0) latency_ms = 0;

    csilk_json_t* resp = csilk_json_object();
    if (rc == 0) {
        csilk_json_add_bool(resp, "success", true);
        csilk_json_add_number(resp, "latency_ms", (double)latency_ms);
        csilk_json_add_string(resp, "message", "连接成功");
    } else {
        csilk_json_add_bool(resp, "success", false);
        csilk_json_add_number(resp, "latency_ms", (double)latency_ms);
        csilk_json_add_string(resp, "message", (res.error_message && res.error_message[0]) ? res.error_message : "连接超时或失败");
    }

    csilk_ai_chat_response_free(&res);
    csilk_ai_free(test_ai);
    csilk_json_free(body);
    respond_ok(c, resp);
}

typedef struct {
    char* data;
    size_t size;
} memory_buf_t;

static size_t curl_write_memory_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    memory_buf_t* mem = (memory_buf_t*)userp;
    char* ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) return 0;
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    return realsize;
}

void ai_service_fetch_models(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* id = csilk_json_get_string(body, "id") ?: "openai";
    const char* base_url = csilk_json_get_string(body, "base_url");
    const char* k_enc = csilk_json_get_string(body, "api_key_enc");
    const char* k = csilk_json_get_string(body, "api_key");

    char key_buf[512] = {0};
    const char* raw_k = (k_enc && k_enc[0]) ? k_enc : k;
    if (raw_k && raw_k[0]) {
        size_t dec_len = sizeof(key_buf);
        if (auth_key_decrypt(raw_k, key_buf, &dec_len) != 0) {
            strncpy(key_buf, raw_k, sizeof(key_buf) - 1);
        }
    } else {
        ai_provider_t* old_p = ai_config_find_provider(&g_config, id);
        if (old_p && old_p->api_key[0]) {
            strncpy(key_buf, old_p->api_key, sizeof(key_buf) - 1);
        } else {
            strcpy(key_buf, "dummy");
        }
    }

    char url[512];
    if (base_url && base_url[0]) {
        size_t blen = strlen(base_url);
        if (base_url[blen - 1] == '/')
            snprintf(url, sizeof(url), "%smodels", base_url);
        else
            snprintf(url, sizeof(url), "%s/models", base_url);
    } else {
        snprintf(url, sizeof(url), "https://api.openai.com/v1/models");
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        csilk_json_free(body);
        respond_error(c, 500, "CURL 初始化失败");
        return;
    }

    memory_buf_t chunk = { .data = malloc(1), .size = 0 };
    if (chunk.data) chunk.data[0] = '\0';

    struct curl_slist* headers = NULL;
    char auth_hdr[512];
    snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", key_buf);
    headers = curl_slist_append(headers, auth_hdr);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_memory_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    csilk_json_t* out = csilk_json_object();
    csilk_json_t* model_arr = csilk_json_array();

    if (res == CURLE_OK && http_code == 200 && chunk.data) {
        csilk_json_t* parsed = csilk_json_parse(chunk.data);
        if (parsed) {
            const csilk_json_t* data_arr = csilk_json_get(parsed, "data");
            if (data_arr && csilk_json_is_array(data_arr)) {
                size_t dsz = csilk_json_array_size(data_arr);
                for (size_t i = 0; i < dsz; i++) {
                    const csilk_json_t* item = csilk_json_array_get(data_arr, i);
                    const char* mid = csilk_json_get_string(item, "id");
                    if (mid && mid[0]) {
                        csilk_json_array_append(model_arr, csilk_json_string_new(mid));
                    }
                }
            }
            const csilk_json_t* models_arr = csilk_json_get(parsed, "models");
            if (models_arr && csilk_json_is_array(models_arr)) {
                size_t msz = csilk_json_array_size(models_arr);
                for (size_t i = 0; i < msz; i++) {
                    const csilk_json_t* item = csilk_json_array_get(models_arr, i);
                    const char* mid = csilk_json_get_string(item, "name");
                    if (!mid) mid = csilk_json_get_string(item, "id");
                    if (!mid) mid = csilk_json_string_value(item);
                    if (mid && mid[0]) {
                        csilk_json_array_append(model_arr, csilk_json_string_new(mid));
                    }
                }
            }
            csilk_json_free(parsed);
        }
    }

    free(chunk.data);
    csilk_json_free(body);
    csilk_json_add_array(out, "models", model_arr);
    respond_ok(c, out);
}
