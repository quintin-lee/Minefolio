#include "application/account/usecases.h"
#include "domain/account/rules.h"
#include "domain/account/entity.h"
#include "infrastructure/repositories/account_repo_impl.h"
#include "infrastructure/repositories/account_permission_repo_impl.h"
#include "common/db.h"
#include "common/ledger_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===== Internal helpers ===== */

static csilk_json_t*
format_account_row(const csilk_json_t* row, const char* ledger_role)
{
    if (!row) {
        return NULL;
    }
    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_int(obj, "id", db_get_int(row, "id"));
    csilk_json_add_int(obj, "user_id", db_get_int(row, "user_id"));
    csilk_json_add_int(obj, "ledger_id", db_get_int(row, "ledger_id"));
    csilk_json_add_string(obj, "name", csilk_json_get_string(row, "name"));
    csilk_json_add_string(obj, "institution", csilk_json_get_string(row, "institution"));
    csilk_json_add_string(obj, "currency", csilk_json_get_string(row, "currency"));
    csilk_json_add_string(obj, "icon", csilk_json_get_string(row, "icon"));
    csilk_json_add_string(obj, "note", csilk_json_get_string(row, "note"));
    csilk_json_add_int(obj, "sort_order", db_get_int(row, "sort_order"));
    csilk_json_add_int(obj, "archived", db_get_int(row, "archived"));
    csilk_json_add_string(obj, "created_at", csilk_json_get_string(row, "created_at"));
    csilk_json_add_string(obj, "updated_at", csilk_json_get_string(row, "updated_at"));
    csilk_json_add_int(obj, "asset_count", db_get_int(row, "asset_count"));

    /* 1. 账号/卡号脱敏 */
    int         mask_acc = db_get_int(row, "mask_account_no");
    const char* raw_acc = csilk_json_get_string(row, "account_no");
    if (mask_acc != 0) {
        char masked_buf[128] = {0};
        mf_account_rule_mask_account_no(raw_acc, masked_buf, sizeof(masked_buf));
        csilk_json_add_string(obj, "account_no", masked_buf);
        csilk_json_add_bool(obj, "is_account_no_masked", true);
    } else {
        csilk_json_add_string(obj, "account_no", raw_acc);
        csilk_json_add_bool(obj, "is_account_no_masked", false);
    }

    /* 2. 金额脱敏 */
    int mask_bal = db_get_int(row, "mask_balance");
    if (mask_bal != 0) {
        csilk_json_add_number(obj, "total_value", 0.0);
        csilk_json_add_bool(obj, "is_balance_masked", true);
    } else {
        csilk_json_add_number(obj, "total_value", db_get_num(row, "total_value"));
        csilk_json_add_bool(obj, "is_balance_masked", false);
    }

    /* 3. 规范化生效权限级别 */
    const char*             perm_s = csilk_json_get_string(row, "permission_level");
    mf_account_perm_level_t configured = mf_account_rule_parse_perm_level(perm_s);
    mf_account_perm_level_t effective = mf_account_rule_effective_perm(configured, ledger_role);
    csilk_json_add_string(obj, "permission_level", mf_account_rule_perm_level_to_string(effective));

    return obj;
}

