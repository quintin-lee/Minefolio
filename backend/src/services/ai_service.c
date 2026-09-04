#include "services/ai_service.h"
#include "services/ai_tools.h"
#include "services/ai/runtime/runtime.h"
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
#include "config/secret.h"
#include <curl/curl.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ai_config_t g_config = {0};
static csilk_ai_t* g_ai = NULL;
static const char*
get_driver_name(const char* prov_id)
{
    if (prov_id && strcmp(prov_id, "ollama") == 0) {
        return "ollama";
    }
    return "openai";
}

static void
utf8_truncate(char* str, size_t max_chars, size_t max_bytes)
{
    if (!str || !*str) {
        return;
    }
    size_t char_count = 0;
    size_t byte_idx = 0;
    size_t valid_byte_len = 0;

    while (str[byte_idx] != '\0') {
        unsigned char c = (unsigned char)str[byte_idx];
        size_t        char_len = 1;
        if ((c & 0x80) == 0) {
            char_len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            char_len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            char_len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            char_len = 4;
        } else {
            char_len = 1;
        }

        /* Check if full character sequence exists in buffer */
        int complete = 1;
        for (size_t i = 1; i < char_len; i++) {
            if (str[byte_idx + i] == '\0' || ((unsigned char)str[byte_idx + i] & 0xC0) != 0x80) {
                complete = 0;
                break;
            }
        }
        if (!complete) {
            break;
        }

        if (char_count >= max_chars || (byte_idx + char_len) > max_bytes) {
            break;
        }

        byte_idx += char_len;
        valid_byte_len = byte_idx;
        char_count++;
    }

    str[valid_byte_len] = '\0';
}

void
ai_init(csilk_db_pool_t* pool)
{
    char* json = pool ? ai_settings_load(pool) : NULL;
    if (json) {
        int loaded = (ai_config_load_json(json, &g_config) == 0);
        free(json);
        if (loaded) {
            ai_provider_t* prov = ai_config_default_provider(&g_config);
            if (prov) {
                const char* key = (prov->api_key[0] != '\0') ? prov->api_key : "dummy";
                const char* dname = get_driver_name(prov->id);
                g_ai = csilk_ai_new(dname, key, prov->base_url[0] ? prov->base_url : NULL);
                if (g_ai) {
                    printf("AI initialized (DB): provider=%s model=%s\n",
                           prov->id,
                           g_config.default_model);
                } else {
                    fprintf(
                        stderr, "WARN: failed to create AI instance for provider '%s'\n", prov->id);
                }
                return;
            }
            /* DB config loaded but no usable provider — keep g_config, do not fall through */
            fprintf(stderr,
                    "WARN: default provider '%s' not found in DB AI config\n",
                    g_config.default_provider);
            return;
        }
    }
    const char* cfg_path = config_env_get("AI_CONFIG", NULL, 0, "config/ai.json");
    if (ai_config_load(cfg_path, &g_config) != 0) {
        fprintf(stderr, "WARN: could not load ai_config from %s\n", cfg_path);
        return;
    }
    ai_provider_t* prov = ai_config_default_provider(&g_config);
    if (!prov) {
        fprintf(
            stderr, "WARN: default provider '%s' not found in config\n", g_config.default_provider);
        return;
    }
    char        api_key_buf[512];
    const char* sec_key = config_secret_get("AI_API_KEY", api_key_buf, sizeof(api_key_buf));
    const char* key = (sec_key && sec_key[0])      ? sec_key
                      : (prov->api_key[0] != '\0') ? prov->api_key
                                                   : "";
    const char* dname = get_driver_name(prov->id);
    g_ai = csilk_ai_new(dname, key, prov->base_url[0] ? prov->base_url : NULL);
    if (g_ai) {
        printf("AI initialized (file): provider=%s model=%s\n", prov->id, g_config.default_model);
    } else {
        fprintf(stderr, "WARN: failed to create AI instance for provider '%s'\n", prov->id);
    }
}

void
ai_shutdown(void)
{
    if (g_ai) {
        csilk_ai_free(g_ai);
        g_ai = NULL;
    }
    ai_config_free(&g_config);
}

ai_config_t*
ai_get_config(void)
{
    return &g_config;
}

