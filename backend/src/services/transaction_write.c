#include "services/transaction_service.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/jwt.h"
#include "common/balance.h"
#include "common/tx_types.h"
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
    char             uid_str[32], ast_str[32], last_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)asset_id);
    snprintf(last_str, sizeof(last_str), "%lld", (long long)linked_asset_id);

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

    char cat_str[32], amt_str[64], price_str[64], qty_str[64];
    snprintf(cat_str, sizeof(cat_str), "%lld", (long long)category_id);
    snprintf(amt_str, sizeof(amt_str), "%.6f", amount);
    snprintf(price_str, sizeof(price_str), "%.4f", price);
    snprintf(qty_str, sizeof(qty_str), "%.4f", qty);

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "数据库错误");
        return;
    }

    char fee_str[64];
    snprintf(fee_str, sizeof(fee_str), "%.6f", fee);
    const char* ins_params_full[] = {uid_str,
                                     ast_str,
                                     last_str,
                                     cat_str,
                                     src_type,
                                     type,
                                     ttype->stat_dir,
                                     ttype->linked_dir,
                                     amt_str,
                                     price_str,
                                     qty_str,
                                     fee_str,
                                     currency,
                                     date,
                                     note ? note : "",
                                     NULL};

    csilk_json_t* ins = csilk_db_query_param_json(
        pool,
        "INSERT INTO transactions (user_id, asset_id, linked_asset_id, category_id, source_type, "
        "transaction_type, "
        "direction, linked_direction, "
        "amount, price_per_unit, quantity, fee, currency, transaction_date, note) "
        "VALUES (?, ?, NULLIF(?, '0'), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id",
        ins_params_full);
    if (!ins || csilk_json_array_size(ins) == 0) {
        csilk_db_exec(pool, "ROLLBACK");
        if (ins) {
            csilk_json_free(ins);
        }
        csilk_json_free(body);
        respond_error(c, 500, "创建失败");
        return;
    }
    int64_t tx_id = db_get_int(csilk_json_array_get(ins, 0), "id");
    csilk_json_free(ins);

    // 持仓联动：仅 buy/sell 且投资类资产
    CSILK_LOG_I(
        "transactions_create inserted id=%lld type=%s", (long long)tx_id, type ? type : "(null)");
    double position_delta = 0;
    int    is_investment_tx = 0;
    if (strcmp(type, "buy") == 0 || strcmp(type, "sell") == 0) {
        int pos_rc = apply_position(pool, asset_id, type, amount, fee, price, qty, &position_delta);
        if (pos_rc < 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            respond_bad_request(c, "持有份额不足");
            return;
        }
        is_investment_tx = (position_delta != 0 || strcmp(type, "buy") == 0);
    }

    // 手续费：同事务生成 fee 行（仅投资类 buy/sell）
    if (fee > 0 && linked_asset_id > 0 && is_investment_tx) {
        char fee_amt_str[64];
        snprintf(fee_amt_str, sizeof(fee_amt_str), "%.6f", fee);
        const char*   fee_note = (note && strlen(note) > 0) ? note : "fee";
        const char*   fee_params[] = {uid_str,
                                      ast_str,
                                      last_str,
                                      cat_str,
                                      src_type,
                                      "fee",
                                      "out",
                                      fee_amt_str,
                                      "0.0000",
                                      "0.0000",
                                      currency,
                                      date,
                                      fee_note,
                                      NULL};
        csilk_json_t* fee_res = csilk_db_query_param_json(
            pool,
            "INSERT INTO transactions (user_id, asset_id, linked_asset_id, category_id, "
            "source_type, transaction_type, direction, linked_direction, "
            "amount, price_per_unit, quantity, currency, transaction_date, note) "
            "VALUES (?, ?, NULLIF(?, '0'), ?, ?, ?, ?, NULL, ?, ?, ?, ?, ?, ?)",
            fee_params);
        if (fee_res) {
            csilk_json_free(fee_res);
        }
        if (balance_apply_delta(
                pool, linked_asset_id, user_id, -fee, "transaction_fee", tx_id, note) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            respond_bad_request(c, "手续费扣减失败");
            return;
        }
    }

    // 目标资产余额联动（非投资类走原逻辑，投资类已在 apply_position 内处理）
    double tdelta = is_investment_tx ? 0 : tx_delta(type, amount, price, qty);
    if (is_investment_tx) {
        if (balance_apply_delta(
                pool, asset_id, user_id, position_delta, "transaction", tx_id, note) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            respond_bad_request(c, "资产余额更新失败");
            return;
        }
    } else if (tdelta != 0) {
        if (balance_apply_delta(pool, asset_id, user_id, tdelta, "transaction", tx_id, note) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            respond_bad_request(c, "资产无效");
            return;
        }
    }

    // 关联资金账户余额联动
    if (linked_asset_id > 0) {
        double ldelta = tx_effective_ldelta(type, amount, tdelta);
        if (ldelta != 0) {
            if (balance_apply_delta(
                    pool, linked_asset_id, user_id, ldelta, "transaction_linked", tx_id, note) !=
                0) {
                csilk_db_exec(pool, "ROLLBACK");
                csilk_json_free(body);
                respond_bad_request(c, "关联资金账户余额更新失败");
                return;
            }
        }
    }

    csilk_db_exec(pool, "COMMIT");
    CSILK_LOG_I("transactions_create committed id=%lld", (long long)tx_id);
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
    int64_t             old_linked_asset_id = db_get_int(old_r, "linked_asset_id");
    double              old_tx_amount = db_get_num(old_r, "amount");
    const char*         old_tx_type = csilk_json_get_string(old_r, "transaction_type");
    double              old_tx_price = db_get_num(old_r, "price_per_unit");
    double              old_tx_qty = db_get_num(old_r, "quantity");
    double              old_fee = db_get_num(old_r, "fee");
    double              old_tdelta = tx_delta(old_tx_type, old_tx_amount, old_tx_price, old_tx_qty);
    double              old_ldelta = tx_effective_ldelta(old_tx_type, old_tx_amount, old_tdelta);

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

    char amt_str[64], price_str[64], qty_str[64], cat_str[32], last_str[32];
    snprintf(amt_str, sizeof(amt_str), "%.6f", amount);
    snprintf(price_str, sizeof(price_str), "%.4f", price);
    snprintf(qty_str, sizeof(qty_str), "%.4f", qty);
    snprintf(cat_str, sizeof(cat_str), "%lld", (long long)category_id);
    snprintf(last_str, sizeof(last_str), "%lld", (long long)linked_asset_id);

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_error(c, 500, "数据库错误");
        return;
    }

    if (!tx_update(pool,
                   user_id,
                   atoll(id_str),
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

    double new_tdelta = tx_delta(type ? type : "", amount, price, qty);
    int    old_is_invest = (strcmp(old_tx_type, "buy") == 0 || strcmp(old_tx_type, "sell") == 0);
    int    new_is_invest =
        (strcmp(type ? type : "", "buy") == 0 || strcmp(type ? type : "", "sell") == 0);
    if (old_is_invest) {
        double old_pos_delta = 0;
        rollback_position(pool,
                          old_asset_id,
                          old_tx_type,
                          old_tx_amount,
                          old_fee,
                          old_tx_price,
                          old_tx_qty,
                          &old_pos_delta);
        if (old_pos_delta != 0 &&
            balance_apply_delta(
                pool, old_asset_id, user_id, old_pos_delta, "transaction", tx_id_val, note) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            csilk_json_free(old_row);
            respond_bad_request(c, "资产无效");
            return;
        }
    } else if (old_tdelta != 0) {
        if (balance_apply_delta(
                pool, old_asset_id, user_id, -old_tdelta, "transaction", tx_id_val, note) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            csilk_json_free(old_row);
            respond_bad_request(c, "资产无效");
            return;
        }
    }
    if (new_is_invest) {
        double new_pos_delta = 0;
        int    new_rc = apply_position(
            pool, old_asset_id, type ? type : "", amount, fee, price, qty, &new_pos_delta);
        if (new_rc < 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            csilk_json_free(old_row);
            respond_bad_request(c, "持有份额不足");
            return;
        }
        if (new_pos_delta != 0 &&
            balance_apply_delta(
                pool, old_asset_id, user_id, new_pos_delta, "transaction", tx_id_val, note) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            csilk_json_free(old_row);
            respond_bad_request(c, "资产无效");
            return;
        }
    } else if (new_tdelta != 0) {
        if (balance_apply_delta(
                pool, old_asset_id, user_id, new_tdelta, "transaction", tx_id_val, note) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            csilk_json_free(old_row);
            respond_bad_request(c, "资产无效");
            return;
        }
    }

    // 2. 关联资金账户差值联动
    double new_ldelta = tx_effective_ldelta(type ? type : "", amount, new_tdelta);
    if (linked_asset_id == old_linked_asset_id) {
        if (linked_asset_id > 0) {
            double ldiff = new_ldelta - old_ldelta;
            if (ldiff != 0) {
                if (balance_apply_delta(pool,
                                        linked_asset_id,
                                        user_id,
                                        ldiff,
                                        "transaction_linked",
                                        tx_id_val,
                                        note) != 0) {
                    csilk_db_exec(pool, "ROLLBACK");
                    csilk_json_free(body);
                    csilk_json_free(old_row);
                    respond_bad_request(c, "关联资金账户更新失败");
                    return;
                }
            }
        }
    } else {
        if (old_linked_asset_id > 0 && old_ldelta != 0) {
            if (balance_apply_delta(pool,
                                    old_linked_asset_id,
                                    user_id,
                                    -old_ldelta,
                                    "transaction_linked",
                                    tx_id_val,
                                    note) != 0) {
                csilk_db_exec(pool, "ROLLBACK");
                csilk_json_free(body);
                csilk_json_free(old_row);
                respond_bad_request(c, "原资金账户更新失败");
                return;
            }
        }
        if (linked_asset_id > 0 && new_ldelta != 0) {
            if (balance_apply_delta(pool,
                                    linked_asset_id,
                                    user_id,
                                    new_ldelta,
                                    "transaction_linked",
                                    tx_id_val,
                                    note) != 0) {
                csilk_db_exec(pool, "ROLLBACK");
                csilk_json_free(body);
                csilk_json_free(old_row);
                respond_bad_request(c, "新资金账户更新失败");
                return;
            }
        }
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

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }

    int64_t tx_id_val = atoll(id_str);

    CSILK_LOG_I("transactions_delete id=%lld", (long long)tx_id_val);
    csilk_db_pool_t* pool = db_get_pool();
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    // 读取旧记录
    const char*   old_params[] = {id_str, uid_str, NULL};
    csilk_json_t* old_row = csilk_db_query_param_json(
        pool,
        "SELECT asset_id, linked_asset_id, amount, transaction_type, quantity, price_per_unit "
        "FROM transactions WHERE id=? AND user_id=?",
        old_params);
    if (!old_row || csilk_json_array_size(old_row) == 0) {
        if (old_row) {
            csilk_json_free(old_row);
        }
        respond_not_found(c);
        return;
    }
    const csilk_json_t* old_r = csilk_json_array_get(old_row, 0);
    int64_t             asset_id = db_get_int(old_r, "asset_id");
    int64_t             linked_asset_id = db_get_int(old_r, "linked_asset_id");
    const char*         old_tx_type = csilk_json_get_string(old_r, "transaction_type");
    double              old_tx_amount = db_get_num(old_r, "amount");
    double              old_tx_price = db_get_num(old_r, "price_per_unit");
    double              old_tx_qty = db_get_num(old_r, "quantity");
    double              old_fee = db_get_num(old_r, "fee");
    double              old_tdelta = tx_delta(old_tx_type, old_tx_amount, old_tx_price, old_tx_qty);
    double              old_ldelta = tx_effective_ldelta(old_tx_type, old_tx_amount, old_tdelta);

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(old_row);
        respond_error(c, 500, "数据库错误");
        return;
    }

    if (!tx_delete(pool, user_id, atoll(id_str))) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(old_row);
        respond_error(c, 500, "删除失败");
        return;
    }

    // 投资类：回滚持仓
    if (strcmp(old_tx_type, "buy") == 0 || strcmp(old_tx_type, "sell") == 0) {
        double old_pos_delta = 0;
        rollback_position(pool,
                          asset_id,
                          old_tx_type,
                          old_tx_amount,
                          old_fee,
                          old_tx_price,
                          old_tx_qty,
                          &old_pos_delta);
        if (old_pos_delta != 0 &&
            balance_apply_delta(
                pool, asset_id, user_id, old_pos_delta, "transaction", tx_id_val, NULL) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(old_row);
            respond_error(c, 500, "删除失败");
            return;
        }
    }

    // 1. 反转目标资产旧 delta（投资类已由 apply_position 处理，非投资类走此处）
    if (old_tdelta != 0 && !(strcmp(old_tx_type, "buy") == 0 || strcmp(old_tx_type, "sell") == 0)) {
        if (balance_apply_delta(
                pool, asset_id, user_id, -old_tdelta, "transaction", tx_id_val, NULL) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(old_row);
            respond_error(c, 500, "删除失败");
            return;
        }
    }

    // 2. 反转关联资金账户旧 delta
    if (linked_asset_id > 0 && old_ldelta != 0) {
        if (balance_apply_delta(pool,
                                linked_asset_id,
                                user_id,
                                -old_ldelta,
                                "transaction_linked",
                                tx_id_val,
                                NULL) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(old_row);
            respond_error(c, 500, "删除失败");
            return;
        }
    }

    csilk_db_exec(pool, "COMMIT");
    CSILK_LOG_I("transactions_delete committed id=%lld", (long long)tx_id_val);
    csilk_json_free(old_row);
    respond_ok_null(c);
}
