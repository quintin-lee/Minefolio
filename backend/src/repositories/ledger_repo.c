#include "repositories/ledger_repo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

csilk_json_t*
ledger_list_by_user(csilk_db_pool_t* pool, int64_t user_id)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char* sql =
        "SELECT l.id, l.owner_id, l.name, l.description, l.currency, l.icon, l.color, "
        "       l.is_default, l.invite_code, CAST(l.invite_expires_at AS TEXT) AS invite_expires_at, "
        "       CAST(l.created_at AS TEXT) AS created_at, CAST(l.updated_at AS TEXT) AS updated_at, "
        "       m.role AS my_role, "
        "       u.username AS owner_username, "
        "       (SELECT COUNT(*) FROM ledger_members WHERE ledger_id = l.id) AS member_count, "
        "       (SELECT COALESCE(SUM(current_value), 0) FROM assets WHERE ledger_id = l.id) AS total_assets "
        "FROM ledgers l "
        "JOIN ledger_members m ON m.ledger_id = l.id AND m.user_id = ? "
        "JOIN users u ON u.id = l.owner_id "
        "ORDER BY l.is_default DESC, l.id ASC";

    return csilk_db_query_param_json(pool, sql, (const char*[]){uid, NULL});
}

csilk_json_t*
ledger_get(csilk_db_pool_t* pool, int64_t ledger_id)
{
    char lid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);

    const char* sql =
        "SELECT l.id, l.owner_id, l.name, l.description, l.currency, l.icon, l.color, "
        "       l.is_default, l.invite_code, CAST(l.invite_expires_at AS TEXT) AS invite_expires_at, "
        "       CAST(l.created_at AS TEXT) AS created_at, CAST(l.updated_at AS TEXT) AS updated_at, "
        "       u.username AS owner_username, "
        "       (SELECT COUNT(*) FROM ledger_members WHERE ledger_id = l.id) AS member_count, "
        "       (SELECT COALESCE(SUM(current_value), 0) FROM assets WHERE ledger_id = l.id) AS total_assets "
        "FROM ledgers l "
        "JOIN users u ON u.id = l.owner_id "
        "WHERE l.id = ?";

    csilk_json_t* arr = csilk_db_query_param_json(pool, sql, (const char*[]){lid, NULL});
    if (!arr || csilk_json_array_size(arr) == 0) {
        if (arr) csilk_json_free(arr);
        return NULL;
    }
    return arr;
}

int64_t
ledger_get_default(csilk_db_pool_t* pool, int64_t user_id)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char* sql =
        "SELECT l.id FROM ledgers l "
        "JOIN ledger_members m ON m.ledger_id = l.id AND m.user_id = ? "
        "ORDER BY l.is_default DESC, l.id ASC LIMIT 1";

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, (const char*[]){uid, NULL});
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = (int64_t)db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) csilk_json_free(res);
    return id;
}

int64_t
ledger_create(csilk_db_pool_t* pool, int64_t owner_id, const char* name,
              const char* description, const char* currency, const char* icon,
              const char* color, bool is_default)
{
    char oid[32], def_str[8];
    snprintf(oid, sizeof(oid), "%lld", (long long)owner_id);
    snprintf(def_str, sizeof(def_str), "%d", is_default ? 1 : 0);

    const char* sql =
        "INSERT INTO ledgers (owner_id, name, description, currency, icon, color, is_default) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) RETURNING id";

    const char* params[] = {
        oid,
        name ? name : "未命名账本",
        description ? description : "",
        currency ? currency : "CNY",
        icon ? icon : "ph:wallet",
        color ? color : "#3b82f6",
        def_str,
        NULL
    };

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int64_t new_id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        new_id = (int64_t)db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) csilk_json_free(res);

    if (new_id > 0) {
        char nid[32];
        snprintf(nid, sizeof(nid), "%lld", (long long)new_id);
        csilk_json_t* m_res = csilk_db_query_param_json(
            pool,
            "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (?, ?, 'owner') RETURNING id",
            (const char*[]){nid, oid, NULL});
        if (m_res) csilk_json_free(m_res);
    }

    return new_id;
}

int
ledger_update(csilk_db_pool_t* pool, int64_t ledger_id, const char* name,
              const char* description, const char* currency, const char* icon,
              const char* color)
{
    char lid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);

    const char* sql =
        "UPDATE ledgers "
        "SET name = ?, description = ?, currency = ?, icon = ?, color = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE id = ? RETURNING id";

    const char* params[] = {
        name ? name : "",
        description ? description : "",
        currency ? currency : "CNY",
        icon ? icon : "ph:wallet",
        color ? color : "#3b82f6",
        lid,
        NULL
    };

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) csilk_json_free(res);
    return ok ? 0 : -1;
}

