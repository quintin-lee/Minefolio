/**
 * @file ledger_repo_impl.c
 * @brief 账本仓储 SQL 实现 (Infrastructure Ledger Repository)
 *
 * 将 repositories/ledger_repo.c 中的 SQL 迁移到此文件。
 */

#include "infrastructure/repositories/ledger_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
mf_ledger_repo_list_by_user(void*                   db_pool,
                            int64_t                 user_id,
                            mf_ledger_list_item_t** out_list,
                            size_t*                 out_count)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char* sql =
        "SELECT l.id, l.owner_id, l.name, l.description, l.currency, l.icon, l.color, "
        "       l.is_default, l.invite_code, "
        "       COALESCE(CAST(l.invite_expires_at AS TEXT), '') AS invite_expires_at, "
        "       COALESCE(CAST(l.created_at AS TEXT), '') AS created_at, "
        "       COALESCE(CAST(l.updated_at AS TEXT), '') AS updated_at, "
        "       m.role AS my_role, "
        "       u.username AS owner_username, "
        "       (SELECT COUNT(*) FROM ledger_members WHERE ledger_id = l.id) AS member_count, "
        "       (SELECT COALESCE(SUM(current_value), 0) FROM assets WHERE ledger_id = l.id) AS "
        "total_assets "
        "FROM ledgers l "
        "JOIN ledger_members m ON m.ledger_id = l.id AND m.user_id = ? "
        "JOIN users u ON u.id = l.owner_id "
        "ORDER BY l.is_default DESC, l.id ASC";

    csilk_json_t* rows = csilk_db_query_param_json(pool, sql, (const char*[]){uid, NULL});
    if (!rows) {
        *out_list = NULL;
        *out_count = 0;
        return 0;
    }

    size_t                 n = csilk_json_array_size(rows);
    mf_ledger_list_item_t* list =
        n > 0 ? (mf_ledger_list_item_t*)malloc(sizeof(mf_ledger_list_item_t) * n) : NULL;

    for (size_t i = 0; i < n; i++) {
        csilk_json_t*          r = csilk_json_array_get(rows, i);
        mf_ledger_list_item_t* item = &list[i];
        memset(item, 0, sizeof(*item));

        item->id = (int64_t)db_get_int(r, "id");
        item->owner_id = (int64_t)db_get_int(r, "owner_id");
        strncpy(item->name, csilk_json_get_string(r, "name") ?: "", sizeof(item->name) - 1);
        strncpy(item->description,
                csilk_json_get_string(r, "description") ?: "",
                sizeof(item->description) - 1);
        strncpy(item->currency,
                csilk_json_get_string(r, "currency") ?: "CNY",
                sizeof(item->currency) - 1);
        strncpy(
            item->icon, csilk_json_get_string(r, "icon") ?: "ph:wallet", sizeof(item->icon) - 1);
        strncpy(
            item->color, csilk_json_get_string(r, "color") ?: "#3b82f6", sizeof(item->color) - 1);
        item->is_default = (db_get_num(r, "is_default") != 0);
        strncpy(item->invite_code,
                csilk_json_get_string(r, "invite_code") ?: "",
                sizeof(item->invite_code) - 1);
        strncpy(item->invite_expires_at,
                csilk_json_get_string(r, "invite_expires_at") ?: "",
                sizeof(item->invite_expires_at) - 1);
        strncpy(item->created_at,
                csilk_json_get_string(r, "created_at") ?: "",
                sizeof(item->created_at) - 1);
        strncpy(item->updated_at,
                csilk_json_get_string(r, "updated_at") ?: "",
                sizeof(item->updated_at) - 1);
        strncpy(
            item->my_role, csilk_json_get_string(r, "my_role") ?: "", sizeof(item->my_role) - 1);
        strncpy(item->owner_username,
                csilk_json_get_string(r, "owner_username") ?: "",
                sizeof(item->owner_username) - 1);
        item->member_count = (int64_t)db_get_int(r, "member_count");
        item->total_assets = db_get_num(r, "total_assets");
    }

    csilk_json_free(rows);
    *out_list = list;
    *out_count = n;
    return 0;
}

