/**
 * @file ledger_repo.c
 * @brief 多账本空间与成员权限 RBAC 数据访问层具体实现
 *
 * 实现了账本空间创建与默认所有者成员自动关联、多表联查成员角色与资产汇总、
 * 严格有序的级联数据删除逻辑、以及基于过期校验的邀请码检索。
 */

#include "repositories/ledger_repo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 查询用户参与的所有账本列表
 *
 * 1. 确保用户拥有至少一个默认账本 (`ledger_get_default`)。
 * 2. 联查 `ledger_members` 与 `users`，通过子查询计算 `member_count` 与 `total_assets`。
 * 3. 按默认账本降序、ID 升序排序 (`ORDER BY l.is_default DESC, l.id ASC`)。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return csilk_json_t* 账本空间列表 JSON 数组
 */
csilk_json_t*
ledger_list_by_user(csilk_db_pool_t* pool, int64_t user_id)
{
    /* 确保用户至少拥有一个默认账本 */
    if (user_id > 0) {
        ledger_get_default(pool, user_id);
    }

    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char* sql =
        "SELECT l.id, l.owner_id, l.name, l.description, l.currency, l.icon, l.color, "
        "       l.is_default, l.invite_code, CAST(l.invite_expires_at AS TEXT) AS "
        "invite_expires_at, "
        "       CAST(l.created_at AS TEXT) AS created_at, CAST(l.updated_at AS TEXT) AS "
        "updated_at, "
        "       m.role AS my_role, "
        "       u.username AS owner_username, "
        "       (SELECT COUNT(*) FROM ledger_members WHERE ledger_id = l.id) AS member_count, "
        "       (SELECT COALESCE(SUM(current_value), 0) FROM assets WHERE ledger_id = l.id) AS "
        "total_assets "
        "FROM ledgers l "
        "JOIN ledger_members m ON m.ledger_id = l.id AND m.user_id = ? "
        "JOIN users u ON u.id = l.owner_id "
        "ORDER BY l.is_default DESC, l.id ASC";

    return csilk_db_query_param_json(pool, sql, (const char*[]){uid, NULL});
}

/**
 * @brief 根据账本 ID 获取账本详情
 *
 * 执行 SQL JOIN 查询：`WHERE l.id = ?`
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @return csilk_json_t* 包含单条账本详情的 JSON 数组（未命中返回 NULL）
 */
csilk_json_t*
ledger_get(csilk_db_pool_t* pool, int64_t ledger_id)
{
    char lid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);

    const char* sql =
        "SELECT l.id, l.owner_id, l.name, l.description, l.currency, l.icon, l.color, "
        "       l.is_default, l.invite_code, CAST(l.invite_expires_at AS TEXT) AS "
        "invite_expires_at, "
        "       CAST(l.created_at AS TEXT) AS created_at, CAST(l.updated_at AS TEXT) AS "
        "updated_at, "
        "       u.username AS owner_username, "
        "       (SELECT COUNT(*) FROM ledger_members WHERE ledger_id = l.id) AS member_count, "
        "       (SELECT COALESCE(SUM(current_value), 0) FROM assets WHERE ledger_id = l.id) AS "
        "total_assets "
        "FROM ledgers l "
        "JOIN users u ON u.id = l.owner_id "
        "WHERE l.id = ?";

    csilk_json_t* arr = csilk_db_query_param_json(pool, sql, (const char*[]){lid, NULL});
    if (!arr || csilk_json_array_size(arr) == 0) {
        if (arr) {
            csilk_json_free(arr);
        }
        return NULL;
    }
    return arr;
}

/**
 * @brief 获取用户的默认账本 ID（兜底自动创建）
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return int64_t 默认账本 ID
 */
int64_t
ledger_get_default(csilk_db_pool_t* pool, int64_t user_id)
{
    if (user_id <= 0) {
        return 0;
    }

    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char* sql = "SELECT l.id FROM ledgers l "
                      "WHERE l.owner_id = ? AND l.is_default = 1 "
                      "LIMIT 1";

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, (const char*[]){uid, NULL});
    int64_t       id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = (int64_t)db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }

    if (id <= 0) {
        id = ledger_create(
            pool, user_id, "默认账本", "个人默认账本", "CNY", "ph:wallet", "#3b82f6", true);
    }
    return id;
}

/**
 * @brief 创建新账本空间并自动添加 owner 成员记录
 *
 * 1. 插入 `ledgers` 表获取生成的主键 ID。
 * 2. 若创建成功，向 `ledger_members` 插入关联记录 `role='owner'`。
 *
 * @param pool 数据库连接池指针
 * @param owner_id 创建者用户 ID
 * @param name 账本名称
 * @param description 描述
 * @param currency 货币
 * @param icon 图标
 * @param color 颜色
 * @param is_default 是否默认账本
 * @return int64_t 新账本 ID，失败返回 0
 */
