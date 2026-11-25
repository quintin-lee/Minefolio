/**
 * @file usecases.c
 * @brief 转账用例编排实现 (Application Transfer Use Cases)
 */

#include "application/transfer/usecases.h"
#include "domain/transfer/rules.h"
#include "domain/transfer/repository.h"
#include "infrastructure/repositories/transfer_repo_impl.h"
#include "infrastructure/repositories/asset_repo_impl.h"
#include "application/account/usecases.h"
#include "common/balance.h"
#include "core/financial/money.h"
#include "core/financial/currency.h"
#include "core/ledger/ledger_engine.h"
#include "common/db.h"
#include "common/response.h"
#include <stdio.h>
#include <string.h>

int
transfer_usecase_create(void*                        pool,
                        const create_transfer_cmd_t* cmd,
                        transfer_usecase_result_t*   out_res)
{
    /* Validate required fields */
    if (!mf_transfer_rule_validate_required(
            cmd->from_asset_id, cmd->to_asset_id, cmd->amount, cmd->transfer_date)) {
        out_res->code = 1002;
        snprintf(out_res->message,
                 sizeof(out_res->message),
                 "from_asset_id、to_asset_id、amount、transfer_date 为必填");
        return -1;
    }

    /* Validate different assets */
    if (!mf_transfer_rule_validate_different_assets(cmd->from_asset_id, cmd->to_asset_id)) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "转出和转入资产不能相同");
        return -1;
    }

    /* Validate assets exist and belong to user */
    if (!mf_transfer_repo_asset_check(pool, cmd->user_id, cmd->from_asset_id, cmd->to_asset_id)) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "资产不存在");
        return -1;
    }

    int64_t from_ledger_id = 0, from_acct_id = 0;
    asset_get_ledger_and_account(
        (csilk_db_pool_t*)pool, cmd->from_asset_id, &from_ledger_id, &from_acct_id);
    int64_t to_ledger_id = 0, to_acct_id = 0;
    asset_get_ledger_and_account(
        (csilk_db_pool_t*)pool, cmd->to_asset_id, &to_ledger_id, &to_acct_id);

    if (from_ledger_id > 0 && to_ledger_id > 0 && from_ledger_id != to_ledger_id) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "Forbidden: 不支持跨账本划转");
        return -1;
    }

    int64_t chk_from_lid = from_ledger_id > 0 ? from_ledger_id : cmd->ledger_id;
    if (from_acct_id > 0 || chk_from_lid > 0) {
        int perm_chk = account_usecase_check_permission(
            pool, chk_from_lid, cmd->user_id, from_acct_id, MF_ACCOUNT_ACTION_WRITE_TX);
        if (perm_chk != 0) {
            out_res->code = 1004;
            snprintf(out_res->message, sizeof(out_res->message), "Forbidden: 转出账户无记账权限");
            return -1;
        }
    }
    int64_t chk_to_lid = to_ledger_id > 0 ? to_ledger_id : cmd->ledger_id;
    if (to_acct_id > 0 || chk_to_lid > 0) {
        int perm_chk = account_usecase_check_permission(
            pool, chk_to_lid, cmd->user_id, to_acct_id, MF_ACCOUNT_ACTION_WRITE_TX);
        if (perm_chk != 0) {
            out_res->code = 1004;
            snprintf(out_res->message, sizeof(out_res->message), "Forbidden: 转入账户无记账权限");
            return -1;
        }
    }

    /* Begin transaction */
    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "mf_transfer_create", &scope) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "数据库错误");
        return -1;
    }

    const char* cur = cmd->currency && cmd->currency[0] ? cmd->currency : "CNY";
    const char* note = cmd->note ? cmd->note : "";

    /* 1. Record transfer_out transaction */
    int64_t out_tx_id = mf_transfer_repo_insert_out_transaction(pool,
                                                                cmd->user_id,
                                                                cmd->from_asset_id,
                                                                cmd->to_asset_id,
                                                                cmd->amount,
                                                                cur,
                                                                cmd->transfer_date,
                                                                note,
                                                                cmd->ledger_id);
    if (out_tx_id <= 0) {
        db_tx_scope_rollback(pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "记录划转交易失败");
        return -1;
    }

    /* 2. Record transfer_in transaction (paired with out_tx_id as parent_tx_id) */
    int64_t in_tx_id = mf_transfer_repo_insert_in_transaction(pool,
                                                              cmd->user_id,
                                                              cmd->to_asset_id,
                                                              cmd->from_asset_id,
                                                              cmd->amount,
                                                              cur,
                                                              cmd->transfer_date,
                                                              note,
                                                              out_tx_id,
                                                              cmd->ledger_id);
    if (in_tx_id <= 0) {
        db_tx_scope_rollback(pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "记录划转交易失败");
        return -1;
    }

    /* 3. Insert transfer record with explicit from_tx_id and to_tx_id */
    mf_transfer_t transfer = {0};
    transfer.user_id = cmd->user_id;
    transfer.from_asset_id = cmd->from_asset_id;
    transfer.to_asset_id = cmd->to_asset_id;
    transfer.amount = cmd->amount;
    transfer.ledger_id = cmd->ledger_id;
    transfer.from_tx_id = out_tx_id;
    transfer.to_tx_id = in_tx_id;
    strncpy(transfer.currency, cur, sizeof(transfer.currency) - 1);
    strncpy(transfer.transfer_date, cmd->transfer_date, sizeof(transfer.transfer_date) - 1);
    if (cmd->note) {
        strncpy(transfer.note, cmd->note, sizeof(transfer.note) - 1);
    }

    int64_t transfer_id = mf_transfer_repo_insert(pool, cmd->user_id, &transfer);
    if (transfer_id <= 0) {
        db_tx_scope_rollback(pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "转账失败");
        return -1;
    }

    /* 4. Apply balance via Ledger Engine */
    currency_t cur_t = currency_from_str(cur);
    money_t    amt_m;
    money_from_double(cmd->amount, cur_t, &amt_m);

    if (ledger_apply_transfer_dated(pool,
                                    cmd->user_id,
                                    cmd->from_asset_id,
                                    cmd->to_asset_id,
                                    amt_m,
                                    transfer_id,
                                    note,
                                    cmd->transfer_date) != 0) {
        db_tx_scope_rollback(pool, &scope);
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "资产余额更新失败");
        return -1;
    }

    /* Commit */
    if (db_tx_scope_commit(pool, &scope) != 0) {
        db_tx_scope_rollback(pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "提交事务失败");
        return -1;
    }
    out_res->code = 0;
    return 0;
}