int
mf_ledger_repo_get(void* db_pool, int64_t ledger_id, mf_ledger_t* out)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             lid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);

    const char* sql =
        "SELECT l.id, l.owner_id, l.name, l.description, l.currency, l.icon, l.color, "
        "       l.is_default, l.invite_code, "
        "       COALESCE(CAST(l.invite_expires_at AS TEXT), '') AS invite_expires_at, "
        "       COALESCE(CAST(l.created_at AS TEXT), '') AS created_at, "
        "       COALESCE(CAST(l.updated_at AS TEXT), '') AS updated_at "
        "FROM ledgers l WHERE l.id = ?";

    csilk_json_t* arr = csilk_db_query_param_json(pool, sql, (const char*[]){lid, NULL});
    if (!arr || csilk_json_array_size(arr) == 0) {
        if (arr) {
            csilk_json_free(arr);
        }
        return 1;
    }

    csilk_json_t* r = csilk_json_array_get(arr, 0);
    memset(out, 0, sizeof(*out));
    out->id = (int64_t)db_get_int(r, "id");
    out->owner_id = (int64_t)db_get_int(r, "owner_id");
    strncpy(out->name, csilk_json_get_string(r, "name") ?: "", sizeof(out->name) - 1);
    strncpy(out->description,
            csilk_json_get_string(r, "description") ?: "",
            sizeof(out->description) - 1);
    strncpy(
        out->currency, csilk_json_get_string(r, "currency") ?: "CNY", sizeof(out->currency) - 1);
    strncpy(out->icon, csilk_json_get_string(r, "icon") ?: "ph:wallet", sizeof(out->icon) - 1);
    strncpy(out->color, csilk_json_get_string(r, "color") ?: "#3b82f6", sizeof(out->color) - 1);
    out->is_default = (db_get_num(r, "is_default") != 0);
    strncpy(out->invite_code,
            csilk_json_get_string(r, "invite_code") ?: "",
            sizeof(out->invite_code) - 1);
    strncpy(out->invite_expires_at,
            csilk_json_get_string(r, "invite_expires_at") ?: "",
            sizeof(out->invite_expires_at) - 1);
    strncpy(
        out->created_at, csilk_json_get_string(r, "created_at") ?: "", sizeof(out->created_at) - 1);
    strncpy(
        out->updated_at, csilk_json_get_string(r, "updated_at") ?: "", sizeof(out->updated_at) - 1);

    csilk_json_free(arr);
    return 0;
}

int64_t
mf_ledger_repo_get_or_create_default(void* db_pool, int64_t user_id)
{
    if (user_id <= 0) {
        return 0;
    }

    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid[32];
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
        /* Auto-create default ledger */
        mf_ledger_t ledger = {0};
        ledger.owner_id = user_id;
        strncpy(ledger.name, "默认账本", sizeof(ledger.name) - 1);
        strncpy(ledger.description, "个人默认账本", sizeof(ledger.description) - 1);
        strncpy(ledger.currency, "CNY", sizeof(ledger.currency) - 1);
        strncpy(ledger.icon, "ph:wallet", sizeof(ledger.icon) - 1);
        strncpy(ledger.color, "#3b82f6", sizeof(ledger.color) - 1);
        ledger.is_default = true;
        id = mf_ledger_repo_create(db_pool, user_id, &ledger);
    }
    return id;
}

int64_t
mf_ledger_repo_create(void* db_pool, int64_t owner_id, const mf_ledger_t* ledger)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             oid[32], def_str[8];
    snprintf(oid, sizeof(oid), "%lld", (long long)owner_id);
    snprintf(def_str, sizeof(def_str), "%d", ledger->is_default ? 1 : 0);

    const char* sql =
        "INSERT INTO ledgers (owner_id, name, description, currency, icon, color, is_default) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) RETURNING id";

    const char* params[] = {oid,
                            ledger->name[0] ? ledger->name : "未命名账本",
                            ledger->description[0] ? ledger->description : "",
                            ledger->currency[0] ? ledger->currency : "CNY",
                            ledger->icon[0] ? ledger->icon : "ph:wallet",
                            ledger->color[0] ? ledger->color : "#3b82f6",
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

int
mf_ledger_repo_update(void* db_pool, int64_t ledger_id, const mf_ledger_t* ledger)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             lid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);

    const char* sql = "UPDATE ledgers "
                      "SET name = ?, description = ?, currency = ?, icon = ?, color = ?, "
                      "updated_at = CURRENT_TIMESTAMP "
                      "WHERE id = ? RETURNING id";

    const char* params[] = {ledger->name[0] ? ledger->name : "",
                            ledger->description[0] ? ledger->description : "",
                            ledger->currency[0] ? ledger->currency : "CNY",
                            ledger->icon[0] ? ledger->icon : "ph:wallet",
                            ledger->color[0] ? ledger->color : "#3b82f6",
                            lid,
                            NULL};

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

int
mf_ledger_repo_delete(void* db_pool, int64_t ledger_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             lid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);

    /* Cascade delete in FK dependency order */
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

