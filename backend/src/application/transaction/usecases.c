#include "application/transaction/usecases.h"
#include "domain/transaction/rules.h"
#include "domain/transaction/repository.h"
#include "core/ledger/ledger_engine.h"
#include "core/financial/currency.h"
#include "repositories/transaction_repo.h"
#include "common/tx_types.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
tx_usecase_create(void* pool, const create_tx_cmd_t* cmd, tx_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));

    if (!cmd || cmd->user_id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "无效的用户上下文");
        return -1;
    }

    const tx_type_t* ttype = tx_type_lookup(cmd->type ? cmd->type : "");
    if (!ttype) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "未知交易类型");
        return -1;
    }

    if (cmd->source_type && strcmp(cmd->source_type, "income") != 0 &&
        strcmp(cmd->source_type, "expense") != 0) {
        out_res->code = 1002;
        snprintf(
            out_res->message, sizeof(out_res->message), "source_type 必须为 income 或 expense");
        return -1;
    }

    if (cmd->asset_id <= 0 || !cmd->type || cmd->amount <= 0 || !cmd->date) {
        out_res->code = 1002;
        snprintf(out_res->message,
                 sizeof(out_res->message),
                 "asset_id、transaction_type、amount、transaction_date 为必填");
        return -1;
    }

    if (cmd->linked_asset_id > 0 && cmd->linked_asset_id == cmd->asset_id) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "关联资金账户不能与投资目标资产相同");
        return -1;
    }

    /* 校验标的资产存在性 */
    if (!tx_asset_exists(pool, cmd->user_id, cmd->asset_id)) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "标的资产不存在");
        return -1;
    }

    if (cmd->linked_asset_id > 0 && !tx_asset_exists(pool, cmd->user_id, cmd->linked_asset_id)) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "关联资金账户无效");
        return -1;
    }

    const char* currency = (cmd->currency && cmd->currency[0]) ? cmd->currency : "CNY";
    double      fee = (cmd->fee > 0) ? cmd->fee : 0.0;

    currency_t cur = currency_from_str(currency);
    money_t    amt_m, fee_m;
    price_t    price_p;
    quantity_t qty_q;
    money_from_double(cmd->amount, cur, &amt_m);
    money_from_double(fee, cur, &fee_m);
    price_from_double(cmd->price, 4, cur, &price_p);
    quantity_from_double(cmd->quantity, 4, &qty_q);

    /* 构造领域实体并执行领域规则校验 */
    mf_transaction_t domain_tx = {0};
    domain_tx.user_id = cmd->user_id;
    domain_tx.asset_id = cmd->asset_id;
    domain_tx.account_id = cmd->linked_asset_id;
    domain_tx.amount = qty_q;
    domain_tx.price = price_p;
    domain_tx.fee = fee_m;
    snprintf(domain_tx.type, sizeof(domain_tx.type), "%s", cmd->type);
    snprintf(domain_tx.fee_currency, sizeof(domain_tx.fee_currency), "%s", currency);
    if (cmd->note) {
        snprintf(domain_tx.note, sizeof(domain_tx.note), "%s", cmd->note);
    }
    if (cmd->date) {
        snprintf(domain_tx.tx_time, sizeof(domain_tx.tx_time), "%s", cmd->date);
    }

    char rule_err[256] = {0};
    if (mf_tx_rule_validate(&domain_tx, rule_err, sizeof(rule_err)) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message,
                 sizeof(out_res->message),
                 "%s",
                 rule_err[0] ? rule_err : "业务规则校验失败");
        return -1;
    }

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "数据库错误");
        return -1;
    }

    ledger_tx_t ltx = {
        .id = 0,
        .user_id = cmd->user_id,
        .asset_id = cmd->asset_id,
        .linked_asset_id = cmd->linked_asset_id,
        .category_id = cmd->category_id,
        .type = ledger_tx_type_from_str(cmd->type),
        .type_str = cmd->type,
        .amount = amt_m,
        .price = price_p,
        .quantity = qty_q,
        .fee = fee_m,
        .tx_date = cmd->date,
        .note = cmd->note,
        .parent_tx_id = 0,
    };

    if (ledger_apply_tx(pool, &ltx) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "持有份额不足或资产处理失败");
        return -1;
    }

    csilk_db_exec(pool, "COMMIT");
    out_res->code = 0;
    out_res->created_id = ltx.id;
    snprintf(out_res->message, sizeof(out_res->message), "OK");
    return 0;
}

