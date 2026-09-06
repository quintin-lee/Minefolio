#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 用户实体模型 (Domain User Entity)
 * @note 严格禁止依赖任何外部 DB、传输协议或 JSON 框架
 */
typedef struct mf_user {
    int64_t id;
    char    username[64];
    char    password_hash[256];
    int64_t token_version;
    bool    totp_enabled;
    char    totp_secret[64];
    char    totp_backup_codes[1024];
    char    created_at[64];
    char    updated_at[64];
} mf_user_t;

/**
 * @brief TOTP 双因子配置模型
 */
typedef struct mf_totp_config {
    char secret[64];
    char otpauth_url[256];
    char backup_codes[16][16];
    int  backup_code_count;
} mf_totp_config_t;

/**
 * @brief OAuth 单点登录第三方用户信息
 */
typedef struct mf_oauth_user {
    char provider[32];
    char oauth_id[128];
    char username[64];
    char email[128];
} mf_oauth_user_t;