int
ledger_delete(csilk_db_pool_t* pool, int64_t ledger_id)
{
    char lid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);

    /* Delete associated assets, transactions, daily_expenses, dca_plans, cashflow_schedules, categories */
    csilk_db_query_param_json(pool, "DELETE FROM dca_plans WHERE ledger_id = ?", (const char*[]){lid, NULL});
    csilk_db_query_param_json(pool, "DELETE FROM cashflow_schedules WHERE ledger_id = ?", (const char*[]){lid, NULL});
    csilk_db_query_param_json(pool, "DELETE FROM daily_expenses WHERE ledger_id = ?", (const char*[]){lid, NULL});
    csilk_db_query_param_json(pool, "DELETE FROM transactions WHERE ledger_id = ?", (const char*[]){lid, NULL});
    csilk_db_query_param_json(pool, "DELETE FROM assets WHERE ledger_id = ?", (const char*[]){lid, NULL});
    csilk_db_query_param_json(pool, "DELETE FROM categories WHERE ledger_id = ?", (const char*[]){lid, NULL});
    csilk_db_query_param_json(pool, "DELETE FROM ledger_members WHERE ledger_id = ?", (const char*[]){lid, NULL});

    csilk_json_t* res = csilk_db_query_param_json(pool, "DELETE FROM ledgers WHERE id = ? RETURNING id", (const char*[]){lid, NULL});
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) csilk_json_free(res);
    return ok ? 0 : -1;
}

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

const char*
ledger_get_user_role(csilk_db_pool_t* pool, int64_t ledger_id, int64_t user_id, char* out_role, size_t out_len)
{
    char lid[32], uid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char* sql = "SELECT role FROM ledger_members WHERE ledger_id = ? AND user_id = ?";
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, (const char*[]){lid, uid, NULL});
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) csilk_json_free(res);
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
ledger_member_add(csilk_db_pool_t* pool, int64_t ledger_id, int64_t user_id, const char* role)
{
    char lid[32], uid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char* sql =
        "INSERT INTO ledger_members (ledger_id, user_id, role) "
        "VALUES (?, ?, ?) RETURNING id";

    const char* params[] = { lid, uid, role ? role : "editor", NULL };
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) csilk_json_free(res);
    return ok ? 0 : -1;
}

int
ledger_member_update_role(csilk_db_pool_t* pool, int64_t ledger_id, int64_t user_id, const char* new_role)
{
    char lid[32], uid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char* sql =
        "UPDATE ledger_members SET role = ? WHERE ledger_id = ? AND user_id = ? RETURNING id";

    const char* params[] = { new_role ? new_role : "editor", lid, uid, NULL };
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) csilk_json_free(res);
    return ok ? 0 : -1;
}

int
ledger_member_remove(csilk_db_pool_t* pool, int64_t ledger_id, int64_t user_id)
{
    char lid[32], uid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char* sql = "DELETE FROM ledger_members WHERE ledger_id = ? AND user_id = ? RETURNING id";
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, (const char*[]){lid, uid, NULL});
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) csilk_json_free(res);
    return ok ? 0 : -1;
}

int
ledger_update_invite_code(csilk_db_pool_t* pool, int64_t ledger_id, const char* invite_code, const char* expires_at)
{
    char lid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);

    const char* sql =
        "UPDATE ledgers SET invite_code = ?, invite_expires_at = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE id = ? RETURNING id";

    const char* params[] = { invite_code ? invite_code : "", expires_at ? expires_at : "", lid, NULL };
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) csilk_json_free(res);
    return ok ? 0 : -1;
}

csilk_json_t*
ledger_find_by_invite_code(csilk_db_pool_t* pool, const char* invite_code)
{
    if (!invite_code || !invite_code[0]) return NULL;

    const char* sql =
        "SELECT l.id, l.owner_id, l.name, l.description, l.currency, l.icon, l.color, "
        "       CAST(l.invite_expires_at AS TEXT) AS invite_expires_at, "
        "       u.username AS owner_username "
        "FROM ledgers l "
        "JOIN users u ON u.id = l.owner_id "
        "WHERE l.invite_code = ? AND (l.invite_expires_at IS NULL OR l.invite_expires_at > CURRENT_TIMESTAMP)";

    csilk_json_t* arr = csilk_db_query_param_json(pool, sql, (const char*[]){invite_code, NULL});
    if (!arr || csilk_json_array_size(arr) == 0) {
        if (arr) csilk_json_free(arr);
        return NULL;
    }
    return arr;
}