int64_t
ledger_create(csilk_db_pool_t* pool,
              int64_t          owner_id,
              const char*      name,
              const char*      description,
              const char*      currency,
              const char*      icon,
              const char*      color,
              bool             is_default)
{
    char oid[32], def_str[8];
    snprintf(oid, sizeof(oid), "%lld", (long long)owner_id);
    snprintf(def_str, sizeof(def_str), "%d", is_default ? 1 : 0);

    const char* sql =
        "INSERT INTO ledgers (owner_id, name, description, currency, icon, color, is_default) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) RETURNING id";

    const char* params[] = {oid,
                            name ? name : "未命名账本",
                            description ? description : "",
                            currency ? currency : "CNY",
                            icon ? icon : "ph:wallet",
                            color ? color : "#3b82f6",
                            def_str,
                            NULL};

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int64_t       new_id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        new_id = (int64_t)db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }

    if (new_id > 0) {
        char nid[32];
        snprintf(nid, sizeof(nid), "%lld", (long long)new_id);
        csilk_json_t* m_res =
            csilk_db_query_param_json(pool,
                                      "INSERT INTO ledger_members (ledger_id, user_id, role) "
                                      "VALUES (?, ?, 'owner') RETURNING id",
                                      (const char*[]){nid, oid, NULL});
        if (m_res) {
            csilk_json_free(m_res);
        }
    }

    return new_id;
}

/**
 * @brief 更新账本基础属性
 *
 * 执行 SQL：`UPDATE ledgers SET name = ?, description = ?, currency = ?, icon = ?, color = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ? RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @param name 名称
 * @param description 描述
 * @param currency 货币
 * @param icon 图标
 * @param color 颜色
 * @return int 成功返回 0，失败返回 -1
 */
int
ledger_update(csilk_db_pool_t* pool,
              int64_t          ledger_id,
              const char*      name,
              const char*      description,
              const char*      currency,
              const char*      icon,
              const char*      color)
{
    char lid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);

    const char* sql = "UPDATE ledgers "
                      "SET name = ?, description = ?, currency = ?, icon = ?, color = ?, "
                      "updated_at = CURRENT_TIMESTAMP "
                      "WHERE id = ? RETURNING id";

    const char* params[] = {name ? name : "",
                            description ? description : "",
                            currency ? currency : "CNY",
                            icon ? icon : "ph:wallet",
                            color ? color : "#3b82f6",
                            lid,
                            NULL};

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

/**
 * @brief 级联删除账本及归属于该账本的所有业务数据
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @return int 成功返回 0，失败返回 -1
 */
int
ledger_delete(csilk_db_pool_t* pool, int64_t ledger_id)
{
    char lid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);

    /* 级联删除关联的定投计划、现金流计划、日常收支、交易记录、资产、分类与成员 */
    csilk_db_query_param_json(
        pool, "DELETE FROM dca_plans WHERE ledger_id = ?", (const char*[]){lid, NULL});
    csilk_db_query_param_json(
        pool, "DELETE FROM cashflow_schedules WHERE ledger_id = ?", (const char*[]){lid, NULL});
    csilk_db_query_param_json(
        pool, "DELETE FROM daily_expenses WHERE ledger_id = ?", (const char*[]){lid, NULL});
    csilk_db_query_param_json(
        pool, "DELETE FROM transactions WHERE ledger_id = ?", (const char*[]){lid, NULL});
    csilk_db_query_param_json(
        pool, "DELETE FROM assets WHERE ledger_id = ?", (const char*[]){lid, NULL});
    csilk_db_query_param_json(
        pool, "DELETE FROM categories WHERE ledger_id = ?", (const char*[]){lid, NULL});
    csilk_db_query_param_json(
        pool, "DELETE FROM ledger_members WHERE ledger_id = ?", (const char*[]){lid, NULL});

    csilk_json_t* res = csilk_db_query_param_json(
        pool, "DELETE FROM ledgers WHERE id = ? RETURNING id", (const char*[]){lid, NULL});
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

/**
 * @brief 查询指定账本的所有成员列表及其角色
 *
 * 排序规则：owner(1) -> editor(2) -> viewer(3)，同角色按 ID 升序。
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @return csilk_json_t* 成员列表 JSON 数组
 */
csilk_json_t*
ledger_member_list(csilk_db_pool_t* pool, int64_t ledger_id)
{
    char lid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);

    const char* sql =
        "SELECT m.id, m.ledger_id, m.user_id, u.username, m.role, "
        "       CAST(m.joined_at AS TEXT) AS joined_at "
        "FROM ledger_members m "
        "JOIN users u ON u.id = m.user_id "
        "WHERE m.ledger_id = ? "
        "ORDER BY CASE m.role WHEN 'owner' THEN 1 WHEN 'editor' THEN 2 ELSE 3 END, m.id ASC";

    return csilk_db_query_param_json(pool, sql, (const char*[]){lid, NULL});
}