int
tx_usecase_update(void* pool, const update_tx_cmd_t* cmd, tx_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));

    if (!cmd || cmd->user_id <= 0 || cmd->tx_id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "无效的更新参数");
        return -1;
    }

    csilk_json_t* old_rows = tx_get_old((csilk_db_pool_t*)pool, cmd->user_id, cmd->tx_id);
    if (!old_rows || csilk_json_array_size(old_rows) == 0) {
        if (old_rows) {
            csilk_json_free(old_rows);
        }
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "未找到交易记录");
        return -1;
    }
    const csilk_json_t* old_row = csilk_json_array_get(old_rows, 0);

    int64_t     old_asset_id = db_get_int(old_row, "asset_id");
    const char* type = (cmd->type && cmd->type[0])
                           ? cmd->type
                           : csilk_json_get_string(old_row, "transaction_type");
    if (!type || strlen(type) == 0) {
        type = csilk_json_get_string(old_row, "type");
    }

    const tx_type_t* ttype = tx_type_lookup(type ? type : "");
    if (!ttype) {
        csilk_json_free(old_rows);
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "未知交易类型");
        return -1;
    }

    const char* currency = (cmd->currency && cmd->currency[0])
                               ? cmd->currency
                               : csilk_json_get_string(old_row, "currency");
    if (!currency) {
        currency = "CNY";
    }

    double      amount = (cmd->amount > 0) ? cmd->amount : db_get_num(old_row, "amount");
    double      price = (cmd->price > 0) ? cmd->price : db_get_num(old_row, "price_per_unit");
    double      qty = (cmd->quantity > 0) ? cmd->quantity : db_get_num(old_row, "quantity");
    double      fee = (cmd->fee >= 0) ? cmd->fee : db_get_num(old_row, "fee");
    const char* date = (cmd->date && cmd->date[0])
                           ? cmd->date
                           : csilk_json_get_string(old_row, "transaction_date");
    const char* note = cmd->note ? cmd->note : csilk_json_get_string(old_row, "note");

    if (csilk_db_exec((csilk_db_pool_t*)pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(old_rows);
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "数据库错误");
        return -1;
    }

    /* 1. 回滚原有交易 */
    if (ledger_reverse_tx((csilk_db_pool_t*)pool, cmd->user_id, cmd->tx_id) != 0) {
        csilk_db_exec((csilk_db_pool_t*)pool, "ROLLBACK");
        csilk_json_free(old_rows);
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "原交易回滚失败");
        return -1;
    }

    /* 2. 应用新交易状态 */
    currency_t cur = currency_from_str(currency);
    money_t    amt_m, fee_m;
    price_t    price_p;
    quantity_t qty_q;
    money_from_double(amount, cur, &amt_m);
    money_from_double(fee, cur, &fee_m);
    price_from_double(price, 4, cur, &price_p);
    quantity_from_double(qty, 4, &qty_q);

    ledger_tx_t ltx = {
        .id = cmd->tx_id,
        .user_id = cmd->user_id,
        .asset_id = old_asset_id,
        .linked_asset_id = cmd->linked_asset_id,
        .category_id = cmd->category_id,
        .type = ledger_tx_type_from_str(type),
        .type_str = type,
        .amount = amt_m,
        .price = price_p,
        .quantity = qty_q,
        .fee = fee_m,
        .tx_date = date,
        .note = note,
        .parent_tx_id = 0,
    };

    if (ledger_apply_tx((csilk_db_pool_t*)pool, &ltx) != 0) {
        csilk_db_exec((csilk_db_pool_t*)pool, "ROLLBACK");
        csilk_json_free(old_rows);
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "新交易处理失败或持有份额不足");
        return -1;
    }

    csilk_db_exec((csilk_db_pool_t*)pool, "COMMIT");
    csilk_json_free(old_rows);
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "OK");
    return 0;
}