static void
send_done(csilk_ctx_t* c)
{
    csilk_json_t* d = csilk_json_object();
    csilk_json_add_string(d, "finish_reason", "stop");
    size_t slen = 0;
    char*  s = csilk_json_serialize(d, &slen);
    csilk_sse_send(c, "done", s ? s : "");
    free(s);
    csilk_json_free(d);
}

static void
format_friendly_error(const char* raw_err, char* out_buf, size_t out_cap)
{
    if (!raw_err || !raw_err[0]) {
        snprintf(out_buf, out_cap, "AI 请求失败");
        return;
    }
    if (strstr(raw_err, "401") || strstr(raw_err, "Unauthorized")) {
        snprintf(out_buf,
                 out_cap,
                 "API Key 无效或未配置 (HTTP 401)，请前往「设置 - AI 助手」检查并配置有效 API Key");
    } else if (strstr(raw_err, "404") || strstr(raw_err, "Not Found")) {
        snprintf(
            out_buf, out_cap, "模型不存在或接口地址错误 (HTTP 404)，请检查模型名称和 Base URL");
    } else if (strstr(raw_err, "429")) {
        snprintf(out_buf, out_cap, "请求过于频繁或额度不足 (HTTP 429)，请检查账户额度或稍后重试");
    } else {
        strncpy(out_buf, raw_err, out_cap - 1);
        out_buf[out_cap - 1] = '\0';
    }
}

static void
send_error(csilk_ctx_t* c, const char* err)
{
    char friendly_err[512];
    format_friendly_error(err, friendly_err, sizeof(friendly_err));
    csilk_json_t* d = csilk_json_object();
    csilk_json_add_string(d, "message", friendly_err);
    size_t slen = 0;
    char*  s = csilk_json_serialize(d, &slen);
    csilk_sse_send(c, "error", s ? s : "");
    free(s);
    csilk_json_free(d);
}

typedef struct {
    csilk_ctx_t*    ctx;
    struct timespec last_send_time;
    int             sse_initialized;
    ai_trace_t*     trace;
} sse_bridge_t;

