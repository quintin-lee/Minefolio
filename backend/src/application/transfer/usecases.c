/**
 * @file usecases.c
 * @brief 转账用例编排实现 (Application Transfer Use Cases)
 */

#include "application/transfer/usecases.h"
#include "domain/transfer/rules.h"
#include "domain/transfer/repository.h"
#include "infrastructure/repositories/transfer_repo_impl.h"
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

    /* Begin transaction */
    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "数据库错误");
        return -1;
    }

    /* 1. Insert transfer record */
    mf_transfer_t transfer = {0};
    transfer.user_id = cmd->user_id;
    transfer.from_asset_id = cmd->from_asset_id;
    transfer.to_asset_id = cmd->to_asset_id;
    transfer.amount = cmd->amount;
    strncpy(transfer.currency,
            cmd->currency && cmd->currency[0] ? cmd->currency : "CNY",
            sizeof(transfer.currency) - 1);
    strncpy(transfer.transfer_date, cmd->transfer_date, sizeof(transfer.transfer_date) - 1);
    if (cmd->note) {
        strncpy(transfer.note, cmd->note, sizeof(transfer.note) - 1);
    }

    int64_t transfer_id = mf_transfer_repo_insert(pool, cmd->user_id, &transfer);
    if (transfer_id <= 0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "转账失败");
        return -1;
    }

    const char* cur = cmd->currency && cmd->currency[0] ? cmd->currency : "CNY";
    const char* note = cmd->note ? cmd->note : "";

    /* 2. Record transfer_out transaction */
    if (mf_transfer_repo_insert_out_transaction(
            pool, cmd->user_id, cmd->from_asset_id, cmd->amount, cur, cmd->transfer_date, note) !=
        0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "记录划转交易失败");
        return -1;
    }

    /* 3. Record transfer_in transaction */
    if (mf_transfer_repo_insert_in_transaction(
            pool, cmd->user_id, cmd->to_asset_id, cmd->amount, cur, cmd->transfer_date, note) !=
        0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "记录划转交易失败");
        return -1;
    }

    /* 4. Apply balance via Ledger Engine */
    currency_t cur_t = currency_from_str(cur);
    money_t    amt_m;
    money_from_double(cmd->amount, cur_t, &amt_m);

    if (ledger_apply_transfer(
            pool, cmd->user_id, cmd->from_asset_id, cmd->to_asset_id, amt_m, transfer_id, note) !=
        0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "资产余额更新失败");
        return -1;
    }

    /* Commit */
    csilk_db_exec(pool, "COMMIT");
    out_res->code = 0;
    return 0;
}