int
tx_usecase_delete(void* pool, const delete_tx_cmd_t* cmd, tx_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));

    if (!cmd || cmd->user_id <= 0 || cmd->tx_id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "无效的删除参数");
        return -1;
    }

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "数据库错误");
        return -1;
    }

    /* 1. 回滚交易及其关联手续费 (状态还原) */
    if (ledger_reverse_tx(pool, cmd->user_id, cmd->tx_id) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "删除回滚失败");
        return -1;
    }

    /* 2. 物理删除交易记录 */
    if (!tx_delete(pool, cmd->user_id, cmd->tx_id)) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "删除失败");
        return -1;
    }

    csilk_db_exec(pool, "COMMIT");
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "OK");
    return 0;
}

int
tx_usecase_query(void* pool, const query_tx_filter_t* filter, tx_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));

    if (!filter || filter->user_id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "无效的用户上下文");
        return -1;
    }

    char        aid_str[32] = {0};
    char        cid_str[32] = {0};
    const char* aid_ptr = NULL;
    const char* cid_ptr = NULL;
    if (filter->asset_id > 0) {
        snprintf(aid_str, sizeof(aid_str), "%lld", (long long)filter->asset_id);
        aid_ptr = aid_str;
    }
    if (filter->category_id > 0) {
        snprintf(cid_str, sizeof(cid_str), "%lld", (long long)filter->category_id);
        cid_ptr = cid_str;
    }

    int64_t       total = 0;
    csilk_json_t* list = tx_list((csilk_db_pool_t*)pool,
                                 filter->user_id,
                                 filter->page,
                                 filter->page_size,
                                 aid_ptr,
                                 cid_ptr,
                                 filter->type,
                                 filter->source_type,
                                 filter->start_date,
                                 filter->end_date,
                                 &total);

    out_res->code = 0;
    out_res->total = total;
    out_res->data_payload = list;
    snprintf(out_res->message, sizeof(out_res->message), "OK");
    return 0;
}

int
tx_usecase_monthly(void* pool, int64_t user_id, const char* month, tx_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));

    if (user_id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "无效的用户上下文");
        return -1;
    }

    if (!month || strlen(month) != 7 || month[4] != '-') {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "month 参数格式错误 (YYYY-MM)");
        return -1;
    }
    for (int i = 0; i < 7; i++) {
        if (i != 4 && (month[i] < '0' || month[i] > '9')) {
            out_res->code = 1002;
            snprintf(out_res->message, sizeof(out_res->message), "month 参数格式错误 (YYYY-MM)");
            return -1;
        }
    }

    char pattern[16];
    snprintf(pattern, sizeof(pattern), "%.4s-%.2s%%", month, month + 5);

    csilk_json_t* res = tx_monthly((csilk_db_pool_t*)pool, user_id, pattern);
    if (!res) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "查询失败");
        return -1;
    }

    csilk_json_t* resp = csilk_json_object();
    if (csilk_json_array_size(res) > 0) {
        const csilk_json_t* row = csilk_json_array_get(res, 0);
        csilk_json_add_number(resp, "total_volume", db_get_num(row, "total_volume"));
        csilk_json_add_number(resp, "inflows", db_get_num(row, "inflows"));
        csilk_json_add_number(resp, "outflows", db_get_num(row, "outflows"));
        csilk_json_add_number(resp, "count", db_get_num(row, "count"));
    } else {
        csilk_json_add_number(resp, "total_volume", 0);
        csilk_json_add_number(resp, "inflows", 0);
        csilk_json_add_number(resp, "outflows", 0);
        csilk_json_add_number(resp, "count", 0);
    }
    csilk_json_free(res);

    out_res->code = 0;
    out_res->data_payload = resp;
    snprintf(out_res->message, sizeof(out_res->message), "OK");
    return 0;
}