int
account_usecase_check_permission(
    void* pool, int64_t ledger_id, int64_t user_id, int64_t account_id, mf_account_action_t action)
{
    if (account_id <= 0 || ledger_id <= 0 || user_id <= 0) {
        return 0;
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    char             role[32] = {0};
    if (!ledger_get_user_role(db_pool, ledger_id, user_id, role, sizeof(role))) {
        return 1004;
    }

    if (strcmp(role, "owner") == 0) {
        return 0;
    }

    csilk_json_t*           perm_res = account_perm_get(db_pool, ledger_id, account_id, user_id);
    mf_account_perm_level_t configured = MF_ACCOUNT_PERM_INHERIT;
    if (perm_res && csilk_json_array_size(perm_res) > 0) {
        const csilk_json_t* r = csilk_json_array_get(perm_res, 0);
        configured = mf_account_rule_parse_perm_level(csilk_json_get_string(r, "permission_level"));
    }
    if (perm_res) {
        csilk_json_free(perm_res);
    }

    mf_account_perm_level_t eff = mf_account_rule_effective_perm(configured, role);

    switch (action) {
    case MF_ACCOUNT_ACTION_READ:
        return mf_account_rule_can_read(eff) ? 0 : 1004;
    case MF_ACCOUNT_ACTION_WRITE_TX:
        return mf_account_rule_can_write_tx(eff) ? 0 : 1004;
    case MF_ACCOUNT_ACTION_MANAGE:
        return mf_account_rule_can_manage(eff) ? 0 : 1004;
    default:
        return 1004;
    }
}

int
account_usecase_create(void*                       pool,
                       const create_account_cmd_t* cmd,
                       account_usecase_result_t*   out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));

    if (!cmd || !cmd->user_id) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "缺少 user_id");
        return -1;
    }

    /* 领域规则校验 */
    const char* name = cmd->name;
    if (!mf_account_rule_validate_name(name)) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "账户名称无效");
        return -1;
    }

    const char* institution = cmd->institution && cmd->institution[0] ? cmd->institution : "other";
    if (!mf_account_rule_is_valid_institution(institution)) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "机构类型无效");
        return -1;
    }

    /* 账本缺省 → 默认账本 */
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    int64_t          ledger_id = cmd->ledger_id;
    if (ledger_id <= 0) {
        ledger_id = ledger_get_default(db_pool, cmd->user_id);
    }
    if (ledger_id <= 0) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "无可用账本");
        return -1;
    }

    int64_t id = account_insert(db_pool,
                                cmd->user_id,
                                ledger_id,
                                name,
                                institution,
                                cmd->account_no,
                                cmd->currency,
                                cmd->icon,
                                cmd->note,
                                cmd->sort_order);
    if (id <= 0) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "账户已存在或创建失败");
        return -1;
    }

    out_res->code = 0;
    out_res->id = id;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
account_usecase_update(void*                       pool,
                       const update_account_cmd_t* cmd,
                       account_usecase_result_t*   out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));

    if (!cmd || !cmd->user_id || cmd->id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "缺少必要参数");
        return -1;
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    /* 归属校验与权限判定 */
    csilk_json_t* cur = account_get(db_pool, cmd->user_id, cmd->id);
    if (!cur || csilk_json_array_size(cur) == 0) {
        if (cur) {
            csilk_json_free(cur);
        }
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "账户不存在");
        return -1;
    }

    const csilk_json_t* cr = csilk_json_array_get(cur, 0);
    int64_t             ledger_id = db_get_int(cr, "ledger_id");
    int                 perm_chk = account_usecase_check_permission(
        db_pool, ledger_id, cmd->user_id, cmd->id, MF_ACCOUNT_ACTION_MANAGE);
    if (perm_chk != 0) {
        csilk_json_free(cur);
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "Forbidden: 账户管理权限不足");
        return -1;
    }

    const char* name = (cmd->name && cmd->name[0]) ? cmd->name : csilk_json_get_string(cr, "name");
    const char* institution = (cmd->institution && cmd->institution[0])
                                  ? cmd->institution
                                  : csilk_json_get_string(cr, "institution");
    const char* currency =
        (cmd->currency && cmd->currency[0]) ? cmd->currency : csilk_json_get_string(cr, "currency");

    if (name && !mf_account_rule_validate_name(name)) {
        csilk_json_free(cur);
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "账户名称无效");
        return -1;
    }
    if (!mf_account_rule_is_valid_institution(institution)) {
        csilk_json_free(cur);
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "机构类型无效");
        return -1;
    }

    int ok = account_update(db_pool,
                            cmd->user_id,
                            cmd->id,
                            name,
                            institution,
                            cmd->account_no,
                            currency,
                            cmd->icon,
                            cmd->note,
                            cmd->sort_order,
                            cmd->has_sort_order,
                            cmd->has_archived,
                            cmd->archived);
    csilk_json_free(cur);
    if (ok != 0) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "更新失败");
        return -1;
    }

    out_res->code = 0;
    out_res->id = cmd->id;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
