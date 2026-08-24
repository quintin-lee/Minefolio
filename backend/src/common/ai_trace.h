#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
#include <stdint.h>
#include <time.h>

typedef struct {
    int64_t id;
    int64_t user_id;
    int64_t session_id;
    char provider[64];
    char model[128];
    char* input_messages;
    char* output_content;
    char* system_prompt;
    int prompt_tokens;
    int completion_tokens;
    int total_tokens;
    long latency_ms;
    long first_token_ms;
    double tokens_per_sec;
    double cost_usd;
    double temperature;
    int max_tokens;
    double top_p;
    char status[32];
    char* error_message;
    char* metadata;
    struct timespec t_start;
    struct timespec t_first_token;
    struct timespec t_end;
    int has_first_token;
    long accumulated_len;
} ai_trace_t;

void ai_trace_init(ai_trace_t* t, int64_t user_id, int64_t session_id);
void ai_trace_set_provider(ai_trace_t* t, const char* provider, const char* model);
void ai_trace_set_params(ai_trace_t* t, double temperature, int max_tokens, double top_p);
void ai_trace_set_system_prompt(ai_trace_t* t, const char* prompt);
void ai_trace_serialize_messages(ai_trace_t* t, csilk_json_t* messages_array);
void ai_trace_append_output(ai_trace_t* t, const char* chunk);
void ai_trace_record_first_token(ai_trace_t* t);
void ai_trace_finish(ai_trace_t* t, const char* status, const char* error);
int64_t ai_trace_save(csilk_db_pool_t* pool, ai_trace_t* t);
void ai_trace_free(ai_trace_t* t);
