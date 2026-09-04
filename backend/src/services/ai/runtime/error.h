#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AI_RUNTIME_ERR_OK = 0,
    AI_RUNTIME_ERR_MODEL = 1001,
    AI_RUNTIME_ERR_TOOL = 1002,
    AI_RUNTIME_ERR_POLICY = 1003,
    AI_RUNTIME_ERR_TIMEOUT = 1004,
    AI_RUNTIME_ERR_CONTEXT_OVERFLOW = 1005,
    AI_RUNTIME_ERR_VALIDATION = 1006,
    AI_RUNTIME_ERR_CANCELLED = 1007,
} ai_runtime_error_t;

typedef struct {
    ai_runtime_error_t code;
    char               message[256];
    char               detail[512];
} ai_runtime_status_t;

const char* ai_runtime_error_name(ai_runtime_error_t code);

void ai_runtime_status_set(ai_runtime_status_t* status,
                           ai_runtime_error_t   code,
                           const char*          message,
                           const char*          detail);

#ifdef __cplusplus
}
#endif
