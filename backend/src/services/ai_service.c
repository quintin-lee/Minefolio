#include "services/ai_service.h"
#include "repositories/ai_session_repo.h"
#include "common/ai_config.h"
#include "common/db.h"
#include "common/response.h"
#include "common/ctx.h"
#include "csilk/drivers/ai.h"
#include "csilk/protocols/sse.h"
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
    (void)pool;
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
    if (g_ai) printf("AI initialized: provider=%s model=%s\n", prov->id, g_config.default_model);
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
} stream_context_t;

static void on_chunk(const char* delta, void* data) {
    stream_context_t* sc = (stream_context_t*)data;
    if (!delta || !sc || !sc->ctx) return;

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
    };

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
        send_error(c, "AI request failed");
    } else {
        if (sctx.accumulated && sctx.len > 0) {
            ai_message_insert(pool, sid, "assistant", sctx.accumulated, model_buf);
        }
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
