#include "services/transaction_service.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/jwt.h"
#include "common/balance.h"
#include "common/tx_types.h"
#include "core/ledger/ledger_engine.h"
#include "repositories/transaction_repo.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
transactions_create(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id < 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    int64_t     asset_id = db_get_int(body, "asset_id");
    int64_t     linked_asset_id = db_get_int(body, "linked_asset_id");
    int64_t     category_id = db_get_int(body, "category_id");
    const char* type = csilk_json_get_string(body, "transaction_type");
    if (!type || strlen(type) == 0) {
        type = csilk_json_get_string(body, "type");
    }
    const char* src_type = csilk_json_get_string(body, "source_type");
    if (!src_type) {
        src_type = "expense";
    }
    double amount = db_get_num(body, "amount");
    CSILK_LOG_I("transactions_create type=%s asset_id=%lld linked_asset_id=%lld amount=%.2f",
                type ? type : "(null)",
                (long long)asset_id,
                (long long)linked_asset_id,
                amount);
    const char* date = csilk_json_get_string(body, "transaction_date");

    const tx_type_t* ttype = tx_type_lookup(type ? type : "");
    if (!ttype) {
        csilk_json_free(body);
        respond_bad_request(c, "未知交易类型");
        return;
    }

    if (strcmp(src_type, "income") != 0 && strcmp(src_type, "expense") != 0) {
        csilk_json_free(body);
        respond_bad_request(c, "source_type 必须为 income 或 expense");
        return;
    }

    if (asset_id <= 0 || !type || amount <= 0 || !date) {
        csilk_json_free(body);
        respond_bad_request(c, "asset_id、transaction_type、amount、transaction_date 为必填");
        return;
    }

    if (linked_asset_id > 0 && linked_asset_id == asset_id) {
        csilk_json_free(body);
        respond_bad_request(c, "关联资金账户不能与投资目标资产相同");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();

    // Verify main asset belongs to user
    if (!tx_asset_exists(pool, user_id, asset_id)) {
        csilk_json_free(body);
        respond_not_found(c);
        return;
    }

    // Verify linked asset belongs to user if specified
    if (linked_asset_id > 0 && !tx_asset_exists(pool, user_id, linked_asset_id)) {
        csilk_json_free(body);
        respond_bad_request(c, "关联资金账户无效");
        return;
    }

    const char* currency = csilk_json_get_string(body, "currency");
    if (!currency) {
        currency = "CNY";
    }
    const char* note = csilk_json_get_string(body, "note");
    double      price = db_get_num(body, "price_per_unit");
    double      qty = db_get_num(body, "quantity");
    double      fee = db_get_num(body, "fee");
    if (fee < 0) {
        fee = 0;
    }

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "数据库错误");
        return;
    }

    currency_t cur = currency_from_str(currency);
    money_t    amt_m, fee_m;
    price_t    price_p;
    quantity_t qty_q;
    money_from_double(amount, cur, &amt_m);
    money_from_double(fee, cur, &fee_m);
    price_from_double(price, 4, cur, &price_p);
    quantity_from_double(qty, 4, &qty_q);

    ledger_tx_t ltx = {.id = 0,
                       .user_id = user_id,
                       .asset_id = asset_id,
                       .linked_asset_id = linked_asset_id,
                       .category_id = category_id,
                       .type = ledger_tx_type_from_str(type),
                       .type_str = type,
                       .amount = amt_m,
                       .price = price_p,
                       .quantity = qty_q,
                       .fee = fee_m,
                       .tx_date = date,
                       .note = note,
                       .parent_tx_id = 0};

    if (ledger_apply_tx(pool, &ltx) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(body);
        respond_bad_request(c, "持有份额不足或资产处理失败");
        return;
    }

    csilk_db_exec(pool, "COMMIT");
    CSILK_LOG_I("transactions_create committed id=%lld", (long long)ltx.id);
    csilk_json_free(body);
    respond_ok_null(c);
}

