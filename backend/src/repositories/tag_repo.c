/**
 * @file tag_repo.c
 * @brief 自定义业务标签数据访问层具体实现
 *
 * 实现了标签的增删改查、排序、模糊前缀搜索建议以及参数化 SQL 执行逻辑。
 */

#include "repositories/tag_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief 获取用户的所有标签
 *
 * 执行 SQL：`SELECT id, name, color, created_at FROM tags WHERE user_id=? ORDER BY name`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return csilk_json_t* 标签列表 JSON 数组
 */
csilk_json_t*
tag_list(csilk_db_pool_t* pool, int64_t user_id)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    return csilk_db_query_param_json(
        pool,
        "SELECT id, name, color, created_at FROM tags WHERE user_id=? ORDER BY name",
        (const char*[]){uid, NULL});
}

/**
 * @brief 根据前缀检索标签自动补全建议
 *
 * 当 prefix 为空时查询前 20 条；非空时以 `LIKE %prefix%` 模糊匹配最多 10 条。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param prefix 搜索前缀字符串
 * @return csilk_json_t* 建议列表 JSON 数组
 */
csilk_json_t*
tag_suggestions(csilk_db_pool_t* pool, int64_t user_id, const char* prefix)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    if (!prefix || prefix[0] == '\0') {
        return csilk_db_query_param_json(
            pool,
            "SELECT id, name, color FROM tags WHERE user_id=? LIMIT 20",
            (const char*[]){uid, NULL});
    }
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "%%%s%%", prefix);
    return csilk_db_query_param_json(
        pool,
        "SELECT id, name, color FROM tags WHERE user_id=? AND name LIKE ? LIMIT 10",
        (const char*[]){uid, pattern, NULL});
}

/**
 * @brief 创建新标签
 *
 * 执行 SQL：`INSERT INTO tags (user_id, name, color) VALUES (?, ?, ?) RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param name 标签名称
 * @param color 标签颜色（默认 "#666666"）
 * @return int64_t 成功生成的主键 ID，失败返回 0
 */
int64_t
tag_insert(csilk_db_pool_t* pool, int64_t user_id, const char* name, const char* color)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    const char*   col = color && color[0] ? color : "#666666";
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO tags (user_id, name, color) VALUES (?, ?, ?) RETURNING id",
        (const char*[]){uid, name, col, NULL});
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
 * @brief 更新标签名称与颜色
 *
 * 执行 SQL：`UPDATE tags SET name=?, color=? WHERE id=? AND user_id=?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 标签 ID
 * @param name 新名称
 * @param color 新颜色
 * @return int 成功返回 1，失败返回 0
 */
int
tag_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* name, const char* color)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "UPDATE tags SET name=?, color=? WHERE id=? AND user_id=?",
        (const char*[]){name ? name : "", color ? color : "", idstr, uid, NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

/**
 * @brief 删除指定标签
 *
 * 执行 SQL：`DELETE FROM tags WHERE id=? AND user_id=?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 标签 ID
 * @return int 成功删除返回 1，失败返回 0
 */
int
tag_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool, "DELETE FROM tags WHERE id=? AND user_id=?", (const char*[]){idstr, uid, NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}
