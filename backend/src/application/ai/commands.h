#pragma once

#include <stdint.h>

typedef struct ai_create_session_cmd {
    int64_t     user_id;
    const char* title;
    const char* model;
    const char* provider;
} ai_create_session_cmd_t;

typedef struct ai_update_session_cmd {
    int64_t     user_id;
    int64_t     id;
    const char* title;
    const char* model;
} ai_update_session_cmd_t;
