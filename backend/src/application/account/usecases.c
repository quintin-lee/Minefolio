#include "application/account/usecases.h"
#include "domain/account/rules.h"
#include "domain/account/entity.h"
#include "infrastructure/repositories/account_repo_impl.h"
#include "common/db.h"
#include "common/ledger_utils.h"
#include <stdio.h>
#include <string.h>

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
        /* 重复名 UNIQUE(user_id, ledger_id, name) 冲突 或 数据库错误 */
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

    /* 归属校验 */
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
    csilk_json_free(cur);

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
    /* 归属校验：取当前行，保留既有字段 */
    csilk_json_t* cur = account_get(db_pool, user_id, id);
    if (!cur || csilk_json_array_size(cur) == 0) {
        if (cur) {
            csilk_json_free(cur);
        }
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "账户不存在");
        return -1;
    }

    const csilk_json_t* cr = csilk_json_array_get(cur, 0);
    const char*         name = csilk_json_get_string(cr, "name");
    const char*         institution = csilk_json_get_string(cr, "institution");
    const char*         currency = csilk_json_get_string(cr, "currency");

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
    csilk_json_t* rows =
        account_list(db_pool, user_id, ledger_id, include_archived != 0, out_total);
    if (rows) {
        *out_list = rows;
    }
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