int
mf_ledger_repo_member_list(void*                db_pool,
                           int64_t              ledger_id,
                           mf_ledger_member_t** out_list,
                           size_t*              out_count)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             lid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);

    const char* sql =
        "SELECT m.id, m.ledger_id, m.user_id, u.username, m.role, "
        "       COALESCE(CAST(m.joined_at AS TEXT), '') AS joined_at "
        "FROM ledger_members m "
        "JOIN users u ON u.id = m.user_id "
        "WHERE m.ledger_id = ? "
        "ORDER BY CASE m.role WHEN 'owner' THEN 1 WHEN 'editor' THEN 2 ELSE 3 END, m.id ASC";

    csilk_json_t* rows = csilk_db_query_param_json(pool, sql, (const char*[]){lid, NULL});
    if (!rows) {
        *out_list = NULL;
        *out_count = 0;
        return 0;
    }

    size_t              n = csilk_json_array_size(rows);
    mf_ledger_member_t* list =
        n > 0 ? (mf_ledger_member_t*)malloc(sizeof(mf_ledger_member_t) * n) : NULL;

    for (size_t i = 0; i < n; i++) {
        csilk_json_t*       r = csilk_json_array_get(rows, i);
        mf_ledger_member_t* m = &list[i];
        memset(m, 0, sizeof(*m));

        m->id = (int64_t)db_get_int(r, "id");
        m->ledger_id = (int64_t)db_get_int(r, "ledger_id");
        m->user_id = (int64_t)db_get_int(r, "user_id");
        strncpy(m->username, csilk_json_get_string(r, "username") ?: "", sizeof(m->username) - 1);
        strncpy(m->role, csilk_json_get_string(r, "role") ?: "", sizeof(m->role) - 1);
        strncpy(
            m->joined_at, csilk_json_get_string(r, "joined_at") ?: "", sizeof(m->joined_at) - 1);
    }

    csilk_json_free(rows);
    *out_list = list;
    *out_count = n;
    return 0;
}

const char*
mf_ledger_repo_get_user_role(
    void* db_pool, int64_t ledger_id, int64_t user_id, char* out_role, size_t out_len)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             lid[32], uid[32];
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

int
mf_ledger_repo_member_add(void* db_pool, int64_t ledger_id, int64_t user_id, const char* role)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             lid[32], uid[32];
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

int
mf_ledger_repo_member_update_role(void*       db_pool,
                                  int64_t     ledger_id,
                                  int64_t     user_id,
                                  const char* new_role)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             lid[32], uid[32];
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

int
mf_ledger_repo_member_remove(void* db_pool, int64_t ledger_id, int64_t user_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             lid[32], uid[32];
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

int
mf_ledger_repo_update_invite_code(void*       db_pool,
                                  int64_t     ledger_id,
                                  const char* invite_code,
                                  const char* expires_at)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             lid[32];
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

int
mf_ledger_repo_find_by_invite_code(void* db_pool, const char* invite_code, mf_ledger_t* out)
{
    if (!invite_code || !invite_code[0]) {
        return 1;
    }

    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;

    const char* sql =
        "SELECT l.id, l.owner_id, l.name, l.description, l.currency, l.icon, l.color, "
        "       l.is_default, l.invite_code, "
        "       COALESCE(CAST(l.invite_expires_at AS TEXT), '') AS invite_expires_at, "
        "       COALESCE(CAST(l.created_at AS TEXT), '') AS created_at, "
        "       COALESCE(CAST(l.updated_at AS TEXT), '') AS updated_at "
        "FROM ledgers l "
        "WHERE l.invite_code = ? AND (l.invite_expires_at IS NULL OR l.invite_expires_at > "
        "CURRENT_TIMESTAMP)";

    csilk_json_t* arr = csilk_db_query_param_json(pool, sql, (const char*[]){invite_code, NULL});
    if (!arr || csilk_json_array_size(arr) == 0) {
        if (arr) {
            csilk_json_free(arr);
        }
        return 1;
    }

    csilk_json_t* r = csilk_json_array_get(arr, 0);
    memset(out, 0, sizeof(*out));
    out->id = (int64_t)db_get_int(r, "id");
    out->owner_id = (int64_t)db_get_int(r, "owner_id");
    strncpy(out->name, csilk_json_get_string(r, "name") ?: "", sizeof(out->name) - 1);
    strncpy(out->description,
            csilk_json_get_string(r, "description") ?: "",
            sizeof(out->description) - 1);
    strncpy(
        out->currency, csilk_json_get_string(r, "currency") ?: "CNY", sizeof(out->currency) - 1);
    strncpy(out->icon, csilk_json_get_string(r, "icon") ?: "ph:wallet", sizeof(out->icon) - 1);
    strncpy(out->color, csilk_json_get_string(r, "color") ?: "#3b82f6", sizeof(out->color) - 1);
    out->is_default = (db_get_num(r, "is_default") != 0);

    csilk_json_free(arr);
    return 0;
}

int64_t
mf_ledger_repo_find_user_by_username(void* db_pool, const char* username)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;

    csilk_json_t* res = csilk_db_query_param_json(
        pool, "SELECT id FROM users WHERE username = ?", (const char*[]){username, NULL});
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return 0;
    }

    int64_t uid = (int64_t)db_get_int(csilk_json_array_get(res, 0), "id");
    csilk_json_free(res);
    return uid;
}

void
mf_ledger_repo_free_list(void* list)
{
    free(list);
}