account_usecase_delete(void*                       pool,
                       const delete_account_cmd_t* cmd,
                       account_usecase_result_t*   out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));

    if (!cmd || !cmd->user_id || cmd->id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "缺少必要参数");
        return -1;
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    csilk_json_t*    cur = account_get(db_pool, cmd->user_id, cmd->id);
    if (!cur || csilk_json_array_size(cur) == 0) {
        if (cur) {
            csilk_json_free(cur);
        }
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "账户不存在");
        return -1;
    }

    const csilk_json_t* cr = csilk_json_array_get(cur, 0);
    int64_t             ledger_id = db_get_int(cr, "ledger_id");
    int                 perm_chk = account_usecase_check_permission(
        db_pool, ledger_id, cmd->user_id, cmd->id, MF_ACCOUNT_ACTION_MANAGE);
    csilk_json_free(cur);
    if (perm_chk != 0) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "Forbidden: 账户管理权限不足");
        return -1;
    }

    int64_t asset_count = 0;
    if (account_count_assets(db_pool, cmd->user_id, cmd->id, &asset_count) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询账户资产失败");
        return -1;
    }

    int has_tx = 0;
    if (account_has_transactions(db_pool, cmd->user_id, cmd->id, &has_tx) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询账户交易失败");
        return -1;
    }

    if (!mf_account_rule_can_delete(asset_count > 0, has_tx)) {
        out_res->code = 1004;
        snprintf(out_res->message,
                 sizeof(out_res->message),
                 "账户下存在资产或交易，无法删除；请改为归档");
        return -1;
    }

    int rc = account_delete(db_pool, cmd->user_id, cmd->id);
    if (rc != 0) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "删除失败");
        return -1;
    }

    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
account_usecase_archive(
    void* pool, int64_t user_id, int64_t id, int archived, account_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));

    if (user_id <= 0 || id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "缺少必要参数");
        return -1;
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    csilk_json_t*    cur = account_get(db_pool, user_id, id);
    if (!cur || csilk_json_array_size(cur) == 0) {
        if (cur) {
            csilk_json_free(cur);
        }
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "账户不存在");
        return -1;
    }

    const csilk_json_t* cr = csilk_json_array_get(cur, 0);
    int64_t             ledger_id = db_get_int(cr, "ledger_id");
    int                 perm_chk =
        account_usecase_check_permission(db_pool, ledger_id, user_id, id, MF_ACCOUNT_ACTION_MANAGE);
    if (perm_chk != 0) {
        csilk_json_free(cur);
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "Forbidden: 账户管理权限不足");
        return -1;
    }

    const char* name = csilk_json_get_string(cr, "name");
    const char* institution = csilk_json_get_string(cr, "institution");
    const char* currency = csilk_json_get_string(cr, "currency");

    int ok = account_update(db_pool,
                            user_id,
                            id,
                            name,
                            institution,
                            csilk_json_get_string(cr, "account_no"),
                            currency,
                            csilk_json_get_string(cr, "icon"),
                            csilk_json_get_string(cr, "note"),
                            (int64_t)db_get_num(cr, "sort_order"),
                            0,
                            1,
                            archived);
    csilk_json_free(cur);

    if (ok != 0) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "归档失败");
        return -1;
    }

    out_res->code = 0;
    out_res->id = id;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
account_usecase_query(void*          pool,
                      int64_t        user_id,
                      int64_t        ledger_id,
                      int            include_archived,
                      csilk_json_t** out_list,
                      int64_t*       out_total)
{
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    if (ledger_id <= 0) {
        ledger_id = ledger_get_default(db_pool, user_id);
    }

    char role[32] = {0};
    ledger_get_user_role(db_pool, ledger_id, user_id, role, sizeof(role));

    int64_t       total = 0;
    csilk_json_t* raw_rows =
        account_list(db_pool, user_id, ledger_id, include_archived != 0, &total);
    if (!raw_rows) {
        *out_list = csilk_json_array();
        if (out_total) {
            *out_total = 0;
        }
        return 0;
    }

    csilk_json_t* formatted_arr = csilk_json_array();
    size_t        count = csilk_json_array_size(raw_rows);
    for (size_t i = 0; i < count; i++) {
        csilk_json_t* formatted = format_account_row(csilk_json_array_get(raw_rows, i), role);
        if (formatted) {
            csilk_json_array_append(formatted_arr, formatted);
        }
    }
    csilk_json_free(raw_rows);

    *out_list = formatted_arr;
    if (out_total) {
        *out_total = (int64_t)count;
    }
    return 0;
}

