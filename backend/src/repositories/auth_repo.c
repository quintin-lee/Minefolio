/**
 * @file auth_repo.c
 * @brief 用户认证、密码管理与 TOTP 2FA 数据访问层具体实现
 *
 * 实现了用户账户检索、注册插入、密码变更、Token 版本升级使登录失效、
 * 以及 TOTP 2FA 密钥与备用恢复码管理等核心 SQL 操作。
 */

#include "repositories/auth_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief 根据用户名查询用户完整信息（含密码哈希）
 *
 * 执行 SQL：
 * `SELECT id, username, password, token_version, totp_secret, totp_enabled, totp_backup_codes, created_at FROM users WHERE username = ?`
 *
 * @param pool 数据库连接池指针
 * @param username 待查询的用户名
 * @return csilk_json_t* 包含用户信息的 JSON 数组
 */
csilk_json_t*
user_find_by_username(csilk_db_pool_t* pool, const char* username)
{
    return csilk_db_query_param_json(
        pool,
        "SELECT id, username, password, token_version, totp_secret, totp_enabled, "
        "totp_backup_codes, created_at FROM users WHERE username = ?",
        (const char*[]){username, NULL});
}

/**
 * @brief 插入新注册用户记录
 *
 * 执行 SQL：`INSERT INTO users (username, password) VALUES (?, ?) RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param username 用户名
 * @param password_hash 密码哈希字符串
 * @return int64_t 成功生成的主键 ID，失败返回 0
 */
int64_t
user_insert(csilk_db_pool_t* pool, const char* username, const char* password_hash)
{
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO users (username, password) VALUES (?, ?) RETURNING id",
        (const char*[]){username, password_hash, NULL});
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return id;
}

/**
 * @brief 根据用户 ID 获取脱敏的用户基本信息
 *
 * 执行 SQL：
 * `SELECT id, username, token_version, totp_secret, totp_enabled, totp_backup_codes, created_at FROM users WHERE id = ?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return csilk_json_t* 包含用户信息的 JSON 数组
 */
csilk_json_t*
user_get_by_id(csilk_db_pool_t* pool, int64_t user_id)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    return csilk_db_query_param_json(
        pool,
        "SELECT id, username, token_version, totp_secret, totp_enabled, totp_backup_codes, "
        "created_at FROM users WHERE id = ?",
        (const char*[]){uid, NULL});
}

/**
 * @brief 更新指定用户的密码散列
 *
 * 执行 SQL：`UPDATE users SET password = ? WHERE id = ?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param password_hash 新密码哈希
 * @return int 更新成功返回 1，失败返回 0
 */
int
user_update_password(csilk_db_pool_t* pool, int64_t user_id, const char* password_hash)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res = csilk_db_query_param_json(pool,
                                                  "UPDATE users SET password = ? WHERE id = ?",
                                                  (const char*[]){password_hash, uid, NULL});
    int           ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

/**
 * @brief 递增用户的 Token 版本号以使所有已下发的 JWT 失效
 *
 * 执行 SQL：`UPDATE users SET token_version = token_version + 1 WHERE id = ?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return int 更新成功返回 1，失败返回 0
 */
int
user_update_token_version(csilk_db_pool_t* pool, int64_t user_id)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "UPDATE users SET token_version = token_version + 1 WHERE id = ?",
                                  (const char*[]){uid, NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

/**
 * @brief 绑定/设置用户的 TOTP 密钥
 *
 * 执行 SQL：`UPDATE users SET totp_secret = ? WHERE id = ?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param secret Base32 编码的密钥字符串
 * @return int 成功返回 0
 */
int
user_set_totp_secret(csilk_db_pool_t* pool, int64_t user_id, const char* secret)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool, "UPDATE users SET totp_secret = ? WHERE id = ?", (const char*[]){secret, uid, NULL});
    if (res) {
        csilk_json_free(res);
    }
    return 0;
}

