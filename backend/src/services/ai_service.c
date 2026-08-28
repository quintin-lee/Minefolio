#include "services/ai_service.h"
#include "services/ai_tools.h"
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
    const char* cfg_path = getenv("MINEFOLIO_AI_CONFIG") ?: "config/ai.json";
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
    const char* key = (prov->api_key[0] != '\0') ? prov->api_key : "dummy";
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

typedef struct {
    csilk_ctx_t*    ctx;
    char*           accumulated;
    size_t          cap;
    size_t          len;
    ai_trace_t*     trace;
    struct timespec last_send_time;
    int             sse_initialized;
} stream_context_t;

static void
ensure_sse_init(stream_context_t* sc)
{
    if (!sc || sc->sse_initialized) {
        return;
    }
    csilk_sse_init(sc->ctx);
    clock_gettime(CLOCK_MONOTONIC, &sc->last_send_time);
    sc->sse_initialized = 1;
}

static size_t
utf8_safe_chunk_len(const char* s, size_t max_len, size_t total_remain)
{
    if (total_remain <= max_len) {
        return total_remain;
    }
    size_t len = max_len;
    while (len > 0 && ((unsigned char)s[len] & 0xC0) == 0x80) {
        len--;
    }
    return len > 0 ? len : 1;
}

static void
on_chunk(const char* delta, void* data)
{
    stream_context_t* sc = (stream_context_t*)data;
    if (!delta || !sc || !sc->ctx) {
        return;
    }

    ensure_sse_init(sc);

    if (sc->trace) {
        ai_trace_record_first_token(sc->trace);
        ai_trace_append_output(sc->trace, delta);
    }

    size_t dlen = strlen(delta);
    if (sc->len + dlen + 1 > sc->cap) {
        size_t ncap = (sc->len + dlen + 1) * 2;
        if (ncap < 1024) {
            ncap = 1024;
        }
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

    /* heartbeat: send SSE comment if idle >15s to prevent proxy timeout */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed_ms = (now.tv_sec - sc->last_send_time.tv_sec) * 1000 +
                      (now.tv_nsec - sc->last_send_time.tv_nsec) / 1000000;
    if (elapsed_ms >= 15000) {
        csilk_sse_send(sc->ctx, NULL, NULL);
    }

    struct timespec real_now;
    clock_gettime(CLOCK_REALTIME, &real_now);
    int64_t ts_ms = (int64_t)real_now.tv_sec * 1000 + (real_now.tv_nsec / 1000000);

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
    csilk_sse_send(sc->ctx, "delta", s ? s : "");
    sc->last_send_time = now;
    free(s);
    csilk_json_free(msg);
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
    int           hsz = history ? (int)csilk_json_array_size(history) : 0;

    /* build messages: [system, ...history] or [system, ...history, user] */
    int                 initial_mc = regenerate ? (1 + hsz) : (1 + hsz + 1);
    int                 mc = initial_mc;
    csilk_ai_message_t* msgs = (csilk_ai_message_t*)malloc(sizeof(csilk_ai_message_t) * (size_t)mc);
    if (!msgs) {
        csilk_json_free(history);
        csilk_json_free(body);
        respond_error(c, 500, "内存不足");
        return;
    }
    int idx = 0;
    msgs[idx++] = (csilk_ai_message_t){.role = "system", .content = g_config.system_prompt};
    if (history) {
        for (int i = 0; i < hsz; i++) {
            csilk_json_t* m = csilk_json_array_get(history, i);
            msgs[idx++] = (csilk_ai_message_t){
                .role = csilk_json_get_string(m, "role"),
                .content = csilk_json_get_string(m, "content"),
            };
        }
    }
    if (!regenerate) {
        msgs[idx++] = (csilk_ai_message_t){.role = "user", .content = content};
    }

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
    ai_trace_set_params(&trace, req_temperature, req_max_tokens, req_top_p);
    ai_trace_set_system_prompt(&trace, g_config.system_prompt);
    sctx.trace = &trace;

    csilk_json_t* input_arr = csilk_json_array();
    csilk_json_t* sys_msg = csilk_json_object();
    csilk_json_add_string(sys_msg, "role", "system");
    csilk_json_add_string(sys_msg, "content", g_config.system_prompt);
    csilk_json_array_append(input_arr, sys_msg);
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

    /* SSE init: establish event-stream connection immediately so proxies never time out */
    csilk_sse_init(c);
    clock_gettime(CLOCK_MONOTONIC, &sctx.last_send_time);
    sctx.sse_initialized = 1;

    /* persist user message (skip on regenerate — already in history) */
    if (!regenerate) {
        ai_message_insert(pool, sid, "user", content, model_buf);
    }

    csilk_ai_t* ai_inst = g_ai;
    int         need_free_ai = 0;
    if (prov && (strcmp(prov->id, g_config.default_provider) != 0 || !ai_inst)) {
        const char* dname = get_driver_name(prov->id);
        const char* key = (prov->api_key[0] != '\0') ? prov->api_key : "dummy";
        csilk_ai_t* custom_ai = csilk_ai_new(dname, key, prov->base_url[0] ? prov->base_url : NULL);
        if (custom_ai) {
            ai_inst = custom_ai;
            need_free_ai = 1;
        }
    }

    /* ---- Tool-calling loop ----
     * First call: non-streaming with tools → check for tool_calls.
     * If tool_calls: execute each, append results, loop.
     * Final call: streaming text response → SSE to client. */
    size_t                 tool_count = 0;
    const csilk_ai_tool_t* tools = ai_tools_get_definitions(&tool_count);

    int round = 0;
    int max_rounds = 10;
    int got_text = 0;
    int total_prompt_tokens = 0;
    int total_completion_tokens = 0;

    while (!got_text && round < max_rounds) {
        csilk_ai_chat_request_t req = {
            .model = model_buf,
            .messages = msgs,
            .message_count = (size_t)mc,
            .stream = 1,
            .on_chunk = on_chunk,
            .user_data = &sctx,
            .tools = (csilk_ai_tool_t*)tools,
            .tool_count = tool_count,
            .tool_choice = "auto",
        };
        csilk_ai_chat_response_t ai_res = {0};
        int                      rc = csilk_ai_chat(ai_inst, &req, &ai_res);
        if (ai_res.prompt_tokens > 0) {
            total_prompt_tokens += ai_res.prompt_tokens;
        }
        if (ai_res.completion_tokens > 0) {
            total_completion_tokens += ai_res.completion_tokens;
        }

        if (rc != 0) {
            ai_trace_calculate_tokens_and_cost(
                &trace, total_prompt_tokens, total_completion_tokens);
            ai_trace_finish(&trace,
                            "error",
                            (ai_res.error_message && ai_res.error_message[0])
                                ? ai_res.error_message
                                : "AI request failed");
            ai_trace_save(db_get_pool(), &trace);
            ai_trace_free(&trace);
            ensure_sse_init(&sctx);
            send_error(c,
                       (ai_res.error_message && ai_res.error_message[0]) ? ai_res.error_message
                                                                         : "AI request failed");
            if (need_free_ai && ai_inst) {
                csilk_ai_free(ai_inst);
            }
            if (sctx.accumulated) {
                free(sctx.accumulated);
            }
            csilk_sse_close(c);
            for (int i = initial_mc; i < mc; i++) {
                if (msgs[i].content) {
                    free((void*)msgs[i].content);
                }
                if (msgs[i].tool_call_id) {
                    free((void*)msgs[i].tool_call_id);
                }
                if (msgs[i].tool_calls) {
                    for (size_t j = 0; j < msgs[i].tool_call_count; j++) {
                        free(msgs[i].tool_calls[j].id);
                        free(msgs[i].tool_calls[j].name);
                        free(msgs[i].tool_calls[j].arguments);
                    }
                    free(msgs[i].tool_calls);
                }
            }
            free(msgs);
            csilk_json_free(history);
            csilk_json_free(body);
            return;
        }

        if (ai_res.tool_call_count == 0) {
            const char* text = ai_res.content ?: (sctx.accumulated ?: "");
            ai_message_insert(pool, sid, "assistant", text, model_buf);
            csilk_ai_chat_response_free(&ai_res);
            got_text = 1;
            break;
        }

        mc++;
        msgs = (csilk_ai_message_t*)realloc(msgs, sizeof(csilk_ai_message_t) * (size_t)mc);
        msgs[mc - 1] = (csilk_ai_message_t){
            .role = "assistant",
            .content = ai_res.content ? strdup(ai_res.content) : strdup(""),
            .tool_call_id = NULL,
        };
        /* Deep-copy tool_calls: we own these strings since ai_res will be freed next */
        {
            size_t n = ai_res.tool_call_count;
            msgs[mc - 1].tool_calls =
                (csilk_ai_tool_call_t*)malloc(sizeof(csilk_ai_tool_call_t) * n);
            msgs[mc - 1].tool_call_count = n;
            for (size_t j = 0; j < n; j++) {
                msgs[mc - 1].tool_calls[j].id =
                    ai_res.tool_calls[j].id ? strdup(ai_res.tool_calls[j].id) : strdup("");
                msgs[mc - 1].tool_calls[j].name =
                    ai_res.tool_calls[j].name ? strdup(ai_res.tool_calls[j].name) : strdup("");
                msgs[mc - 1].tool_calls[j].arguments = ai_res.tool_calls[j].arguments
                                                           ? strdup(ai_res.tool_calls[j].arguments)
                                                           : strdup("{}");
            }
        }

        /* Pre-parse all argument strings once */
        csilk_json_t** parsed_args =
            (csilk_json_t**)malloc(sizeof(csilk_json_t*) * ai_res.tool_call_count);
        for (size_t t = 0; t < ai_res.tool_call_count; t++) {
            csilk_json_t* a = csilk_json_parse(ai_res.tool_calls[t].arguments);
            parsed_args[t] = a ? a : csilk_json_object();
        }

        /* 2. Execute each tool and append corresponding role="tool" messages */
        for (size_t t = 0; t < ai_res.tool_call_count; t++) {
            csilk_ai_tool_call_t* tc = &ai_res.tool_calls[t];
            csilk_json_t*         args = parsed_args[t];

            ensure_sse_init(&sctx);
            /* Properly escaped JSON via csilk_json — no snprintf truncation / injection */
            {
                csilk_json_t* evt = csilk_json_object();
                csilk_json_add_string(evt, "id", tc->id ?: "");
                csilk_json_add_string(evt, "name", tc->name ?: "");
                csilk_json_add_string(evt, "arguments", tc->arguments ?: "");
                size_t ev_len = 0;
                char*  ev_str = csilk_json_serialize(evt, &ev_len);
                csilk_sse_send(c, "tool_call", ev_str ? ev_str : "");
                free(ev_str);
                csilk_json_free(evt);
            }

            char*           result = NULL;
            struct timespec ts0, ts1;
            clock_gettime(CLOCK_MONOTONIC, &ts0);
            result = ai_tools_execute_parsed(pool, user_id, args, tc->name);
            clock_gettime(CLOCK_MONOTONIC, &ts1);
            long span_ms = (ts1.tv_sec - ts0.tv_sec) * 1000 + (ts1.tv_nsec - ts0.tv_nsec) / 1000000;
            if (!result) {
                result = strdup("{\"error\":\"tool execution failed\"}");
                ai_trace_add_tool_span(&trace, tc->name ?: "", span_ms, 0, 0);
            } else {
                ai_trace_add_tool_span(&trace, tc->name ?: "", span_ms, strlen(result), 1);
            }

            {
                csilk_json_t* evt = csilk_json_object();
                csilk_json_add_string(evt, "tool_call_id", tc->id ?: "");
                csilk_json_add_string(evt, "name", tc->name ?: "");
                csilk_json_t* parsed = csilk_json_parse(result);
                if (parsed) {
                    csilk_json_add_object(evt, "result", parsed);
                } else {
                    csilk_json_add_string(evt, "result", result);
                }
                size_t ev_len = 0;
                char*  ev_str = csilk_json_serialize(evt, &ev_len);
                csilk_sse_send(c, "tool_result", ev_str ? ev_str : "");
                free(ev_str);
                csilk_json_free(evt);
            }
            /* result owned by msgs[mc-1].content below; don't free here */

            /* Build tool result message */
            mc++;
            msgs = (csilk_ai_message_t*)realloc(msgs, sizeof(csilk_ai_message_t) * (size_t)mc);
            msgs[mc - 1] = (csilk_ai_message_t){
                .role = "tool",
                .content = result,
                .tool_call_id = tc->id ? strdup(tc->id) : strdup(""),
                .tool_calls = NULL,
                .tool_call_count = 0,
            };
        }

        /* Free pre-parsed argument objects */
        for (size_t t = 0; t < ai_res.tool_call_count; t++) {
            csilk_json_free(parsed_args[t]);
        }
        free(parsed_args);

        /* Reset sctx.accumulated for the next text generation round */
        if (sctx.accumulated) {
            sctx.accumulated[0] = '\0';
            sctx.len = 0;
        }

        csilk_ai_chat_response_free(&ai_res);
        round++;
    }

    ai_trace_calculate_tokens_and_cost(&trace, total_prompt_tokens, total_completion_tokens);
    ai_trace_finish(&trace, got_text ? "ok" : "error", NULL);
    ai_trace_save(db_get_pool(), &trace);
    ai_trace_free(&trace);
    ensure_sse_init(&sctx);
    send_done(c);

    if (need_free_ai && ai_inst) {
        csilk_ai_free(ai_inst);
    }
    if (sctx.accumulated) {
        free(sctx.accumulated);
    }
    csilk_sse_close(c);
    /* Free dynamically allocated content strings from tool-call rounds.
     * The first initial_mc messages point into g_config/history/body (not owned). */
    for (int i = initial_mc; i < mc; i++) {
        if (msgs[i].content) {
            free((void*)msgs[i].content);
        }
        if (msgs[i].tool_call_id) {
            free((void*)msgs[i].tool_call_id);
        }
        if (msgs[i].tool_calls) {
            for (size_t j = 0; j < msgs[i].tool_call_count; j++) {
                free(msgs[i].tool_calls[j].id);
                free(msgs[i].tool_calls[j].name);
                free(msgs[i].tool_calls[j].arguments);
            }
            free(msgs[i].tool_calls);
        }
    }
    free(msgs);
    csilk_json_free(history);
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

    csilk_ai_t* ai_inst = g_ai;
    int         need_free_ai = 0;
    if (strcmp(prov->id, g_config.default_provider) != 0 || !ai_inst) {
        const char* dname = get_driver_name(prov->id);
        const char* key = (prov->api_key[0] != '\0') ? prov->api_key : "dummy";
        csilk_ai_t* custom_ai = csilk_ai_new(dname, key, prov->base_url[0] ? prov->base_url : NULL);
        if (custom_ai) {
            ai_inst = custom_ai;
            need_free_ai = 1;
        }
    }
    if (!ai_inst) {
        return 0;
    }

    ai_trace_t trace;
    ai_trace_init(&trace, user_id, session_id);
    ai_trace_set_provider(&trace, prov->id, g_config.default_model);

    stream_context_t sctx = {
        .ctx = c,
        .sse_initialized = 1,
        .accumulated = NULL,
        .cap = 0,
        .len = 0,
        .trace = &trace,
    };
    clock_gettime(CLOCK_MONOTONIC, &sctx.last_send_time);

    const char* sys_prompt = "你是一名资深全栈私人财务顾问与资产配置专家。你正在执行自动化财务工作"
                             "流。请根据系统聚合提供的用户真实财务数据上下文（JSON），生成一份专业"
                             "、深入、结构清晰且具有针对性的诊断分析报告。\n"
                             "要求：\n"
                             "1. 使用优雅规范的 Markdown 输出；\n"
                             "2. 深度分析核心收支、结余率、资产负债与应急保障指标；\n"
                             "3. 结合真实数据生成一个 Mermaid 图表展示支出构成或资产大类配置；\n"
                             "4. 提供 3 条切实可行的落地优化建议；\n"
                             "5. 如需记账或调仓，可生成 ```action { ... } ``` 操作卡片。";

    char user_prompt[16384];
    snprintf(user_prompt,
             sizeof(user_prompt),
             "【当前工作流】：%s\n\n"
             "【系统已提取的用户真实财务上下文（JSON）】：\n"
             "%s\n\n"
             "请基于以上全部真实数据，直接输出结构化深度财务诊断报告与优化建议。",
             workflow_title ? workflow_title : "智能财务工作流",
             structured_data_json ? structured_data_json : "{}");

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

    csilk_ai_message_t msgs[2] = {
        {.role = "system", .content = sys_prompt },
        {.role = "user",   .content = user_prompt},
    };

    csilk_ai_chat_request_t req = {
        .model = g_config.default_model,
        .messages = msgs,
        .message_count = 2,
        .stream = 1,
        .on_chunk = on_chunk,
        .user_data = &sctx,
        .tools = NULL,
        .tool_count = 0,
    };

    csilk_ai_chat_response_t ai_res = {0};
    int                      rc = csilk_ai_chat(ai_inst, &req, &ai_res);

    if (need_free_ai && ai_inst) {
        csilk_ai_free(ai_inst);
    }

    if (rc != 0) {
        ai_trace_calculate_tokens_and_cost(&trace, ai_res.prompt_tokens, ai_res.completion_tokens);
        ai_trace_finish(
            &trace, "error", ai_res.error_message ? ai_res.error_message : "AI request failed");
        ai_trace_save(db_get_pool(), &trace);
        ai_trace_free(&trace);
        csilk_ai_chat_response_free(&ai_res);
        if (sctx.accumulated) {
            free(sctx.accumulated);
        }
        return 0;
    }

    ai_trace_calculate_tokens_and_cost(&trace, ai_res.prompt_tokens, ai_res.completion_tokens);
    ai_trace_finish(&trace, "ok", NULL);
    ai_trace_save(db_get_pool(), &trace);
    ai_trace_free(&trace);

    const char* text = ai_res.content ?: (sctx.accumulated ?: "");
    if (text && text[0] && session_id > 0) {
        csilk_db_pool_t* pool = db_get_pool();
        if (pool) {
            ai_message_insert(pool, session_id, "assistant", text, g_config.default_model);
        }
    }

    csilk_ai_chat_response_free(&ai_res);
    if (sctx.accumulated) {
        free(sctx.accumulated);
    }
    return 1;
}
