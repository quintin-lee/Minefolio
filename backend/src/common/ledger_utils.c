/**
 * @file ledger_utils.c
 * @brief 共享账本工具函数实现
 *
 * 从 legacy repository 提取的核心查询函数，供 ledger_engine、ctx.h 等使用。
 */

#include "common/ledger_utils.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

int64_t
ledger_get_default(csilk_db_pool_t* pool, int64_t user_id)
{
    if (user_id <= 0) {
        return 0;
    }

    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char* sql =
        "SELECT l.id FROM ledgers l WHERE l.owner_id = ? AND l.is_default = 1 LIMIT 1";
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, (const char*[]){uid, NULL});
    int64_t       id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = (int64_t)db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }

    if (id <= 0) {
        /* Create default ledger if not exists */
        const char* cr_sql =
            "INSERT INTO ledgers (owner_id, name, description, currency, icon, color, is_default) "
            "VALUES (?, '默认账本', '个人默认账本', 'CNY', 'ph:wallet', '#3b82f6', 1)";
        csilk_json_t* cr_res = csilk_db_query_param_json(pool, cr_sql, (const char*[]){uid, NULL});
        if (cr_res) {
            csilk_json_free(cr_res);
        }
        /* Re-query to get the ID */
        res = csilk_db_query_param_json(pool, sql, (const char*[]){uid, NULL});
        if (res && csilk_json_array_size(res) > 0) {
            id = (int64_t)db_get_int(csilk_json_array_get(res, 0), "id");
        }
        if (res) {
            csilk_json_free(res);
        }
        if (id > 0) {
            /* Mirror legacy ledger_create: owner membership row, else RBAC 1004 on writes */
            char lid[32];
            snprintf(lid, sizeof(lid), "%lld", (long long)id);
            csilk_json_t* m_res =
                csilk_db_query_param_json(pool,
                                          "INSERT INTO ledger_members (ledger_id, user_id, role) "
                                          "VALUES (?, ?, 'owner')",
                                          (const char*[]){lid, uid, NULL});
            if (m_res) {
                csilk_json_free(m_res);
            }
        }
    }
    return id;
}

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

csilk_json_t*
tx_get_old(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);
    return csilk_db_query_param_json(
        pool,
        "SELECT id, asset_id, linked_asset_id, transaction_type, amount, price_per_unit, "
        "quantity, fee, currency, transaction_date, note FROM transactions "
        "WHERE id=? AND user_id=?",
        (const char*[]){id_str, uid_str, NULL});
}

csilk_json_t*
tx_child_fee_rows(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_tx_id)
{
    char uid_str[32], ptx_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(ptx_str, sizeof(ptx_str), "%lld", (long long)parent_tx_id);
    return csilk_db_query_param_json(
        pool,
        "SELECT id, linked_asset_id, amount, fee, currency, note FROM transactions "
        "WHERE parent_tx_id=? AND user_id=? AND transaction_type='fee'",
        (const char*[]){ptx_str, uid_str, NULL});
}

int
tx_delete_fee_children(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_tx_id)
{
    char uid_str[32], ptx_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(ptx_str, sizeof(ptx_str), "%lld", (long long)parent_tx_id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "DELETE FROM transactions WHERE parent_tx_id=? AND user_id=? AND transaction_type='fee'"
        " RETURNING id",
        (const char*[]){ptx_str, uid_str, NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}
