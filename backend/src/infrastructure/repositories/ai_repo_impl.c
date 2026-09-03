#include "infrastructure/repositories/ai_repo_impl.h"
#include "repositories/ai_session_repo.h"
#include "repositories/ai_trace_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
mf_ai_session_repo_list(void*            pool,
                        int64_t          user_id,
                        int64_t          page,
                        int64_t          page_size,
                        int64_t*         total,
                        mf_ai_session_t* out_list,
                        size_t           max_out)
{
    if (!pool || user_id <= 0) {
        return -1;
    }

    csilk_json_t* res = ai_session_list((csilk_db_pool_t*)pool, user_id, page, page_size, total);
    if (!res) {
        return -1;
    }

    size_t count = csilk_json_array_size(res);
    if (out_list && max_out > 0) {
        size_t n = count < max_out ? count : max_out;
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* row = csilk_json_array_get(res, i);
            out_list[i].id = (int64_t)db_get_int(row, "id");
            out_list[i].user_id = user_id;
            const char* s = csilk_json_get_string(row, "title");
            if (s) {
                snprintf(out_list[i].title, sizeof(out_list[i].title), "%s", s);
            }
            s = csilk_json_get_string(row, "model");
            if (s) {
                snprintf(out_list[i].model, sizeof(out_list[i].model), "%s", s);
            }
            s = csilk_json_get_string(row, "provider");
            if (s) {
                snprintf(out_list[i].provider, sizeof(out_list[i].provider), "%s", s);
            }
            s = csilk_json_get_string(row, "created_at");
            if (s) {
                snprintf(out_list[i].created_at, sizeof(out_list[i].created_at), "%s", s);
            }
            s = csilk_json_get_string(row, "updated_at");
            if (s) {
                snprintf(out_list[i].updated_at, sizeof(out_list[i].updated_at), "%s", s);
            }
        }
    }

    csilk_json_free(res);
    return (int)count;
}

int64_t
mf_ai_session_repo_create(
    void* pool, int64_t user_id, const char* title, const char* model, const char* provider)
{
    if (!pool || user_id <= 0) {
        return 0;
    }
    return ai_session_insert((csilk_db_pool_t*)pool, user_id, title, model, provider);
}

int
mf_ai_session_repo_get(void* pool, int64_t user_id, int64_t id, mf_ai_session_t* out_session)
{
    if (!pool || user_id <= 0 || id <= 0 || !out_session) {
        return -1;
    }
    memset(out_session, 0, sizeof(*out_session));

    csilk_json_t* res = ai_session_get((csilk_db_pool_t*)pool, user_id, id);
    if (!res) {
        return -1;
    }

    out_session->id = (int64_t)db_get_int(res, "id");
    out_session->user_id = user_id;
    const char* s = csilk_json_get_string(res, "title");
    if (s) {
        snprintf(out_session->title, sizeof(out_session->title), "%s", s);
    }
    s = csilk_json_get_string(res, "model");
    if (s) {
        snprintf(out_session->model, sizeof(out_session->model), "%s", s);
    }
    s = csilk_json_get_string(res, "provider");
    if (s) {
        snprintf(out_session->provider, sizeof(out_session->provider), "%s", s);
    }
    s = csilk_json_get_string(res, "created_at");
    if (s) {
        snprintf(out_session->created_at, sizeof(out_session->created_at), "%s", s);
    }
    s = csilk_json_get_string(res, "updated_at");
    if (s) {
        snprintf(out_session->updated_at, sizeof(out_session->updated_at), "%s", s);
    }

    csilk_json_free(res);
    return 0;
}

int
mf_ai_session_repo_update(
    void* pool, int64_t user_id, int64_t id, const char* title, const char* model)
{
    if (!pool || user_id <= 0 || id <= 0) {
        return 0;
    }
    return ai_session_update((csilk_db_pool_t*)pool, user_id, id, title, model);
}

int
mf_ai_session_repo_delete(void* pool, int64_t user_id, int64_t id)
{
    if (!pool || user_id <= 0 || id <= 0) {
        return 0;
    }
    return ai_session_delete((csilk_db_pool_t*)pool, user_id, id);
}

int
mf_ai_trace_repo_stats(void* pool, int64_t user_id, mf_ai_trace_summary_t* out_stats)
{
    if (!pool || user_id <= 0 || !out_stats) {
        return -1;
    }
    memset(out_stats, 0, sizeof(*out_stats));

    csilk_json_t* res = ai_trace_stats((csilk_db_pool_t*)pool, user_id);
    if (!res) {
        return -1;
    }

    out_stats->total_traces = (int64_t)db_get_int(res, "total_traces");
    out_stats->total_prompt_tokens = (int64_t)db_get_int(res, "total_prompt_tokens");
    out_stats->total_completion_tokens = (int64_t)db_get_int(res, "total_completion_tokens");
    out_stats->total_tokens = (int64_t)db_get_int(res, "total_tokens");
    out_stats->avg_latency_ms = db_get_num(res, "avg_latency_ms");

    csilk_json_free(res);
    return 0;
}