/**
 * @brief 启用 TOTP 并保存备用恢复码
 *
 * 执行 SQL：`UPDATE users SET totp_enabled = TRUE, totp_backup_codes = ? WHERE id = ?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param backup_codes_json 备用恢复码 JSON 字符串
 * @return int 成功返回 0
 */
int
user_enable_totp(csilk_db_pool_t* pool, int64_t user_id, const char* backup_codes_json)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "UPDATE users SET totp_enabled = TRUE, totp_backup_codes = ? WHERE id = ?",
        (const char*[]){backup_codes_json, uid, NULL});
    if (res) {
        csilk_json_free(res);
    }
    return 0;
}

/**
 * @brief 停用 TOTP 并清空相关密钥与恢复码
 *
 * 执行 SQL：`UPDATE users SET totp_secret = '', totp_enabled = FALSE, totp_backup_codes = '' WHERE id = ?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return int 成功返回 0
 */
int
user_disable_totp(csilk_db_pool_t* pool, int64_t user_id)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res = csilk_db_query_param_json(pool,
                                                  "UPDATE users SET totp_secret = '', totp_enabled "
                                                  "= FALSE, totp_backup_codes = '' WHERE id = ?",
                                                  (const char*[]){uid, NULL});
    if (res) {
        csilk_json_free(res);
    }
    return 0;
}

/**
 * @brief 更新用户的 TOTP 备用码列表
 *
 * 执行 SQL：`UPDATE users SET totp_backup_codes = ? WHERE id = ?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param backup_codes_json 更新后的备用码 JSON 字符串
 * @return int 成功返回 0
 */
int
user_update_backup_codes(csilk_db_pool_t* pool, int64_t user_id, const char* backup_codes_json)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "UPDATE users SET totp_backup_codes = ? WHERE id = ?",
                                  (const char*[]){backup_codes_json, uid, NULL});
    if (res) {
        csilk_json_free(res);
    }
    return 0;
}

/**
 * @brief 统计系统内的注册用户总数
 *
 * 执行 SQL：`SELECT COUNT(*) as count FROM users`
 *
 * @param pool 数据库连接池指针
 * @return int 注册用户数量
 */
int
user_count(csilk_db_pool_t* pool)
{
    csilk_json_t* res = csilk_db_query_param_json(
        pool, "SELECT COUNT(*) as count FROM users", (const char*[]){NULL});
    int count = 0;
    if (res && csilk_json_array_size(res) > 0) {
        count = (int)db_get_int(csilk_json_array_get(res, 0), "count");
    }
    if (res) {
        csilk_json_free(res);
    }
    return count;
}

/**
 * @brief 检查系统是否已初始化
 *
 * 通过 `user_count > 0` 判断系统是否已经创建过初始管理员或用户。
 *
 * @param pool 数据库连接池指针
 * @return int 已初始化返回 1，未初始化返回 0
 */
int
system_is_initialized(csilk_db_pool_t* pool)
{
    return user_count(pool) > 0;
}

csilk_json_t*
user_find_by_oauth(csilk_db_pool_t* pool, const char* provider, const char* oauth_id)
{
    return csilk_db_query_param_json(
        pool,
        "SELECT id, username, token_version FROM users WHERE oauth_provider = ? AND oauth_id = ?",
        (const char*[]){provider, oauth_id, NULL});
}

int64_t
user_create_oauth(csilk_db_pool_t* pool,
                  const char*      username,
                  const char*      provider,
                  const char*      oauth_id)
{
    csilk_json_t* ins_res = csilk_db_query_param_json(
        pool,
        "INSERT INTO users (username, password, token_version, oauth_provider, oauth_id) "
        "VALUES (?, '', 0, ?, ?) RETURNING id",
        (const char*[]){username, provider, oauth_id, NULL});
    int64_t user_id = 0;
    if (ins_res && csilk_json_array_size(ins_res) > 0) {
        user_id = db_get_int(csilk_json_array_get(ins_res, 0), "id");
    }
    if (ins_res) {
        csilk_json_free(ins_res);
    }
    return user_id;
}
