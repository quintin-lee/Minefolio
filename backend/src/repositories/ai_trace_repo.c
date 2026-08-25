#include "repositories/ai_trace_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

static void uid_str(int64_t uid, char out[static 32]) {
    snprintf(out, 32, "%lld", (long long)uid);
}

csilk_json_t* ai_trace_list(csilk_db_pool_t* pool, int64_t user_id, int64_t page,
                             int64_t page_size, const char* provider, const char* model,
                             int64_t* total) {
    char uid[32], lim[32], off[32];
    uid_str(user_id, uid);
    snprintf(lim, 32, "%lld", (long long)page_size);
    snprintf(off, 32, "%lld", (long long)((page - 1) * page_size));

    char where[256] = "WHERE user_id=?";
    const char* params[8];
    int pc = 0;
    params[pc++] = uid;

    if (provider && provider[0]) {
        strncat(where, " AND provider=?", sizeof(where) - strlen(where) - 1);
        params[pc++] = provider;
    }
    if (model && model[0]) {
        strncat(where, " AND model=?", sizeof(where) - strlen(where) - 1);
        params[pc++] = model;
    }

    char cnt_sql[512];
    snprintf(cnt_sql, sizeof(cnt_sql), "SELECT COUNT(*) as cnt FROM ai_traces %s", where);
    params[pc] = NULL;
    csilk_json_t* cnt = csilk_db_query_param_json(pool, cnt_sql, params);
    *total = 0;
    if (cnt && csilk_json_array_size(cnt) > 0)
        *total = db_get_int(csilk_json_array_get(cnt, 0), "cnt");
    csilk_json_free(cnt);

    char sql[1024];
    snprintf(sql, sizeof(sql),
        "SELECT id, user_id, session_id, provider, model, "
        "prompt_tokens, completion_tokens, total_tokens, "
        "latency_ms, first_token_ms, tokens_per_sec, cost_usd, "
        "temperature, max_tokens, top_p, status, error_message, "
        "datetime(created_at) as created_at "
        "FROM ai_traces %s ORDER BY created_at DESC LIMIT ? OFFSET ?",
        where);

    params[pc++] = lim;
    params[pc++] = off;
    params[pc] = NULL;

    return csilk_db_query_param_json(pool, sql, params);
}

csilk_json_t* ai_trace_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id) {
    char uid[32], id_s[32];
    uid_str(user_id, uid);
    snprintf(id_s, 32, "%lld", (long long)id);

    csilk_json_t* r = csilk_db_query_param_json(pool,
        "SELECT id, user_id, session_id, provider, model, "
        "input_messages, output_content, system_prompt, "
        "prompt_tokens, completion_tokens, total_tokens, "
        "latency_ms, first_token_ms, tokens_per_sec, cost_usd, "
        "temperature, max_tokens, top_p, status, error_message, metadata, "
        "datetime(created_at) as created_at "
        "FROM ai_traces WHERE id=? AND user_id=?",
        (const char*[]){id_s, uid, NULL});
    if (!r || csilk_json_array_size(r) == 0) { csilk_json_free(r); return NULL; }
    return r;
}

csilk_json_t* ai_trace_stats(csilk_db_pool_t* pool, int64_t user_id) {
    char uid[32];
    uid_str(user_id, uid);

    csilk_json_t* r = csilk_db_query_param_json(pool,
        "SELECT COUNT(*) as total_traces, "
        "COALESCE(SUM(total_tokens), 0) as total_tokens, "
        "COALESCE(AVG(latency_ms), 0) as avg_latency_ms, "
        "COALESCE(AVG(first_token_ms), 0) as avg_first_token_ms, "
        "COALESCE(AVG(tokens_per_sec), 0) as avg_tokens_per_sec, "
        "COALESCE(SUM(cost_usd), 0) as total_cost_usd "
        "FROM ai_traces WHERE user_id=?",
        (const char*[]){uid, NULL});
    return r;
}

int64_t ai_trace_save(csilk_db_pool_t* pool, ai_trace_t* t) {
    char uid[32], sid[32], pt[32], ct[32], tt[32];
    char lat[32], ftt[32], tps[64], cost[64];
    char temp[32], mtt[32], tp[32];

    snprintf(uid, sizeof(uid), "%lld", (long long)t->user_id);
    snprintf(sid, sizeof(sid), "%lld", (long long)t->session_id);
    snprintf(pt, sizeof(pt), "%d", t->prompt_tokens);
    snprintf(ct, sizeof(ct), "%d", t->completion_tokens);
    snprintf(tt, sizeof(tt), "%d", t->total_tokens);
    snprintf(lat, sizeof(lat), "%ld", t->latency_ms);
    snprintf(ftt, sizeof(ftt), "%ld", t->first_token_ms);
    snprintf(tps, sizeof(tps), "%.2f", t->tokens_per_sec);
    snprintf(cost, sizeof(cost), "%.6f", t->cost_usd);
    snprintf(temp, sizeof(temp), "%.2f", t->temperature);
    snprintf(mtt, sizeof(mtt), "%d", t->max_tokens);
    snprintf(tp, sizeof(tp), "%.2f", t->top_p);

    const char* sql =
        "INSERT INTO ai_traces "
        "(user_id, session_id, provider, model, input_messages, output_content, "
        "system_prompt, prompt_tokens, completion_tokens, total_tokens, "
        "latency_ms, first_token_ms, tokens_per_sec, cost_usd, "
        "temperature, max_tokens, top_p, status, error_message, metadata) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "RETURNING id";

    const char* params[] = {
        uid, sid, t->provider, t->model, t->input_messages, t->output_content,
        t->system_prompt, pt, ct, tt,
        lat, ftt, tps, cost,
        temp, mtt, tp, t->status, t->error_message, t->metadata,
        NULL
    };

    csilk_json_t* r = csilk_db_query_param_json(pool, sql, params);
    int64_t id = 0;
    if (r && csilk_json_array_size(r) > 0)
        id = db_get_int(csilk_json_array_get(r, 0), "id");
    csilk_json_free(r);
    return id;
}
