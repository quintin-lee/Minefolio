#include "common/ai_trace.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void
ai_trace_init(ai_trace_t* t, int64_t user_id, int64_t session_id)
{
    memset(t, 0, sizeof(*t));
    t->user_id = user_id;
    t->session_id = session_id;
    t->input_messages = strdup("[]");
    t->output_content = strdup("");
    t->system_prompt = strdup("");
    t->error_message = strdup("");
    t->metadata = strdup("{}");
    strncpy(t->tool_spans, "[]", sizeof(t->tool_spans) - 1);
    t->tool_spans[sizeof(t->tool_spans) - 1] = '\0';
    strncpy(t->status, "ok", sizeof(t->status) - 1);
    clock_gettime(CLOCK_MONOTONIC, &t->t_start);
}

void
ai_trace_add_tool_span(ai_trace_t* t, const char* name, long latency_ms, size_t bytes, int ok)
{
    if (!t || !name) {
        return;
    }
    /* Cap the array to avoid overflow of the fixed buffer. */
    size_t cur = strlen(t->tool_spans);
    if (cur + 256 >= sizeof(t->tool_spans)) {
        return;
    }
    char entry[256];
    int  n = snprintf(entry,
                      sizeof(entry),
                      "{\"name\":\"%s\",\"latency_ms\":%ld,\"bytes\":%zu,\"ok\":%d}",
                      name,
                      latency_ms,
                      bytes,
                      ok ? 1 : 0);
    if (n < 0) {
        return;
    }
    const char* sep = (cur <= 2) ? "" : ","; /* skip comma for first element */
    /* Replace trailing ']' with ',entry]' */
    size_t pos = cur > 0 ? cur - 1 : 0;
    if (t->tool_spans[pos] == ']') {
        t->tool_spans[pos] = '\0';
    }
    int written = snprintf(t->tool_spans + strlen(t->tool_spans),
                           sizeof(t->tool_spans) - strlen(t->tool_spans),
                           "%s%s]",
                           sep,
                           entry);
    (void)written;
}

void
ai_trace_set_provider(ai_trace_t* t, const char* provider, const char* model)
{
    strncpy(t->provider, provider ?: "", sizeof(t->provider) - 1);
    strncpy(t->model, model ?: "", sizeof(t->model) - 1);
}

void
ai_trace_set_params(ai_trace_t* t, double temperature, int max_tokens, double top_p)
{
    t->temperature = temperature;
    t->max_tokens = max_tokens;
    t->top_p = top_p;
}

void
ai_trace_set_system_prompt(ai_trace_t* t, const char* prompt)
{
    free(t->system_prompt);
    t->system_prompt = strdup(prompt ?: "");
}

void
ai_trace_serialize_messages(ai_trace_t* t, csilk_json_t* messages_array)
{
    free(t->input_messages);
    size_t slen = 0;
    char*  s = csilk_json_serialize(messages_array, &slen);
    t->input_messages = s ? s : strdup("[]");
}

void
ai_trace_append_output(ai_trace_t* t, const char* chunk)
{
    if (!chunk || !chunk[0]) {
        return;
    }
    size_t clen = strlen(chunk);
    size_t olen = strlen(t->output_content);
    size_t nlen = olen + clen + 1;
    char*  buf = (char*)realloc(t->output_content, nlen);
    if (!buf) {
        return;
    }
    memcpy(buf + olen, chunk, clen);
    buf[olen + clen] = '\0';
    t->output_content = buf;
    t->accumulated_len = (long)nlen;
}

void
ai_trace_record_first_token(ai_trace_t* t)
{
    if (!t->has_first_token) {
        clock_gettime(CLOCK_MONOTONIC, &t->t_first_token);
        t->has_first_token = 1;
        t->first_token_ms = (t->t_first_token.tv_sec - t->t_start.tv_sec) * 1000 +
                            (t->t_first_token.tv_nsec - t->t_start.tv_nsec) / 1000000;
    }
}

