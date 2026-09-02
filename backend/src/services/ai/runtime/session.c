#include "services/ai/runtime/session.h"
#include "repositories/ai_session_repo.h"
#include "common/db.h"
#include <stdlib.h>
#include <string.h>

ai_session_context_t*
ai_session_load_or_create(csilk_db_pool_t* pool,
                          int64_t          user_id,
                          int64_t          session_id,
                          const char*      provider,
                          const char*      model)
{
    if (!pool || user_id <= 0) {
        return NULL;
    }
    ai_session_context_t* sctx = (ai_session_context_t*)calloc(1, sizeof(ai_session_context_t));
    if (!sctx) {
        return NULL;
    }
    sctx->user_id = user_id;

    if (session_id > 0) {
        csilk_json_t* sess = ai_session_get(pool, user_id, session_id);
        if (sess && csilk_json_array_size(sess) > 0) {
            const csilk_json_t* row = csilk_json_array_get(sess, 0);
            sctx->session_id = session_id;
            const char* t = csilk_json_get_string(row, "title");
            if (t) {
                strncpy(sctx->title, t, sizeof(sctx->title) - 1);
            }
            const char* p = csilk_json_get_string(row, "provider");
            if (p) {
                strncpy(sctx->provider, p, sizeof(sctx->provider) - 1);
            }
            const char* m = csilk_json_get_string(row, "model");
            if (m) {
                strncpy(sctx->model, m, sizeof(sctx->model) - 1);
            }
            sctx->messages = ai_message_recent(pool, session_id, 50);
            csilk_json_free(sess);
            return sctx;
        }
        if (sess) {
            csilk_json_free(sess);
        }
    }

    int64_t new_id = ai_session_insert(
        pool, user_id, "新对话", model ? model : "gpt-4o", provider ? provider : "openai");
    sctx->session_id = new_id;
    strncpy(sctx->title, "新对话", sizeof(sctx->title) - 1);
    if (provider) {
        strncpy(sctx->provider, provider, sizeof(sctx->provider) - 1);
    }
    if (model) {
        strncpy(sctx->model, model, sizeof(sctx->model) - 1);
    }
    sctx->messages = csilk_json_array();
    return sctx;
}

int
ai_session_append_message(csilk_db_pool_t* pool,
                          int64_t          session_id,
                          const char*      role,
                          const char*      content,
                          const char*      model)
{
    if (!pool || session_id <= 0 || !role) {
        return -1;
    }
    int64_t msg_id = ai_message_insert(pool, session_id, role, content ? content : "", model);
    return msg_id > 0 ? 0 : -1;
}

void
ai_session_context_free(ai_session_context_t* sctx)
{
    if (!sctx) {
        return;
    }
    if (sctx->messages) {
        csilk_json_free(sctx->messages);
    }
    free(sctx);
}