int
account_usecase_get(void*                     pool,
                    int64_t                   user_id,
                    int64_t                   ledger_id,
                    int64_t                   account_id,
                    csilk_json_t**            out_obj,
                    account_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    csilk_json_t*    cur = account_get(db_pool, user_id, account_id);
    if (!cur || csilk_json_array_size(cur) == 0) {
        if (cur) {
            csilk_json_free(cur);
        }
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "账户不存在");
        return -1;
    }

    char role[32] = {0};
    ledger_get_user_role(db_pool, ledger_id, user_id, role, sizeof(role));

    csilk_json_t* formatted = format_account_row(csilk_json_array_get(cur, 0), role);
    csilk_json_free(cur);

    if (out_obj) {
        *out_obj = formatted;
    } else if (formatted) {
        csilk_json_free(formatted);
    }
    out_res->code = 0;
    return 0;
}

void
account_usecase_seed_default(void* pool, int64_t user_id, int64_t ledger_id)
{
    if (user_id <= 0) {
        return;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    int64_t          lid = ledger_id;
    if (lid <= 0) {
        lid = ledger_get_default(db_pool, user_id);
    }
    if (lid <= 0) {
        return;
    }
    account_create_default(db_pool, user_id, lid);
}

/* ===== RBAC Permission use cases (Owner only) ===== */

static int
verify_owner_permission(csilk_db_pool_t*          pool,
                        int64_t                   ledger_id,
                        int64_t                   user_id,
                        account_usecase_result_t* out_res)
{
    char role[32] = {0};
    if (!ledger_get_user_role(pool, ledger_id, user_id, role, sizeof(role)) ||
        strcmp(role, "owner") != 0) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "Forbidden: 仅账本所有者可管理权限");
        return -1;
    }
    return 0;
}

int
account_permission_usecase_list_by_account(void*                     pool,
                                           int64_t                   user_id,
                                           int64_t                   ledger_id,
                                           int64_t                   account_id,
                                           csilk_json_t**            out_list,
                                           account_usecase_result_t* out_res)
{
    if (!out_res || !out_list) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    if (verify_owner_permission(db_pool, ledger_id, user_id, out_res) != 0) {
        return -1;
    }

    csilk_json_t* list = account_perm_list_by_account(db_pool, ledger_id, account_id);
    *out_list = list ? list : csilk_json_array();
    out_res->code = 0;
    return 0;
}

int
account_permission_usecase_list_by_user(void*                     pool,
                                        int64_t                   user_id,
                                        int64_t                   ledger_id,
                                        int64_t                   target_user_id,
                                        csilk_json_t**            out_list,
                                        account_usecase_result_t* out_res)
{
    if (!out_res || !out_list) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    if (verify_owner_permission(db_pool, ledger_id, user_id, out_res) != 0) {
        return -1;
    }

    csilk_json_t* list = account_perm_list_by_user(db_pool, ledger_id, target_user_id);
    *out_list = list ? list : csilk_json_array();
    out_res->code = 0;
    return 0;
}