static void
sse_bridge_text_chunk(const char* delta, void* udata)
{
    sse_bridge_t* b = (sse_bridge_t*)udata;
    if (!delta || !b || !b->ctx) {
        return;
    }
    if (!b->sse_initialized) {
        csilk_sse_init(b->ctx);
        clock_gettime(CLOCK_MONOTONIC, &b->last_send_time);
        b->sse_initialized = 1;
    }
    if (b->trace) {
        ai_trace_record_first_token(b->trace);
        ai_trace_append_output(b->trace, delta);
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed_ms = (now.tv_sec - b->last_send_time.tv_sec) * 1000 +
                      (now.tv_nsec - b->last_send_time.tv_nsec) / 1000000;
    if (elapsed_ms >= 15000) {
        csilk_sse_send(b->ctx, NULL, NULL);
    }

    struct timespec real_now;
    clock_gettime(CLOCK_REALTIME, &real_now);
    int64_t   ts_ms = (int64_t)real_now.tv_sec * 1000 + (real_now.tv_nsec / 1000000);
    struct tm tm_buf;
    localtime_r(&real_now.tv_sec, &tm_buf);
    char time_str[64];
    int  ms = (int)(real_now.tv_nsec / 1000000);
    snprintf(time_str,
             sizeof(time_str),
             "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tm_buf.tm_year + 1900,
             tm_buf.tm_mon + 1,
             tm_buf.tm_mday,
             tm_buf.tm_hour,
             tm_buf.tm_min,
             tm_buf.tm_sec,
             ms);

    csilk_json_t* msg = csilk_json_object();
    csilk_json_add_string(msg, "content", delta);
    csilk_json_add_number(msg, "timestamp", (double)ts_ms);
    csilk_json_add_string(msg, "time", time_str);
    size_t slen = 0;
    char*  s = csilk_json_serialize(msg, &slen);
    csilk_sse_send(b->ctx, "delta", s ? s : "");
    b->last_send_time = now;
    free(s);
    csilk_json_free(msg);
}

static void
sse_bridge_tool_call(const char* id, const char* name, const char* args_json, void* udata)
{
    sse_bridge_t* b = (sse_bridge_t*)udata;
    if (!b || !b->ctx) {
        return;
    }
    if (!b->sse_initialized) {
        csilk_sse_init(b->ctx);
        clock_gettime(CLOCK_MONOTONIC, &b->last_send_time);
        b->sse_initialized = 1;
    }
    csilk_json_t* evt = csilk_json_object();
    csilk_json_add_string(evt, "id", id ? id : "");
    csilk_json_add_string(evt, "name", name ? name : "");
    csilk_json_add_string(evt, "arguments", args_json ? args_json : "");
    size_t ev_len = 0;
    char*  ev_str = csilk_json_serialize(evt, &ev_len);
    csilk_sse_send(b->ctx, "tool_call", ev_str ? ev_str : "");
    free(ev_str);
    csilk_json_free(evt);
}

static void
sse_bridge_tool_result(const char* id, const char* name, const char* result_json, void* udata)
{
    sse_bridge_t* b = (sse_bridge_t*)udata;
    if (!b || !b->ctx) {
        return;
    }
    if (!b->sse_initialized) {
        csilk_sse_init(b->ctx);
        clock_gettime(CLOCK_MONOTONIC, &b->last_send_time);
        b->sse_initialized = 1;
    }
    csilk_json_t* evt = csilk_json_object();
    csilk_json_add_string(evt, "tool_call_id", id ? id : "");
    csilk_json_add_string(evt, "name", name ? name : "");
    csilk_json_t* parsed = csilk_json_parse(result_json ? result_json : "{}");
    if (parsed) {
        csilk_json_add_object(evt, "result", parsed);
    } else {
        csilk_json_add_string(evt, "result", result_json ? result_json : "");
    }
    size_t ev_len = 0;
    char*  ev_str = csilk_json_serialize(evt, &ev_len);
    csilk_sse_send(b->ctx, "tool_result", ev_str ? ev_str : "");
    free(ev_str);
    csilk_json_free(evt);
}

static void
sse_bridge_error(const ai_runtime_status_t* status, void* udata)
{
    sse_bridge_t* b = (sse_bridge_t*)udata;
    if (!b || !b->ctx) {
        return;
    }
    if (!b->sse_initialized) {
        csilk_sse_init(b->ctx);
        b->sse_initialized = 1;
    }
    const char* emsg =
        (status && status->detail[0])
            ? status->detail
            : ((status && status->message[0]) ? status->message : "AI request failed");
    send_error(b->ctx, emsg);
}

static void
sse_bridge_done(const ai_runtime_stats_t* stats, void* udata)
{
    sse_bridge_t* b = (sse_bridge_t*)udata;
    if (!b || !b->ctx) {
        return;
    }
    if (!b->sse_initialized) {
        csilk_sse_init(b->ctx);
        b->sse_initialized = 1;
    }
    send_done(b->ctx);
}

void
ai_chat_handler(csilk_ctx_t* c)
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

    int64_t     session_id = db_get_int(body, "session_id");
    const char* content = csilk_json_get_string(body, "content");
    const char* model_override = csilk_json_get_string(body, "model");
    const char* provider_override = csilk_json_get_string(body, "provider");
    int         regenerate = csilk_json_get_bool(body, "regenerate");
    double      req_temperature = db_get_num(body, "temperature");
    int         req_max_tokens = (int)db_get_int(body, "max_tokens");
    double      req_top_p = db_get_num(body, "top_p");

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
    char           provider_buf[64] = {0};
    char           model_buf[128] = {0};
    if (provider_override && provider_override[0]) {
        strncpy(provider_buf, provider_override, sizeof(provider_buf) - 1);
        prov = ai_config_find_provider(&g_config, provider_buf);
    }
    if (!prov) {
        prov = ai_config_default_provider(&g_config);
    }
    if (!prov) {
        csilk_json_free(body);
        respond_error(c, 500, "未找到可用 AI 供应商");
        return;
    }
    strncpy(model_buf, model_override ?: (prov->models[0] ?: "default"), sizeof(model_buf) - 1);

    /* session: create if new, or auto-title default "新对话" session */
    int64_t sid = session_id;
    if (sid <= 0) {
        char title[256];
        strncpy(title, content, sizeof(title) - 1);
        title[sizeof(title) - 1] = '\0';
        utf8_truncate(title, 20, 60);
        if (title[0] == '\0') {
            strcpy(title, "新对话");
        }
        sid = ai_session_insert(pool, user_id, title, model_buf, prov->id);
        if (sid <= 0) {
            csilk_json_free(body);
            respond_error(c, 500, "创建会话失败");
            return;
        }
    } else {
        csilk_json_t* sess = ai_session_get(pool, user_id, sid);
        if (!sess) {
            csilk_json_free(body);
            respond_not_found(c);
            return;
        }
        const char* current_title = csilk_json_get_string(csilk_json_array_get(sess, 0), "title");
        if (current_title && strcmp(current_title, "新对话") == 0 && !regenerate) {
            char title[256];
            strncpy(title, content, sizeof(title) - 1);
            title[sizeof(title) - 1] = '\0';
            utf8_truncate(title, 20, 60);
            if (title[0] != '\0') {
                ai_session_update(pool, user_id, sid, title, NULL);
            }
        }
        csilk_json_free(sess);
        if (regenerate) {
            ai_message_delete_last_assistant(pool, sid);
        }
    }

    /* load history */
    int           ctx_size = g_config.context_size > 0 ? g_config.context_size : 20;
    csilk_json_t* history = ai_message_recent(pool, sid, ctx_size);

    /* persist user message (skip on regenerate — already in history) */
    if (!regenerate) {
        ai_message_insert(pool, sid, "user", content, model_buf);
    }

    /* 构造统一 AI Runtime 上下文 */
    ai_runtime_context_t rctx;
    ai_runtime_context_init(&rctx);
    rctx.user_id = user_id;
    rctx.session_id = sid;
    snprintf(rctx.provider_id, sizeof(rctx.provider_id), "%s", prov->id);
    snprintf(rctx.model_name, sizeof(rctx.model_name), "%s", model_buf);
    rctx.temperature = (req_temperature > 0.0) ? req_temperature : 0.7;
    rctx.max_tokens = req_max_tokens;
    rctx.top_p = req_top_p;

    /* 构建受控记忆窗口 messages */
    csilk_json_free(rctx.messages);
    rctx.messages = ai_memory_build_messages(
        g_config.system_prompt, history, regenerate ? NULL : content, ctx_size);

    /* 初始化 Trace */
    ai_trace_t trace;
    ai_trace_init(&trace, user_id, sid);
    ai_trace_set_provider(&trace, prov->id, model_buf);
    ai_trace_set_params(&trace, req_temperature, req_max_tokens, req_top_p);
    ai_trace_set_system_prompt(&trace, g_config.system_prompt);

    csilk_json_t* input_arr = csilk_json_array();
    csilk_json_t* sys_msg = csilk_json_object();
    csilk_json_add_string(sys_msg, "role", "system");
    csilk_json_add_string(sys_msg, "content", g_config.system_prompt);
    csilk_json_array_append(input_arr, sys_msg);
    int hsz = history ? (int)csilk_json_array_size(history) : 0;
    for (int i = 0; i < hsz; i++) {
        csilk_json_t* m = csilk_json_array_get(history, i);
        csilk_json_t* im = csilk_json_object();
        csilk_json_add_string(im, "role", csilk_json_get_string(m, "role") ?: "");
        csilk_json_add_string(im, "content", csilk_json_get_string(m, "content") ?: "");
        csilk_json_array_append(input_arr, im);
    }
    if (!regenerate) {
        csilk_json_t* usr_msg = csilk_json_object();
        csilk_json_add_string(usr_msg, "role", "user");
        csilk_json_add_string(usr_msg, "content", content);
        csilk_json_array_append(input_arr, usr_msg);
    }
    ai_trace_serialize_messages(&trace, input_arr);
    csilk_json_free(input_arr);

    rctx.trace = &trace;

    /* SSE init: establish event-stream connection immediately so proxies never time out */
    csilk_sse_init(c);

    sse_bridge_t sse_bridge = {
        .ctx = c,
        .last_send_time = {0},
        .sse_initialized = 1,
        .trace = &trace,
    };
    clock_gettime(CLOCK_MONOTONIC, &sse_bridge.last_send_time);

    ai_runtime_callbacks_t cbs = {
        .on_text_chunk = sse_bridge_text_chunk,
        .on_tool_call = sse_bridge_tool_call,
        .on_tool_result = sse_bridge_tool_result,
        .on_error = sse_bridge_error,
        .on_done = sse_bridge_done,
    };

    /* 委托 AI Runtime 执行 Agent 循环 */
    ai_runtime_execute_stream(pool, &rctx, &cbs, &sse_bridge);

    csilk_sse_close(c);
    ai_trace_free(&trace);
    ai_runtime_context_free(&rctx);
    if (history) {
        csilk_json_free(history);
    }
    csilk_json_free(body);
}
void
ai_service_test_connection(csilk_ctx_t* c)
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

    const char* id = csilk_json_get_string(body, "id") ?: "openai";
    const char* base_url = csilk_json_get_string(body, "base_url");
    const char* k_enc = csilk_json_get_string(body, "api_key_enc");
    const char* k = csilk_json_get_string(body, "api_key");
    const char* model = csilk_json_get_string(body, "model");

    char        key_buf[512] = {0};
    const char* raw_k = (k_enc && k_enc[0]) ? k_enc : k;
    if (raw_k && raw_k[0]) {
        size_t dec_len = sizeof(key_buf);
        if (auth_key_decrypt(raw_k, key_buf, &dec_len) != 0) {
            size_t _k_len = strlen(raw_k);
            size_t _copy_len = _k_len < sizeof(key_buf) - 1 ? _k_len : sizeof(key_buf) - 1;
            memcpy(key_buf, raw_k, _copy_len);
            key_buf[_copy_len] = '\0';
        }
    } else {
        ai_provider_t* old_p = ai_config_find_provider(&g_config, id);
        if (old_p && old_p->api_key[0]) {
            size_t _src_len = strlen(old_p->api_key);
            size_t _copy_len = _src_len < sizeof(key_buf) - 1 ? _src_len : sizeof(key_buf) - 1;
            memcpy(key_buf, old_p->api_key, _copy_len);
            key_buf[_copy_len] = '\0';
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

    const char* test_model = model;
    if (!test_model || !test_model[0]) {
        ai_provider_t* p = ai_config_find_provider(&g_config, id);
        if (p && p->model_count > 0 && p->models[0] && p->models[0][0]) {
            test_model = p->models[0];
        } else if (strcmp(id, "deepseek") == 0) {
            test_model = "deepseek-chat";
        } else if (strcmp(id, "openai") == 0) {
            test_model = "gpt-4o-mini";
        } else if (strcmp(id, "qwen") == 0) {
            test_model = "qwen-plus";
        } else if (strcmp(id, "ollama") == 0) {
            test_model = "llama3";
        } else {
            test_model = "default";
        }
    }

    csilk_ai_message_t      msg = {.role = "user", .content = "ping"};
    csilk_ai_chat_request_t req = {
        .model = test_model,
        .messages = &msg,
        .message_count = 1,
        .stream = 0,
        .timeout_ms = 10000,
    };
    csilk_ai_chat_response_t res = {0};
    int                      rc = csilk_ai_chat(test_ai, &req, &res);
    clock_gettime(CLOCK_MONOTONIC, &t2);

    long latency_ms = (t2.tv_sec - t1.tv_sec) * 1000 + (t2.tv_nsec - t1.tv_nsec) / 1000000;
    if (latency_ms < 0) {
        latency_ms = 0;
    }

    csilk_json_t* resp = csilk_json_object();
    if (rc == 0) {
        csilk_json_add_bool(resp, "success", true);
        csilk_json_add_number(resp, "latency_ms", (double)latency_ms);
        csilk_json_add_string(resp, "message", "连接成功");
    } else {
        const char* raw_err =
            (res.error_message && res.error_message[0]) ? res.error_message : "连接超时或失败";
        char friendly_err[512];
        format_friendly_error(raw_err, friendly_err, sizeof(friendly_err));
        csilk_json_add_bool(resp, "success", false);
        csilk_json_add_number(resp, "latency_ms", (double)latency_ms);
        csilk_json_add_string(resp, "message", friendly_err);
    }

    csilk_ai_chat_response_free(&res);
    csilk_ai_free(test_ai);
    csilk_json_free(body);
    respond_ok(c, resp);
}

typedef struct {
    char*  data;
    size_t size;
} memory_buf_t;

static size_t
curl_write_memory_cb(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t        realsize = size * nmemb;
    memory_buf_t* mem = (memory_buf_t*)userp;
    char*         ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) {
        return 0;
    }
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    return realsize;
}

