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

void ai_trace_free(ai_trace_t* t) {
    if (!t) return;
    free(t->input_messages);
    free(t->output_content);
    free(t->system_prompt);
    free(t->error_message);
    free(t->metadata);
    memset(t, 0, sizeof(*t));
}