int
account_permission_usecase_set(void*                     pool,
                               int64_t                   user_id,
                               int64_t                   ledger_id,
                               int64_t                   account_id,
                               int64_t                   target_user_id,
                               const char*               permission_level,
                               int                       mask_balance,
                               int                       mask_account_no,
                               account_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    if (verify_owner_permission(db_pool, ledger_id, user_id, out_res) != 0) {
        return -1;
    }

    mf_account_perm_level_t lvl = mf_account_rule_parse_perm_level(permission_level);
    if (lvl == MF_ACCOUNT_PERM_INHERIT) {
        /* 当设置为 inherit 时，直接删除显式覆盖规则 */
        account_perm_delete(db_pool, ledger_id, account_id, target_user_id);
        out_res->code = 0;
        snprintf(out_res->message, sizeof(out_res->message), "ok");
        return 0;
    }

    const char* norm_lvl = mf_account_rule_perm_level_to_string(lvl);
    if (account_perm_upsert(db_pool,
                            ledger_id,
                            account_id,
                            target_user_id,
                            norm_lvl,
                            mask_balance,
                            mask_account_no) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "保存权限失败");
        return -1;
    }

    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
account_permission_usecase_batch_set(void*                     pool,
                                     int64_t                   user_id,
                                     int64_t                   ledger_id,
                                     int64_t                   account_id,
                                     const csilk_json_t*       rules_arr,
                                     account_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    if (verify_owner_permission(db_pool, ledger_id, user_id, out_res) != 0) {
        return -1;
    }

    if (!rules_arr || !csilk_json_is_array(rules_arr)) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "permissions 必须为数组");
        return -1;
    }

    size_t count = csilk_json_array_size(rules_arr);
    for (size_t i = 0; i < count; i++) {
        const csilk_json_t* rule = csilk_json_array_get(rules_arr, i);
        int64_t             target_uid = db_get_int(rule, "user_id");
        const char*         perm = csilk_json_get_string(rule, "permission_level");
        int                 mask_bal = db_get_int(rule, "mask_balance");
        int                 mask_acc = db_get_int(rule, "mask_account_no");

        if (target_uid <= 0) {
            continue;
        }

        mf_account_perm_level_t lvl = mf_account_rule_parse_perm_level(perm);
        if (lvl == MF_ACCOUNT_PERM_INHERIT) {
            account_perm_delete(db_pool, ledger_id, account_id, target_uid);
        } else {
            account_perm_upsert(db_pool,
                                ledger_id,
                                account_id,
                                target_uid,
                                mf_account_rule_perm_level_to_string(lvl),
                                mask_bal,
                                mask_acc);
        }
    }

    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
account_permission_usecase_batch_set_for_user(void*                     pool,
                                              int64_t                   user_id,
                                              int64_t                   ledger_id,
                                              int64_t                   target_user_id,
                                              const csilk_json_t*       rules_arr,
                                              account_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    if (verify_owner_permission(db_pool, ledger_id, user_id, out_res) != 0) {
        return -1;
    }

    if (!rules_arr || !csilk_json_is_array(rules_arr)) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "permissions 必须为数组");
        return -1;
    }

    size_t count = csilk_json_array_size(rules_arr);
    for (size_t i = 0; i < count; i++) {
        const csilk_json_t* rule = csilk_json_array_get(rules_arr, i);
        int64_t             aid = db_get_int(rule, "account_id");
        const char*         perm = csilk_json_get_string(rule, "permission_level");
        int                 mask_bal = db_get_int(rule, "mask_balance");
        int                 mask_acc = db_get_int(rule, "mask_account_no");

        if (aid <= 0) {
            continue;
        }

        mf_account_perm_level_t lvl = mf_account_rule_parse_perm_level(perm);
        if (lvl == MF_ACCOUNT_PERM_INHERIT) {
            account_perm_delete(db_pool, ledger_id, aid, target_user_id);
        } else {
            account_perm_upsert(db_pool,
                                ledger_id,
                                aid,
                                target_user_id,
                                mf_account_rule_perm_level_to_string(lvl),
                                mask_bal,
                                mask_acc);
        }
    }

    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
account_permission_usecase_delete(void*                     pool,
                                  int64_t                   user_id,
                                  int64_t                   ledger_id,
                                  int64_t                   account_id,
                                  int64_t                   target_user_id,
                                  account_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    if (verify_owner_permission(db_pool, ledger_id, user_id, out_res) != 0) {
        return -1;
    }

    account_perm_delete(db_pool, ledger_id, account_id, target_user_id);
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}
