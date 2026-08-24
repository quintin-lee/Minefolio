#include "common/ai_trace.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void ai_trace_init(ai_trace_t* t, int64_t user_id, int64_t session_id) {
    memset(t, 0, sizeof(*t));
    t->user_id = user_id;
    t->session_id = session_id;
    t->input_messages = strdup("[]");
    t->output_content = strdup("");
    t->system_prompt = strdup("");
    t->error_message = strdup("");
    t->metadata = strdup("{}");
    strncpy(t->status, "ok", sizeof(t->status) - 1);
    clock_gettime(CLOCK_MONOTONIC, &t->t_start);
}

void ai_trace_set_provider(ai_trace_t* t, const char* provider, const char* model) {
    strncpy(t->provider, provider ?: "", sizeof(t->provider) - 1);
    strncpy(t->model, model ?: "", sizeof(t->model) - 1);
}

void ai_trace_set_params(ai_trace_t* t, double temperature, int max_tokens, double top_p) {
    t->temperature = temperature;
    t->max_tokens = max_tokens;
    t->top_p = top_p;
}

void ai_trace_set_system_prompt(ai_trace_t* t, const char* prompt) {
    free(t->system_prompt);
    t->system_prompt = strdup(prompt ?: "");
}

void ai_trace_serialize_messages(ai_trace_t* t, csilk_json_t* messages_array) {
    free(t->input_messages);
    size_t slen = 0;
    char* s = csilk_json_serialize(messages_array, &slen);
    t->input_messages = s ? s : strdup("[]");
}

void ai_trace_append_output(ai_trace_t* t, const char* chunk) {
    if (!chunk || !chunk[0]) return;
    size_t clen = strlen(chunk);
    size_t olen = strlen(t->output_content);
    size_t nlen = olen + clen + 1;
    char* buf = (char*)realloc(t->output_content, nlen);
    if (!buf) return;
    memcpy(buf + olen, chunk, clen);
    buf[olen + clen] = '\0';
    t->output_content = buf;
    t->accumulated_len = (long)nlen;
}

void ai_trace_record_first_token(ai_trace_t* t) {
    if (!t->has_first_token) {
        clock_gettime(CLOCK_MONOTONIC, &t->t_first_token);
        t->has_first_token = 1;
        t->first_token_ms = (t->t_first_token.tv_sec - t->t_start.tv_sec) * 1000
                          + (t->t_first_token.tv_nsec - t->t_start.tv_nsec) / 1000000;
    }
}

void ai_trace_finish(ai_trace_t* t, const char* status, const char* error) {
    clock_gettime(CLOCK_MONOTONIC, &t->t_end);
    strncpy(t->status, status ?: "ok", sizeof(t->status) - 1);
    if (error && error[0]) {
        free(t->error_message);
        t->error_message = strdup(error);
    }
    t->latency_ms = (t->t_end.tv_sec - t->t_start.tv_sec) * 1000
                  + (t->t_end.tv_nsec - t->t_start.tv_nsec) / 1000000;
    if (t->latency_ms < 0) t->latency_ms = 0;
    if (t->total_tokens > 0 && t->latency_ms > 0) {
        t->tokens_per_sec = (double)t->total_tokens / ((double)t->latency_ms / 1000.0);
    }
}

static void safe_append(char** dest, const char* src, size_t* len, size_t* cap) {
    size_t slen = strlen(src);
    if (*len + slen + 1 > *cap) {
        size_t ncap = (*len + slen + 1) * 2;
        if (ncap < 256) ncap = 256;
        char* nb = (char*)realloc(*dest, ncap);
        if (!nb) return;
        *dest = nb;
        *cap = ncap;
    }
    memcpy(*dest + *len, src, slen);
    *len += slen;
    (*dest)[*len] = '\0';
}

static char* sql_escape(const char* s) {
    if (!s) return strdup("");
    size_t slen = strlen(s);
    size_t cap = slen * 2 + 1;
    char* out = (char*)malloc(cap);
    if (!out) return strdup("");
    size_t olen = 0;
    size_t ocap = cap;
    const char* p = s;
    while (*p) {
        if (*p == '\'') {
            safe_append(&out, "''", &olen, &ocap);
        } else if (*p == '\\') {
            safe_append(&out, "\\\\", &olen, &ocap);
        } else {
            char tmp[2] = {*p, '\0'};
            safe_append(&out, tmp, &olen, &ocap);
        }
        p++;
    }
    return out;
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

    char* esc_input = sql_escape(t->input_messages);
    char* esc_output = sql_escape(t->output_content);
    char* esc_sys = sql_escape(t->system_prompt);
    char* esc_err = sql_escape(t->error_message);
    char* esc_meta = sql_escape(t->metadata);

    char sql[4096];
    snprintf(sql, sizeof(sql),
        "INSERT INTO ai_traces "
        "(user_id, session_id, provider, model, input_messages, output_content, "
        "system_prompt, prompt_tokens, completion_tokens, total_tokens, "
        "latency_ms, first_token_ms, tokens_per_sec, cost_usd, "
        "temperature, max_tokens, top_p, status, error_message, metadata) "
        "VALUES (%s, %s, '%s', '%s', '%s', '%s', "
        "'%s', %s, %s, %s, "
        "%s, %s, %s, %s, "
        "%s, %s, %s, '%s', '%s', '%s') RETURNING id",
        uid, sid, t->provider, t->model, esc_input, esc_output,
        esc_sys, pt, ct, tt,
        lat, ftt, tps, cost,
        temp, mtt, tp, t->status, esc_err, esc_meta);

    free(esc_input);
    free(esc_output);
    free(esc_sys);
    free(esc_err);
    free(esc_meta);

    csilk_json_t* r = csilk_db_query_param_json(pool, sql, (const char*[]){NULL});
    int64_t id = 0;
    if (r && csilk_json_array_size(r) > 0)
        id = db_get_int(csilk_json_array_get(r, 0), "id");
    csilk_json_free(r);
    return id;
}

void ai_trace_free(ai_trace_t* t) {
    if (!t) return;
    free(t->input_messages);
    free(t->output_content);
    free(t->system_prompt);
    free(t->error_message);
    free(t->metadata);
    memset(t, 0, sizeof(*t));
}
