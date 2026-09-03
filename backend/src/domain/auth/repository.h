#pragma once

#include <stdint.h>
#include <stddef.h>
#include "domain/auth/entity.h"

/**
 * @brief 认证与用户仓储抽象契约接口 (Domain Auth Repository Contract)
 * @note 纯 C 契约，严格禁止返回 JSON 节点或直接编写 SQL
 */

int mf_auth_repo_find_by_username(void* pool, const char* username, mf_user_t* out_user);

int mf_auth_repo_get_by_id(void* pool, int64_t user_id, mf_user_t* out_user);

int
mf_auth_repo_create(void* pool, const char* username, const char* password_hash, int64_t* out_id);

int mf_auth_repo_update_password(void* pool, int64_t user_id, const char* password_hash);

int mf_auth_repo_update_token_version(void* pool, int64_t user_id);

int mf_auth_repo_set_totp_secret(void* pool, int64_t user_id, const char* secret);

int mf_auth_repo_enable_totp(void* pool, int64_t user_id, const char* backup_codes_json);

int mf_auth_repo_disable_totp(void* pool, int64_t user_id);

int mf_auth_repo_update_backup_codes(void* pool, int64_t user_id, const char* backup_codes_json);

int mf_auth_repo_count(void* pool);

int mf_auth_repo_is_initialized(void* pool);
