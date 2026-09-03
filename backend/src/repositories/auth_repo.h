#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

/**
 * @file auth_repo.h
 * @brief 用户认证、密码管理与 TOTP 2FA 数据访问层接口
 *
 * 负责用户表 (users) 的基本 CRUD、密码哈希存储、JWT Token 版本控制 (使历史 Token 失效)、
 * 以及 TOTP 双因子认证 (密钥、开关状态、备用恢复码) 的数据持久化交互。
 */

/**
 * @brief 根据用户名查询完整用户信息（含密码哈希与 2FA 凭据）
 *
 * 用于登录验证、防重注册检查以及 TOTP 二次验证凭据读取。
 *
 * @param pool 数据库连接池指针
 * @param username 用户名
 * @return csilk_json_t* 包含用户完整记录的 JSON 数组（若不存在返回空数组或 NULL）
 */
csilk_json_t* user_find_by_username(csilk_db_pool_t* pool, const char* username);

/**
 * @brief 创建新用户记录
 *
 * 插入用户名与已加密的密码散列，返回新生成的用户 ID。
 *
 * @param pool 数据库连接池指针
 * @param username 用户名
 * @param password_hash 经 PBKDF2/Argon2 等算法加盐后的密码哈希
 * @return int64_t 成功返回新用户的主键 ID，失败返回 0
 */
int64_t user_insert(csilk_db_pool_t* pool, const char* username, const char* password_hash);

/**
 * @brief 根据用户 ID 获取用户信息（脱敏不含明文/哈希密码）
 *
 * 查询当前登录态的基础属性、Token 版本号与 2FA 配置状态。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return csilk_json_t* 包含 id, username, token_version, totp_enabled 等字段的 JSON 数组
 */
csilk_json_t* user_get_by_id(csilk_db_pool_t* pool, int64_t user_id);

/**
 * @brief 更新指定用户的密码哈希
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param password_hash 新的密码散列字符串
 * @return int 成功更新返回 1，失败返回 0
 */
int user_update_password(csilk_db_pool_t* pool, int64_t user_id, const char* password_hash);

/**
 * @brief 递增用户的 Token 版本号 (token_version = token_version + 1)
 *
 * 用于密码修改、主动注销全部设备或权限变更时，使之前签发的所有 JWT 立即失效。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return int 成功更新返回 1，失败返回 0
 */
int user_update_token_version(csilk_db_pool_t* pool, int64_t user_id);

/**
 * @brief 设置/暂存用户的 TOTP Base32 密钥
 *
 * 在 2FA 绑定流程中预存密钥，待验证动态码成功后再正式启用。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param secret Base32 编码的 TOTP 密钥
 * @return int 成功返回 0
 */
int user_set_totp_secret(csilk_db_pool_t* pool, int64_t user_id, const char* secret);

/**
 * @brief 正式启用用户的 TOTP 双因子认证并保存备用码
 *
 * 更新 `totp_enabled = TRUE` 并持久化备份恢复码 JSON。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param backup_codes_json 备用恢复码的 JSON 数组格式字符串
 * @return int 成功返回 0
 */
int user_enable_totp(csilk_db_pool_t* pool, int64_t user_id, const char* backup_codes_json);

/**
 * @brief 停用并清除指定用户的 TOTP 双因子认证
 *
 * 清空 totp_secret, 将 totp_enabled 设为 FALSE, 清空 totp_backup_codes。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return int 成功返回 0
 */
int user_disable_totp(csilk_db_pool_t* pool, int64_t user_id);

/**
 * @brief 更新用户的 TOTP 备用恢复码列表
 *
 * 当用户使用某一个备用码完成登录后，从列表中核销该码并保存剩余的码。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param backup_codes_json 更新后的备用码 JSON 字符串
 * @return int 成功返回 0
 */
int user_update_backup_codes(csilk_db_pool_t* pool, int64_t user_id, const char* backup_codes_json);

/**
 * @brief 查询系统当前注册的用户总数
 *
 * @param pool 数据库连接池指针
 * @return int 数据库中的总用户数量
 */
int user_count(csilk_db_pool_t* pool);

/**
 * @brief 判断系统是否已完成初始化注册
 *
 * 用于首屏向导或安装检测，若用户总数大于 0 则表示系统已初始化。
 *
 * @param pool 数据库连接池指针
 * @return int 已初始化返回 1，未初始化（首个用户未注册）返回 0
 */
int system_is_initialized(csilk_db_pool_t* pool);

/**
 * @brief 根据 OAuth 提供商及唯一 ID 查找关联用户
 */
csilk_json_t* user_find_by_oauth(csilk_db_pool_t* pool, const char* provider, const char* oauth_id);

/**
 * @brief 创建 OAuth 关联用户
 */
int64_t user_create_oauth(csilk_db_pool_t* pool,
                          const char*      username,
                          const char*      provider,
                          const char*      oauth_id);