void
ai_service_fetch_models(csilk_ctx_t* c)
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

    const char* id = csilk_json_get_string(body, "id") ?: "openai";
    const char* base_url = csilk_json_get_string(body, "base_url");
    const char* k_enc = csilk_json_get_string(body, "api_key_enc");
    const char* k = csilk_json_get_string(body, "api_key");

    char        key_buf[512] = {0};
    const char* raw_k = (k_enc && k_enc[0]) ? k_enc : k;
    if (raw_k && raw_k[0]) {
        size_t dec_len = sizeof(key_buf);
        if (auth_key_decrypt(raw_k, key_buf, &dec_len) != 0) {
            size_t _k_len = strlen(raw_k);
            size_t _copy_len = _k_len < sizeof(key_buf) - 1 ? _k_len : sizeof(key_buf) - 1;
            memcpy(key_buf, raw_k, _copy_len);
            key_buf[_copy_len] = '\0';
        }
    } else {
        ai_provider_t* old_p = ai_config_find_provider(&g_config, id);
        if (old_p && old_p->api_key[0]) {
            size_t _src_len = strlen(old_p->api_key);
            size_t _copy_len = _src_len < sizeof(key_buf) - 1 ? _src_len : sizeof(key_buf) - 1;
            memcpy(key_buf, old_p->api_key, _copy_len);
            key_buf[_copy_len] = '\0';
        } else {
            strcpy(key_buf, "dummy");
        }
    }

    char url[512];
    if (base_url && base_url[0]) {
        size_t blen = strlen(base_url);
        if (base_url[blen - 1] == '/') {
            snprintf(url, sizeof(url), "%smodels", base_url);
        } else {
            snprintf(url, sizeof(url), "%s/models", base_url);
        }
    } else {
        snprintf(url, sizeof(url), "https://api.openai.com/v1/models");
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        csilk_json_free(body);
        respond_error(c, 500, "CURL 初始化失败");
        return;
    }

    memory_buf_t chunk = {.data = malloc(1), .size = 0};
    if (chunk.data) {
        chunk.data[0] = '\0';
    }

    struct curl_slist* headers = NULL;
    char               auth_hdr[512];
    snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", key_buf);
    headers = curl_slist_append(headers, auth_hdr);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_memory_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    long     http_code = 0;
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
                    const char*         mid = csilk_json_get_string(item, "id");
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
                    const char*         mid = csilk_json_get_string(item, "name");
                    if (!mid) {
                        mid = csilk_json_get_string(item, "id");
                    }
                    if (!mid) {
                        mid = csilk_json_string_value(item);
                    }
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