void
transactions_update(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }

    int64_t tx_id_val = atoll(id_str);
    CSILK_LOG_I("transactions_update id=%lld", (long long)tx_id_val);

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    // 读取旧记录
    csilk_json_t* old_row = tx_get_old(pool, user_id, atoll(id_str));
    if (!old_row || csilk_json_array_size(old_row) == 0) {
        csilk_json_free(body);
        if (old_row) {
            csilk_json_free(old_row);
        }
        respond_not_found(c);
        return;
    }
    const csilk_json_t* old_r = csilk_json_array_get(old_row, 0);
    int64_t             old_asset_id = db_get_int(old_r, "asset_id");

    int64_t     linked_asset_id = db_get_int(body, "linked_asset_id");
    const char* type = csilk_json_get_string(body, "transaction_type");
    if (!type || strlen(type) == 0) {
        type = csilk_json_get_string(body, "type");
    }

    const tx_type_t* ntype = tx_type_lookup(type ? type : "");
    if (!ntype) {
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_bad_request(c, "未知交易类型");
        return;
    }

    double      amount = db_get_num(body, "amount");
    const char* date = csilk_json_get_string(body, "transaction_date");
    const char* currency = csilk_json_get_string(body, "currency");
    const char* note = csilk_json_get_string(body, "note");
    const char* src_type = csilk_json_get_string(body, "source_type");
    if (src_type && strcmp(src_type, "income") != 0 && strcmp(src_type, "expense") != 0) {
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_bad_request(c, "source_type 必须为 income 或 expense");
        return;
    }
    double  price = db_get_num(body, "price_per_unit");
    double  qty = db_get_num(body, "quantity");
    int64_t category_id = db_get_int(body, "category_id");
    double  fee = db_get_num(body, "fee");
    if (fee < 0) {
        fee = 0;
    }

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_error(c, 500, "数据库错误");
        return;
    }

    // 1. 逆转旧交易的影响
    if (ledger_reverse_tx(pool, user_id, tx_id_val) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_error(c, 500, "旧交易回滚失败");
        return;
    }

    // 2. 更新交易表主记录
    if (!tx_update(pool,
                   user_id,
                   tx_id_val,
                   type ? type : "",
                   ntype->stat_dir,
                   ntype->linked_dir,
                   amount,
                   price,
                   qty,
                   currency ? currency : "CNY",
                   date ? date : "",
                   note ? note : "",
                   category_id,
                   src_type ? src_type : "expense",
                   linked_asset_id)) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_error(c, 500, "更新失败");
        return;
    }

    // 3. 应用新交易状态
    currency_t cur = currency_from_str(currency ? currency : "CNY");
    money_t    amt_m, fee_m;
    price_t    price_p;
    quantity_t qty_q;
    money_from_double(amount, cur, &amt_m);
    money_from_double(fee, cur, &fee_m);
    price_from_double(price, 4, cur, &price_p);
    quantity_from_double(qty, 4, &qty_q);

    ledger_tx_t ltx = {.id = tx_id_val,
                       .user_id = user_id,
                       .asset_id = old_asset_id,
                       .linked_asset_id = linked_asset_id,
                       .category_id = category_id,
                       .type = ledger_tx_type_from_str(type),
                       .type_str = type,
                       .amount = amt_m,
                       .price = price_p,
                       .quantity = qty_q,
                       .fee = fee_m,
                       .tx_date = date,
                       .note = note,
                       .parent_tx_id = 0};

    if (ledger_apply_tx(pool, &ltx) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_bad_request(c, "新交易处理失败或持有份额不足");
        return;
    }

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(body);
    CSILK_LOG_I("transactions_update committed id=%lld", (long long)tx_id_val);
    csilk_json_free(old_row);
    respond_ok_null(c);
}

void
transactions_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }

    int64_t tx_id_val = atoll(id_str);
    CSILK_LOG_I("transactions_delete id=%lld", (long long)tx_id_val);
    csilk_db_pool_t* pool = db_get_pool();

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        respond_error(c, 500, "数据库错误");
        return;
    }

    // 1. 回滚交易及其关联手续费
    if (ledger_reverse_tx(pool, user_id, tx_id_val) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        respond_error(c, 500, "删除失败");
        return;
    }

    // 2. 删除交易记录
    if (!tx_delete(pool, user_id, tx_id_val)) {
        csilk_db_exec(pool, "ROLLBACK");
        respond_error(c, 500, "删除失败");
        return;
    }

    csilk_db_exec(pool, "COMMIT");
    CSILK_LOG_I("transactions_delete committed id=%lld", (long long)tx_id_val);
    respond_ok_null(c);
}
