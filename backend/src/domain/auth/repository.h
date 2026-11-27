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

int mf_auth_repo_create_with_role(
    void* pool, const char* username, const char* password_hash, const char* role, int64_t* out_id);

int mf_auth_repo_update_password(void* pool, int64_t user_id, const char* password_hash);

int mf_auth_repo_update_token_version(void* pool, int64_t user_id);

int mf_auth_repo_set_totp_secret(void* pool, int64_t user_id, const char* secret);

int mf_auth_repo_enable_totp(void* pool, int64_t user_id, const char* backup_codes_json);

int mf_auth_repo_disable_totp(void* pool, int64_t user_id);

int mf_auth_repo_update_backup_codes(void* pool, int64_t user_id, const char* backup_codes_json);

int mf_auth_repo_cas_backup_codes(void*       pool,
                                  int64_t     user_id,
                                  const char* expected_backup_codes_json,
                                  const char* next_backup_codes_json);

int mf_auth_repo_count(void* pool);

int mf_auth_repo_is_initialized(void* pool);

/**
 * @brief 获取实例管理员用户 ID
 * @note 实例管理员 = 最早注册的账号（MIN(id)）。无用户时返回 0。
 * @return int64_t 管理员 user_id（未初始化返回 0）
 */
int64_t mf_auth_repo_get_instance_admin_id(void* pool);

/**
 * @brief 判断用户是否为实例管理员
 * @note 实例管理员 = 最早注册的账号（/system/setup 创建的首个用户）。AI 全局配置等
 *       实例级共享资源仅允许管理员改写；参数非法或查询失败时保守返回 0（拒绝）。
 * @return int 1 是管理员；0 否
 */
int mf_auth_repo_is_instance_admin(void* pool, int64_t user_id);

int mf_auth_repo_find_by_oauth(void*       pool,
                               const char* provider,
                               const char* oauth_id,
                               mf_user_t*  out_user);

int mf_auth_repo_create_oauth(
    void* pool, const char* username, const char* provider, const char* oauth_id, int64_t* out_id);

/**
 * @brief 分页查询用户列表（供管理员用户管理面板使用）
 * @param disabled_filter -1 全部，0 仅启用，1 仅禁用
 * @note 调用方负责对 *out_items 执行 free()
 */
int mf_auth_repo_list_users(void*               pool,
                            int64_t             page,
                            int64_t             page_size,
                            const char*         keyword,
                            const char*         role_filter,
                            int                 disabled_filter,
                            mf_user_summary_t** out_items,
                            int*                out_count,
                            int64_t*            out_total);

/**
 * @brief 统计系统用户概况（总数 / 活跃 / 管理员 / 已禁用）
 */
int mf_auth_repo_get_user_stats(void* pool, mf_user_stats_t* out_stats);

/**
 * @brief 更新用户角色 ('admin' | 'user')
 */
int mf_auth_repo_update_role(void* pool, int64_t user_id, const char* role);

/**
 * @brief 设置用户启用/禁用状态
 */
int mf_auth_repo_set_disabled(void* pool, int64_t user_id, bool disabled);

/**
 * @brief 统计当前启用中的管理员总数
 */
int mf_auth_repo_count_active_admins(void* pool);

/**
 * @brief 级联删除用户及其全部业务数据（在事务中执行）
 */
int mf_auth_repo_delete_user_cascade(void* pool, int64_t user_id);
