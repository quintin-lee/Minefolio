#pragma once

#include <stdbool.h>
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

typedef struct disable_2fa_cmd {
    int64_t     user_id;
    const char* password;
    const char* password_enc;
    const char* code;
} disable_2fa_cmd_t;

typedef struct verify_2fa_login_cmd {
    const char* temp_token;
    const char* code;
} verify_2fa_login_cmd_t;

typedef struct oauth_callback_cmd {
    const char* provider;
    const char* code;
    const char* username;
} oauth_callback_cmd_t;

typedef struct admin_list_users_cmd {
    int64_t     page;
    int64_t     page_size;
    const char* keyword;
    const char* role;
    const char* status;
} admin_list_users_cmd_t;

typedef struct admin_create_user_cmd {
    const char* username;
    const char* password_enc;
    const char* plain_password;
    const char* role;
} admin_create_user_cmd_t;

typedef struct admin_update_user_cmd {
    int64_t     actor_user_id;
    int64_t     target_user_id;
    const char* role;
    bool        has_disabled;
    bool        disabled;
} admin_update_user_cmd_t;

typedef struct admin_reset_password_cmd {
    int64_t     target_user_id;
    const char* new_password_enc;
    const char* plain_password;
} admin_reset_password_cmd_t;
