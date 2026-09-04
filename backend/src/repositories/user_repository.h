#pragma once

/**
 * @file user_repository.h
 * @brief 用户数据访问层接口（纯数据映射，严禁业务规则、权限决策、HTTP上下文）
 */

#include "infrastructure/database/database.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t id;
    char    username[64];
    char    password_hash[256];
    char    email[128];
    char    totp_secret[64];
    bool    totp_enabled;
    char    totp_backup_codes[512];
    char    created_at[64];
    char    updated_at[64];
} user_record_t;

int     user_repo_find_by_id(mf_db_t* db, int64_t id, user_record_t* out_user);
int     user_repo_find_by_username(mf_db_t* db, const char* username, user_record_t* out_user);
int64_t user_repo_insert(mf_db_t* db, const char* username, const char* password_hash);
int     user_repo_update_password(mf_db_t* db, int64_t id, const char* new_hash);
int     user_repo_update_totp(
    mf_db_t* db, int64_t id, const char* secret, bool enabled, const char* backup_codes);
int user_repo_count(mf_db_t* db, int64_t* out_count);

#ifdef __cplusplus
}
#endif
