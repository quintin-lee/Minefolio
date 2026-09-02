/**
 * @file category_repo.c
 * @brief 收支与资产分类数据访问层具体实现
 *
 * 实现了分类树节点的增删改查、排序、父子级联检查及按需查找自动创建逻辑。
 */

#include "repositories/category_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief 查询指定用户的分类列表
 *
 * 支持按分类主类别 (`type`) 进行过滤，查询结果按 `sort_order ASC, name ASC` 升序排列。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param type 分类主类别（如 "expense", "income", "asset"，为 NULL 时不过滤）
 * @return csilk_json_t* 分类记录 JSON 数组
 */
csilk_json_t*
category_list(csilk_db_pool_t* pool, int64_t user_id, const char* type)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    if (type && type[0]) {
        return csilk_db_query_param_json(
            pool,
            "SELECT "
            "c.id,c.name,c.parent_id,c.type,c.asset_type,c.currency,c.icon,c.sort_order,c.created_"
            "at,c.updated_at FROM categories c WHERE c.user_id=? AND c.type=? ORDER BY "
            "c.sort_order,c.name",
            (const char*[]){uid, type, NULL});
    }
    return csilk_db_query_param_json(
        pool,
        "SELECT "
        "c.id,c.name,c.parent_id,c.type,c.asset_type,c.currency,c.icon,c.sort_order,c.created_at,c."
        "updated_at FROM categories c WHERE c.user_id=? ORDER BY c.sort_order,c.name",
        (const char*[]){uid, NULL});
}

/**
 * @brief 获取单个分类详情
 *
 * 执行 SQL：`SELECT id,name,parent_id,type,asset_type,currency,icon,sort_order FROM categories WHERE id=? AND user_id=?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 分类 ID
 * @return csilk_json_t* 包含分类详情的 JSON 数组
 */
csilk_json_t*
category_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    return csilk_db_query_param_json(
        pool,
        "SELECT id,name,parent_id,type,asset_type,currency,icon,sort_order FROM categories WHERE "
        "id=? AND user_id=?",
        (const char*[]){idstr, uid, NULL});
}

/**
 * @brief 插入新分类
 *
 * 执行 SQL：
 * `INSERT INTO categories (user_id,name,parent_id,type,asset_type,currency,icon,sort_order) VALUES (?,?,?,?,?,?,?,?) RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param name 分类名称
 * @param parent_id 父分类 ID（0 表示 NULL/顶级）
 * @param type 主类别
 * @param asset_type 资产细分类型
 * @param currency 预设货币
 * @param icon 图标名称
 * @param sort_order 排序序号
 * @return int64_t 成功返回新分类 ID，失败返回 0
 */
int64_t
category_insert(csilk_db_pool_t* pool,
                int64_t          user_id,
                const char*      name,
                int64_t          parent_id,
                const char*      type,
                const char*      asset_type,
                const char*      currency,
                const char*      icon,
                int              sort_order)
{
    char uid[32], pid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)parent_id);
    char sort_str[16];
    snprintf(sort_str, sizeof(sort_str), "%d", sort_order);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO categories (user_id,name,parent_id,type,asset_type,currency,icon,sort_order) "
        "VALUES (?,?,?,?,?,?,?,?) RETURNING id",
        (const char*[]){uid,
                        name,
                        parent_id > 0 ? pid : "NULL",
                        type ? type : "",
                        asset_type ? asset_type : "",
                        currency ? currency : "",
                        icon ? icon : "",
                        sort_str,
                        NULL});
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
 * @brief 更新分类信息
 *
 * 执行 SQL：
 * `UPDATE categories SET name=?,type=?,asset_type=?,currency=?,icon=?,sort_order=? WHERE id=? AND user_id=?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 分类 ID
 * @param name 分类名称
 * @param type 主类型
 * @param asset_type 资产细分类型
 * @param currency 货币
 * @param icon 图标
 * @param sort_order 排序序号
 * @return int 成功返回 1，失败返回 0
 */
int
category_update(csilk_db_pool_t* pool,
                int64_t          user_id,
                int64_t          id,
                const char*      name,
                const char*      type,
                const char*      asset_type,
                const char*      currency,
                const char*      icon,
                int              sort_order)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    char sort_str[16];
    snprintf(sort_str, sizeof(sort_str), "%d", sort_order);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "UPDATE categories SET name=?,type=?,asset_type=?,currency=?,icon=?,sort_order=? WHERE "
        "id=? AND user_id=?",
        (const char*[]){name ? name : "",
                        type ? type : "",
                        asset_type ? asset_type : "",
                        currency ? currency : "",
                        icon ? icon : "",
                        sort_str,
                        idstr,
                        uid,
                        NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

/**
 * @brief 删除指定分类
 *
 * 执行 SQL：`DELETE FROM categories WHERE id=? AND user_id=?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 分类 ID
 * @return int 成功删除返回 1，失败返回 0
 */
int
category_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool, "DELETE FROM categories WHERE id=? AND user_id=?", (const char*[]){idstr, uid, NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

/**
 * @brief 统计指定分类下的直接子分类数量
 *
 * 执行 SQL：`SELECT COUNT(*) as cnt FROM categories WHERE parent_id=? AND user_id=?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param parent_id 父分类 ID
 * @return csilk_json_t* 包含计数的 JSON 数组
 */
csilk_json_t*
category_children(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_id)
{
    char uid[32], pid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)parent_id);
    return csilk_db_query_param_json(
        pool,
        "SELECT COUNT(*) as cnt FROM categories WHERE parent_id=? AND user_id=?",
        (const char*[]){pid, uid, NULL});
}

/**
 * @brief 查找同名同层级的分类，若不存在则创建
 *
 * 1. 首先尝试匹配 `WHERE user_id=? AND name=? AND parent_id=?`。
 * 2. 若找到直接返回已有分类 ID。
 * 3. 若未找到则调用 `category_insert` 插入新分类并返回生成 ID。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param name 分类名称
 * @param parent_id 父级分类 ID
 * @param type 主类别
 * @param asset_type 资产子类别
 * @param icon 图标标识
 * @param sort_order 排序
 * @return int64_t 分类 ID
 */
int64_t
category_find_or_create(csilk_db_pool_t* pool,
                        int64_t          user_id,
                        const char*      name,
                        int64_t          parent_id,
                        const char*      type,
                        const char*      asset_type,
                        const char*      icon,
                        int              sort_order)
{
    char uid[32], pid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)parent_id);
    csilk_json_t* chk = csilk_db_query_param_json(
        pool,
        "SELECT id FROM categories WHERE user_id=? AND name=? AND parent_id=?",
        (const char*[]){uid, name, parent_id > 0 ? pid : "NULL", NULL});
    if (chk && csilk_json_array_size(chk) > 0) {
        int64_t id = db_get_int(csilk_json_array_get(chk, 0), "id");
        csilk_json_free(chk);
        return id;
    }
    if (chk) {
        csilk_json_free(chk);
    }
    return category_insert(pool,
                           user_id,
                           name,
                           parent_id,
                           type,
                           asset_type,
                           "",
                           icon && icon[0] ? icon : "",
                           sort_order);
}

/**
 * @brief 检查分类是否存在且归属于该用户
 *
 * 执行 SQL：`SELECT id FROM categories WHERE id=? AND user_id=?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 分类 ID
 * @return int 存在返回 1，不存在返回 0
 */
int
category_exists(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "SELECT id FROM categories WHERE id=? AND user_id=?",
                                  (const char*[]){idstr, uid, NULL});
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}