int
ai_service_stream_report(csilk_ctx_t* c,
                         int64_t      user_id,
                         int64_t      session_id,
                         const char*  workflow_title,
                         const char*  structured_data_json)
{
    if (!g_config.default_provider[0] || !g_config.default_model[0]) {
        return 0;
    }
    ai_provider_t* prov = ai_config_find_provider(&g_config, g_config.default_provider);
    if (!prov || (!prov->api_key[0] && strcmp(prov->id, "ollama") != 0)) {
        return 0;
    }

    const char* sys_prompt =
        "你是一名资深全栈私人财务顾问与资产配置专家。你正在执行自动化财务工作流。\n"
        "【真实性与防编造铁律（最高优先级）】\n"
        "1. "
        "严格基于真实数据：报告中的所有数字、金额、资产名称、分类名称、日期、比例、负债与交易明细，"
        "必须 100% 严格来源于系统提供的真实 JSON 上下文。\n"
        "2. 绝对严禁编造或假设：严禁凭空捏造、杜撰或虚构任何未在数据中出现的项目、数字或假数据。\n"
        "3. 数据缺失/为0处理：若某项数据在 JSON 中为 0、空数组 [] "
        "或暂无记录，必须如实向用户陈述「当前暂无相关数据」或显示为 "
        "0，并给出如何开始记账/完善资产的实操建议，严禁自行编造示例数据！\n"
        "4. Mermaid 图表规范：Mermaid 图表（如饼图、柱状图）中的标签与数值必须与真实 JSON "
        "完全对应。若分类为空或数据为 0，严禁生成假数据图表，应直接以文字提示说明。\n"
        "5. 输出要求：使用规范优雅的 Markdown 格式，结构清晰、重点突出；在报告末尾提供 3 "
        "条切实可行的优化建议，如需记账或调仓可输出合法 ```action { ... } ``` 操作卡片。";

    char now_str[64];
    {
        time_t    now = time(NULL);
        struct tm tm_buf;
        localtime_r(&now, &tm_buf);
        snprintf(now_str,
                 sizeof(now_str),
                 "%04d-%02d-%02d %02d:%02d:%02d",
                 tm_buf.tm_year + 1900,
                 tm_buf.tm_mon + 1,
                 tm_buf.tm_mday,
                 tm_buf.tm_hour,
                 tm_buf.tm_min,
                 tm_buf.tm_sec);
    }
    char user_prompt[16384];
    snprintf(user_prompt,
             sizeof(user_prompt),
             "【当前工作流】：%s\n"
             "【当前真实时间】：%s（服务器本地真实时间）\n\n"
             "【系统从数据库提取的用户真实财务上下文 JSON】：\n"
             "%s\n\n"
             "【指令与防幻觉要求】：\n"
             "请基于上述 100%% "
             "真实的财务数据与当前时间，直接生成专业结构化的财务诊断复盘报告与优化建议。"
             "再次强调：所有引用的金额、指标与分类必须完全忠实于上下文数据，严禁编造任何假数据或模"
             "拟数字；如数据为空请如实说明。",
             workflow_title ? workflow_title : "智能财务工作流",
             now_str,
             structured_data_json ? structured_data_json : "{}");

    ai_trace_t trace;
    ai_trace_init(&trace, user_id, session_id);
    ai_trace_set_provider(&trace, prov->id, g_config.default_model);
    ai_trace_set_system_prompt(&trace, sys_prompt);

    csilk_json_t* input_arr = csilk_json_array();
    csilk_json_t* sys_msg = csilk_json_object();
    csilk_json_add_string(sys_msg, "role", "system");
    csilk_json_add_string(sys_msg, "content", sys_prompt);
    csilk_json_array_append(input_arr, sys_msg);
    csilk_json_t* usr_msg = csilk_json_object();
    csilk_json_add_string(usr_msg, "role", "user");
    csilk_json_add_string(usr_msg, "content", user_prompt);
    csilk_json_array_append(input_arr, usr_msg);
    ai_trace_serialize_messages(&trace, input_arr);
    csilk_json_free(input_arr);

    sse_bridge_t sse_bridge = {
        .ctx = c,
        .last_send_time = {0},
        .sse_initialized = 1,
        .trace = &trace,
    };
    clock_gettime(CLOCK_MONOTONIC, &sse_bridge.last_send_time);

    ai_runtime_callbacks_t cbs = {
        .on_text_chunk = sse_bridge_text_chunk,
        .on_tool_call = sse_bridge_tool_call,
        .on_tool_result = sse_bridge_tool_result,
        .on_error = sse_bridge_error,
        .on_done = sse_bridge_done,
    };

    ai_runtime_context_t rctx;
    ai_runtime_context_init(&rctx);
    rctx.user_id = user_id;
    rctx.session_id = session_id;
    snprintf(rctx.provider_id, sizeof(rctx.provider_id), "%s", prov->id);
    snprintf(rctx.model_name, sizeof(rctx.model_name), "%s", g_config.default_model);
    csilk_json_free(rctx.messages);
    rctx.messages = ai_memory_build_messages(sys_prompt, NULL, user_prompt, 1);
    rctx.trace = &trace;

    ai_runtime_status_t status = ai_runtime_execute_stream(db_get_pool(), &rctx, &cbs, &sse_bridge);
    ai_trace_free(&trace);
    ai_runtime_context_free(&rctx);

    return (status.code == AI_RUNTIME_ERR_OK) ? 1 : 0;
}