int
ai_estimate_tokens_from_text(const char* text)
{
    if (!text || !*text) {
        return 0;
    }
    size_t char_count = 0;
    size_t ascii_count = 0;
    size_t cjk_count = 0;
    size_t i = 0;

    while (text[i] != '\0') {
        unsigned char c = (unsigned char)text[i];
        if ((c & 0x80) == 0) {
            ascii_count++;
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            cjk_count++;
            i += (text[i + 1] != '\0') ? 2 : 1;
        } else if ((c & 0xF0) == 0xE0) {
            cjk_count++;
            i += (text[i + 1] != '\0' && text[i + 2] != '\0') ? 3 : 1;
        } else if ((c & 0xF8) == 0xF0) {
            cjk_count++;
            i += (text[i + 1] != '\0' && text[i + 2] != '\0' && text[i + 3] != '\0') ? 4 : 1;
        } else {
            ascii_count++;
            i += 1;
        }
        char_count++;
    }

    /* Standard heuristic: ~3.5 ASCII chars per token, ~1.2 tokens per CJK character */
    int estimated = (int)((double)ascii_count / 3.5 + (double)cjk_count * 1.2);
    if (estimated <= 0 && char_count > 0) {
        estimated = 1;
    }
    return estimated;
}

void
ai_trace_calculate_tokens_and_cost(ai_trace_t* t, int prompt_tokens, int completion_tokens)
{
    if (prompt_tokens > 0) {
        t->prompt_tokens = prompt_tokens;
    } else {
        int est_in = ai_estimate_tokens_from_text(t->input_messages);
        int est_sys = ai_estimate_tokens_from_text(t->system_prompt);
        t->prompt_tokens = est_in + est_sys + 8; /* Add slight overhead for message framing */
    }

    if (completion_tokens > 0) {
        t->completion_tokens = completion_tokens;
    } else {
        t->completion_tokens = ai_estimate_tokens_from_text(t->output_content);
    }

    t->total_tokens = t->prompt_tokens + t->completion_tokens;

    /* Pricing estimation (per 1M tokens) */
    double in_price_per_1m = 0.50;
    double out_price_per_1m = 1.50;

    if (strstr(t->model, "gpt-4o-mini") || strstr(t->model, "4o-mini")) {
        in_price_per_1m = 0.15;
        out_price_per_1m = 0.60;
    } else if (strstr(t->model, "gpt-4o") || strstr(t->model, "gpt-4")) {
        in_price_per_1m = 2.50;
        out_price_per_1m = 10.00;
    } else if (strstr(t->provider, "deepseek") || strstr(t->model, "deepseek")) {
        in_price_per_1m = 0.14;
        out_price_per_1m = 0.28;
    } else if (strstr(t->provider, "qwen") || strstr(t->model, "qwen")) {
        in_price_per_1m = 0.20;
        out_price_per_1m = 0.60;
    } else if (strstr(t->provider, "ollama")) {
        in_price_per_1m = 0.0;
        out_price_per_1m = 0.0;
    }

    t->cost_usd = ((double)t->prompt_tokens * in_price_per_1m / 1000000.0) +
                  ((double)t->completion_tokens * out_price_per_1m / 1000000.0);
}

void
ai_trace_finish(ai_trace_t* t, const char* status, const char* error)
{
    clock_gettime(CLOCK_MONOTONIC, &t->t_end);
    strncpy(t->status, status ?: "ok", sizeof(t->status) - 1);
    if (error && error[0]) {
        free(t->error_message);
        t->error_message = strdup(error);
    }
    t->latency_ms = (t->t_end.tv_sec - t->t_start.tv_sec) * 1000 +
                    (t->t_end.tv_nsec - t->t_start.tv_nsec) / 1000000;
    if (t->latency_ms < 0) {
        t->latency_ms = 0;
    }

    /* Calculate tokens per second */
    if (t->completion_tokens > 0) {
        long gen_ms = (t->first_token_ms > 0 && t->latency_ms > t->first_token_ms)
                          ? (t->latency_ms - t->first_token_ms)
                          : t->latency_ms;
        if (gen_ms > 0) {
            t->tokens_per_sec = (double)t->completion_tokens / ((double)gen_ms / 1000.0);
        } else {
            t->tokens_per_sec = (double)t->completion_tokens;
        }
    } else if (t->total_tokens > 0 && t->latency_ms > 0) {
        t->tokens_per_sec = (double)t->total_tokens / ((double)t->latency_ms / 1000.0);
    }
}

void
ai_trace_free(ai_trace_t* t)
{
    if (!t) {
        return;
    }
    free(t->input_messages);
    free(t->output_content);
    free(t->system_prompt);
    free(t->error_message);
    free(t->metadata);
    memset(t, 0, sizeof(*t));
}
