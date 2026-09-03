#pragma once

#include <stdint.h>

typedef struct register_user_cmd {
    const char* username;
    const char* password_enc;
    const char* plain_password;
} register_user_cmd_t;

typedef struct login_user_cmd {
    const char* username;
    const char* password_enc;
    const char* plain_password;
} login_user_cmd_t;

typedef struct change_password_cmd {
    int64_t     user_id;
    const char* old_password_enc;
    const char* new_password_enc;
} change_password_cmd_t;

typedef struct enable_2fa_cmd {
    int64_t     user_id;
    const char* code;
} enable_2fa_cmd_t;

typedef struct verify_2fa_login_cmd {
    const char* temp_token;
    const char* code;
} verify_2fa_login_cmd_t;

typedef struct oauth_callback_cmd {
    const char* provider;
    const char* code;
    const char* oauth_id;
    const char* username;
} oauth_callback_cmd_t;