/**
 * @brief 获取指定用户在账本中的权限角色
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @param user_id 用户 ID
 * @param[out] out_role 接收角色的字符串缓冲区
 * @param out_len 缓冲区长度
 * @return const char* 角色字符串，若不存在返回 NULL
 */
const char*
ledger_get_user_role(
    csilk_db_pool_t* pool, int64_t ledger_id, int64_t user_id, char* out_role, size_t out_len)
{
    char lid[32], uid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char*   sql = "SELECT role FROM ledger_members WHERE ledger_id = ? AND user_id = ?";
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, (const char*[]){lid, uid, NULL});
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return NULL;
    }

    const char* r = csilk_json_get_string(csilk_json_array_get(res, 0), "role");
    if (r && out_role && out_len > 0) {
        strncpy(out_role, r, out_len - 1);
        out_role[out_len - 1] = '\0';
    }
    csilk_json_free(res);
    return out_role;
}

/**
 * @brief 向账本添加新成员
 *
 * 执行 SQL：`INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (?, ?, ?) RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @param user_id 用户 ID
 * @param role 角色
 * @return int 成功返回 0，失败返回 -1
 */
int
ledger_member_add(csilk_db_pool_t* pool, int64_t ledger_id, int64_t user_id, const char* role)
{
    char lid[32], uid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char* sql = "INSERT INTO ledger_members (ledger_id, user_id, role) "
                      "VALUES (?, ?, ?) RETURNING id";

    const char*   params[] = {lid, uid, role ? role : "editor", NULL};
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

/**
 * @brief 更新账本成员角色
 *
 * 执行 SQL：`UPDATE ledger_members SET role = ? WHERE ledger_id = ? AND user_id = ? RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @param user_id 用户 ID
 * @param new_role 新角色
 * @return int 成功返回 0，失败返回 -1
 */
int
ledger_member_update_role(csilk_db_pool_t* pool,
                          int64_t          ledger_id,
                          int64_t          user_id,
                          const char*      new_role)
{
    char lid[32], uid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char* sql =
        "UPDATE ledger_members SET role = ? WHERE ledger_id = ? AND user_id = ? RETURNING id";

    const char*   params[] = {new_role ? new_role : "editor", lid, uid, NULL};
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

/**
 * @brief 移除账本成员
 *
 * 执行 SQL：`DELETE FROM ledger_members WHERE ledger_id = ? AND user_id = ? RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @param user_id 用户 ID
 * @return int 成功返回 0，失败返回 -1
 */
int
ledger_member_remove(csilk_db_pool_t* pool, int64_t ledger_id, int64_t user_id)
{
    char lid[32], uid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char* sql = "DELETE FROM ledger_members WHERE ledger_id = ? AND user_id = ? RETURNING id";
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, (const char*[]){lid, uid, NULL});
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

/**
 * @brief 更新账本邀请码及其有效期
 *
 * 执行 SQL：`UPDATE ledgers SET invite_code = ?, invite_expires_at = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ? RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @param invite_code 邀请码
 * @param expires_at 过期时间
 * @return int 成功返回 0，失败返回 -1
 */
int
ledger_update_invite_code(csilk_db_pool_t* pool,
                          int64_t          ledger_id,
                          const char*      invite_code,
                          const char*      expires_at)
{
    char lid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);

    const char* sql =
        "UPDATE ledgers SET invite_code = ?, invite_expires_at = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE id = ? RETURNING id";

    const char* params[] = {
        invite_code ? invite_code : "", expires_at ? expires_at : "", lid, NULL};
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

/**
 * @brief 根据邀请码查找未过期的账本详情
 *
 * 执行 SQL 校验 `l.invite_code = ? AND (l.invite_expires_at IS NULL OR l.invite_expires_at > CURRENT_TIMESTAMP)`
 *
 * @param pool 数据库连接池指针
 * @param invite_code 邀请码
 * @return csilk_json_t* 包含账本信息的 JSON 数组
 */
csilk_json_t*
ledger_find_by_invite_code(csilk_db_pool_t* pool, const char* invite_code)
{
    if (!invite_code || !invite_code[0]) {
        return NULL;
    }

    const char* sql =
        "SELECT l.id, l.owner_id, l.name, l.description, l.currency, l.icon, l.color, "
        "       CAST(l.invite_expires_at AS TEXT) AS invite_expires_at, "
        "       u.username AS owner_username "
        "FROM ledgers l "
        "JOIN users u ON u.id = l.owner_id "
        "WHERE l.invite_code = ? AND (l.invite_expires_at IS NULL OR l.invite_expires_at > "
        "CURRENT_TIMESTAMP)";

    csilk_json_t* arr = csilk_db_query_param_json(pool, sql, (const char*[]){invite_code, NULL});
    if (!arr || csilk_json_array_size(arr) == 0) {
        if (arr) {
            csilk_json_free(arr);
        }
        return NULL;
    }
    return arr;
}
