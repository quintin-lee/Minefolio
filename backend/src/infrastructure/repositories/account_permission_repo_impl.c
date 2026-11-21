#include "infrastructure/repositories/account_permission_repo_impl.h"
#include "common/db.h"
#include "common/ledger_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

csilk_json_t*
account_perm_get(csilk_db_pool_t* pool, int64_t ledger_id, int64_t account_id, int64_t user_id)
{
    char lid_str[32], aid_str[32], uid_str[32];
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)account_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    return csilk_db_query_param_json(
        pool,
        "SELECT ap.id, ap.ledger_id, ap.account_id, ap.user_id, ap.permission_level, "
        "CAST(ap.mask_balance AS INTEGER) AS mask_balance, "
        "CAST(ap.mask_account_no AS INTEGER) AS mask_account_no, "
        "ap.created_at, ap.updated_at "
        "FROM account_permissions ap "
        "WHERE ap.ledger_id=? AND ap.account_id=? AND ap.user_id=?",
        (const char*[]){lid_str, aid_str, uid_str, NULL});
}

csilk_json_t*
account_perm_list_by_account(csilk_db_pool_t* pool, int64_t ledger_id, int64_t account_id)
{
    char lid_str[32], aid_str[32];
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)account_id);

    return csilk_db_query_param_json(
        pool,
        "SELECT ap.id, ap.ledger_id, ap.account_id, ap.user_id, u.username, ap.permission_level, "
        "CAST(ap.mask_balance AS INTEGER) AS mask_balance, "
        "CAST(ap.mask_account_no AS INTEGER) AS mask_account_no, "
        "ap.created_at, ap.updated_at "
        "FROM account_permissions ap "
        "JOIN users u ON ap.user_id = u.id "
        "WHERE ap.ledger_id=? AND ap.account_id=? "
        "ORDER BY ap.id ASC",
        (const char*[]){lid_str, aid_str, NULL});
}

csilk_json_t*
account_perm_list_by_user(csilk_db_pool_t* pool, int64_t ledger_id, int64_t user_id)
{
    char lid_str[32], uid_str[32];
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    return csilk_db_query_param_json(pool,
                                     "SELECT ap.id, ap.ledger_id, ap.account_id, ac.name AS "
                                     "account_name, ap.user_id, ap.permission_level, "
                                     "CAST(ap.mask_balance AS INTEGER) AS mask_balance, "
                                     "CAST(ap.mask_account_no AS INTEGER) AS mask_account_no, "
                                     "ap.created_at, ap.updated_at "
                                     "FROM account_permissions ap "
                                     "JOIN accounts ac ON ap.account_id = ac.id "
                                     "WHERE ap.ledger_id=? AND ap.user_id=? "
                                     "ORDER BY ap.id ASC",
                                     (const char*[]){lid_str, uid_str, NULL});
}

int
account_perm_upsert(csilk_db_pool_t* pool,
                    int64_t          ledger_id,
                    int64_t          account_id,
                    int64_t          user_id,
                    const char*      permission_level,
                    int              mask_balance,
                    int              mask_account_no)
{
    char lid_str[32], aid_str[32], uid_str[32], mb_str[16], ma_str[16];
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)account_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(mb_str, sizeof(mb_str), "%d", mask_balance ? 1 : 0);
    snprintf(ma_str, sizeof(ma_str), "%d", mask_account_no ? 1 : 0);

    const char* sql = "INSERT INTO account_permissions (ledger_id, account_id, user_id, "
                      "permission_level, mask_balance, mask_account_no, updated_at) "
                      "VALUES (?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP) "
                      "ON CONFLICT (ledger_id, account_id, user_id) "
                      "DO UPDATE SET permission_level = EXCLUDED.permission_level, "
                      "              mask_balance = EXCLUDED.mask_balance, "
                      "              mask_account_no = EXCLUDED.mask_account_no, "
                      "              updated_at = CURRENT_TIMESTAMP";

    const char* params[7] = {lid_str, aid_str, uid_str, permission_level, mb_str, ma_str, NULL};
    return csilk_db_exec_param(pool, sql, params) >= 0 ? 0 : -1;
}

int
account_perm_delete(csilk_db_pool_t* pool, int64_t ledger_id, int64_t account_id, int64_t user_id)
{
    char lid_str[32], aid_str[32], uid_str[32];
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)account_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    const char* sql =
        "DELETE FROM account_permissions WHERE ledger_id=? AND account_id=? AND user_id=?";
    const char* params[4] = {lid_str, aid_str, uid_str, NULL};
    return csilk_db_exec_param(pool, sql, params) >= 0 ? 0 : -1;
}

int
account_perm_get_effective_masks(csilk_db_pool_t* pool,
                                 int64_t          ledger_id,
                                 int64_t          account_id,
                                 int64_t          user_id,
                                 int*             out_mask_balance,
                                 int*             out_mask_account_no)
{
    if (out_mask_balance) {
        *out_mask_balance = 0;
    }
    if (out_mask_account_no) {
        *out_mask_account_no = 0;
    }
    if (!pool || account_id <= 0 || user_id <= 0) {
        return 0;
    }

    if (ledger_id > 0) {
        char role[32] = {0};
        if (ledger_get_user_role(pool, ledger_id, user_id, role, sizeof(role)) &&
            strcmp(role, "owner") == 0) {
            return 0;
        }
    }

    char aid_str[32], uid_str[32];
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)account_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT CAST(ap.mask_balance AS INTEGER) AS mask_balance, "
        "CAST(ap.mask_account_no AS INTEGER) AS mask_account_no "
        "FROM account_permissions ap "
        "JOIN accounts ac ON ap.account_id = ac.id "
        "JOIN ledgers l ON ac.ledger_id = l.id "
        "WHERE ap.account_id=? AND ap.user_id=? AND l.owner_id != ? LIMIT 1",
        (const char*[]){aid_str, uid_str, uid_str, NULL});
    if (res && csilk_json_array_size(res) > 0) {
        const csilk_json_t* r = csilk_json_array_get(res, 0);
        if (out_mask_balance) {
            *out_mask_balance = db_get_int(r, "mask_balance") != 0 ? 1 : 0;
        }
        if (out_mask_account_no) {
            *out_mask_account_no = db_get_int(r, "mask_account_no") != 0 ? 1 : 0;
        }
    }
    if (res) {
        csilk_json_free(res);
    }
    return 0;
}
