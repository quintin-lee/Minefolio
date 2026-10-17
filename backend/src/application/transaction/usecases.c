#include "application/transaction/usecases.h"
#include "domain/transaction/rules.h"
#include "domain/transaction/repository.h"
#include "core/ledger/ledger_engine.h"
#include "core/financial/currency.h"
#include "infrastructure/repositories/transaction_repo_impl.h"
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
        .ledger_id = cmd->ledger_id,
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

    int64_t target_asset_id = (cmd->asset_id > 0) ? cmd->asset_id : old_asset_id;
    bool    type_supports_linked = (strcmp(type, "buy") == 0 || strcmp(type, "sell") == 0 ||
                                    strcmp(type, "transfer_in") == 0 ||
                                    strcmp(type, "transfer_out") == 0 || strcmp(type, "fee") == 0);
    int64_t target_linked_id = 0;
    if (type_supports_linked) {
        target_linked_id = (cmd->linked_asset_id > 0)
                               ? cmd->linked_asset_id
                               : (int64_t)db_get_int(old_row, "linked_asset_id");
    }
    int64_t target_cat_id =
        (cmd->category_id > 0) ? cmd->category_id : (int64_t)db_get_int(old_row, "category_id");
    int64_t target_ledger_id =
        (cmd->ledger_id > 0) ? cmd->ledger_id : (int64_t)db_get_int(old_row, "ledger_id");

    /* 校验目标资产存在性 */
    if (!tx_asset_exists(pool, cmd->user_id, target_asset_id)) {
        csilk_json_free(old_rows);
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "目标资产不存在");
        return -1;
    }

    if (target_linked_id > 0 && !tx_asset_exists(pool, cmd->user_id, target_linked_id)) {
        csilk_json_free(old_rows);
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "关联资金账户无效");
        return -1;
    }

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
        .ledger_id = target_ledger_id,
        .asset_id = target_asset_id,
        .linked_asset_id = target_linked_id,
        .category_id = target_cat_id,
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

    /* 0. 查询该笔交易的类型与关联属性 */
    char uid_str[32], tid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)cmd->user_id);
    snprintf(tid_str, sizeof(tid_str), "%lld", (long long)cmd->tx_id);

    csilk_json_t* tx_rows = csilk_db_query_param_json(
        pool,
        "SELECT id, transaction_type, parent_tx_id, asset_id, linked_asset_id, amount "
        "FROM transactions WHERE id=? AND user_id=?",
        (const char*[]){tid_str, uid_str, NULL});
    if (!tx_rows || csilk_json_array_size(tx_rows) == 0) {
        if (tx_rows) {
            csilk_json_free(tx_rows);
        }
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "交易不存在");
        return -1;
    }

    csilk_json_t* tx_row = csilk_json_array_get(tx_rows, 0);
    const char*   type = csilk_json_get_string(tx_row, "transaction_type");
    int64_t       parent_tx_id = (int64_t)db_get_int(tx_row, "parent_tx_id");
    int64_t       asset_id = (int64_t)db_get_int(tx_row, "asset_id");
    int64_t       linked_asset_id = (int64_t)db_get_int(tx_row, "linked_asset_id");
    double        amount = db_get_num(tx_row, "amount");

    int is_transfer_out = (type && strcmp(type, "transfer_out") == 0);
    int is_transfer_in = (type && strcmp(type, "transfer_in") == 0);

    /* 转账配对不变式：
       - 权威配对键是 parent_tx_id：创建路径恒定将 transfer_in.parent_tx_id 写为
         transfer_out.id（见 application/transfer/usecases.c 记录 in 交易处）。
         删除时优先据此定位主/子单（下方 parent_tx_id=? / main_out_id=parent_tx_id）。
       - 仅在「历史遗留行」（引入 parent_tx_id 之前落库、值 <= 0）时，才退化到
         (transaction_type + asset_id + linked_asset_id + amount) 属性匹配兜底。
       - transfers 辅助表在 V014 引入显式双向外键 (from_tx_id, to_tx_id) 建立强关联：
         删除时优先按外键 (from_tx_id=? OR to_tx_id=?) 精准删除；
         若无受影响记录（历史未回填外键的遗留行），才退化到属性匹配兜底。
       去掉上述兜底会孤立遗留 transfer_in、漏回滚余额，故此处维持现状。 */
    if (is_transfer_out) {
        char laid_str[32], aid_str[32], amt_str[64];
        snprintf(laid_str, sizeof(laid_str), "%lld", (long long)linked_asset_id);
        snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
        snprintf(amt_str, sizeof(amt_str), "%.6f", amount);

        /* 1. transfer_out 作为转账主单，执行状态回滚 (同时还原转出和转入资产余额) */
        if (ledger_reverse_tx(pool, cmd->user_id, cmd->tx_id) != 0) {
            csilk_json_free(tx_rows);
            csilk_db_exec(pool, "ROLLBACK");
            out_res->code = 1004;
            snprintf(out_res->message, sizeof(out_res->message), "删除回滚失败");
            return -1;
        }

        /* 2. 先清理 transfers 辅助流水 (子表)，优先精准按外键删除，兼容兜底历史遗留行 */
        csilk_json_t* del_res = csilk_db_query_param_json(
            pool,
            "DELETE FROM transfers WHERE user_id=? AND (from_tx_id=? OR to_tx_id=?) RETURNING id",
            (const char*[]){uid_str, tid_str, tid_str, NULL});
        if (!del_res || csilk_json_array_size(del_res) == 0) {
            // 历史遗留行兜底（未回填外键的数据）
            csilk_json_free(csilk_db_query_param_json(
                pool,
                "DELETE FROM transfers WHERE id IN ("
                "  SELECT id FROM transfers WHERE user_id=? AND "
                "from_asset_id=? AND to_asset_id=? AND amount=? "
                "  ORDER BY id DESC LIMIT 1)",
                (const char*[]){uid_str, aid_str, laid_str, amt_str, NULL}));
        }
        if (del_res) {
            csilk_json_free(del_res);
        }

        /* 3. 清理配对的 transfer_in 子单 (优先权威 parent_tx_id，未关联时才兜底) */
        csilk_json_t* in_del = csilk_db_query_param_json(
            pool,
            "DELETE FROM transactions WHERE user_id=? AND parent_tx_id=? RETURNING id",
            (const char*[]){uid_str, tid_str, NULL});
        if (!in_del || csilk_json_array_size(in_del) == 0) {
            csilk_json_free(csilk_db_query_param_json(
                pool,
                "DELETE FROM transactions WHERE id IN ("
                "  SELECT id FROM transactions WHERE user_id=? AND transaction_type='transfer_in' "
                "  AND asset_id=? AND linked_asset_id=? AND amount=? ORDER BY id DESC LIMIT 1)",
                (const char*[]){uid_str, laid_str, aid_str, amt_str, NULL}));
        }
        if (in_del) {
            csilk_json_free(in_del);
        }

        /* 4. 物理删除 transfer_out 主记录 */
        if (!tx_delete(pool, cmd->user_id, cmd->tx_id)) {
            csilk_json_free(tx_rows);
            csilk_db_exec(pool, "ROLLBACK");
            out_res->code = 1004;
            snprintf(out_res->message, sizeof(out_res->message), "删除失败");
            return -1;
        }
    } else if (is_transfer_in) {
        /* 若删除的是 transfer_in: 寻找配对的 transfer_out 主单执行回滚 */
        int64_t main_out_id = parent_tx_id;
        char    maid_str[32] = {0};
        char    laid_str[32], aid_str[32], amt_str[64];
        snprintf(laid_str, sizeof(laid_str), "%lld", (long long)linked_asset_id);
        snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
        snprintf(amt_str, sizeof(amt_str), "%.6f", amount);

        if (main_out_id <= 0) {
            // 没有 parent_tx_id，尝试按对等属性查找 transfer_out
            csilk_json_t* f_rows = csilk_db_query_param_json(
                pool,
                "SELECT id FROM transactions WHERE user_id=? AND transaction_type='transfer_out' "
                "AND asset_id=? AND linked_asset_id=? AND amount=? ORDER BY id DESC LIMIT 1",
                (const char*[]){uid_str, laid_str, aid_str, amt_str, NULL});
            if (f_rows && csilk_json_array_size(f_rows) > 0) {
                main_out_id = (int64_t)db_get_int(csilk_json_array_get(f_rows, 0), "id");
            }
            if (f_rows) {
                csilk_json_free(f_rows);
            }
        }

        if (main_out_id > 0) {
            snprintf(maid_str, sizeof(maid_str), "%lld", (long long)main_out_id);
            if (ledger_reverse_tx(pool, cmd->user_id, main_out_id) != 0) {
                csilk_json_free(tx_rows);
                csilk_db_exec(pool, "ROLLBACK");
                out_res->code = 1004;
                snprintf(out_res->message, sizeof(out_res->message), "删除回滚失败");
                return -1;
            }
            // 先清理 transfers 辅助流水 (子表)，优先按主单或子单 ID 精准删除
            const char*   target_tx_id = (main_out_id > 0) ? maid_str : tid_str;
            csilk_json_t* del_res = csilk_db_query_param_json(
                pool,
                "DELETE FROM transfers WHERE user_id=? AND (from_tx_id=? OR to_tx_id=?) RETURNING "
                "id",
                (const char*[]){uid_str, target_tx_id, target_tx_id, NULL});
            if (!del_res || csilk_json_array_size(del_res) == 0) {
                csilk_json_free(csilk_db_query_param_json(
                    pool,
                    "DELETE FROM transfers WHERE id IN ("
                    "  SELECT id FROM transfers WHERE user_id=? AND from_asset_id=? AND "
                    "to_asset_id=? "
                    "AND amount=? "
                    "  ORDER BY id DESC LIMIT 1)",
                    (const char*[]){uid_str, laid_str, aid_str, amt_str, NULL}));
            }
            if (del_res) {
                csilk_json_free(del_res);
            }

            // 删除主单及所有子单
            csilk_json_free(csilk_db_query_param_json(
                pool,
                "DELETE FROM transactions WHERE user_id=? AND (id=? OR parent_tx_id=?)",
                (const char*[]){uid_str, maid_str, maid_str, NULL}));
            // 也删除当前 transfer_in（以防未被 parent_tx_id 关联到）
            tx_delete(pool, cmd->user_id, cmd->tx_id);
        } else {
            // 单独存在的 transfer_in，直接单笔回滚
            if (ledger_reverse_tx(pool, cmd->user_id, cmd->tx_id) != 0) {
                csilk_json_free(tx_rows);
                csilk_db_exec(pool, "ROLLBACK");
                out_res->code = 1004;
                snprintf(out_res->message, sizeof(out_res->message), "删除回滚失败");
                return -1;
            }
            csilk_json_free(csilk_db_query_param_json(
                pool,
                "DELETE FROM transfers WHERE user_id=? AND (from_tx_id=? OR to_tx_id=?)",
                (const char*[]){uid_str, tid_str, tid_str, NULL}));
            tx_delete(pool, cmd->user_id, cmd->tx_id);
        }
    } else {
        /* 普通交易 */
        if (ledger_reverse_tx(pool, cmd->user_id, cmd->tx_id) != 0) {
            csilk_json_free(tx_rows);
            csilk_db_exec(pool, "ROLLBACK");
            out_res->code = 1004;
            snprintf(out_res->message, sizeof(out_res->message), "删除回滚失败");
            return -1;
        }

        if (!tx_delete(pool, cmd->user_id, cmd->tx_id)) {
            csilk_json_free(tx_rows);
            csilk_db_exec(pool, "ROLLBACK");
            out_res->code = 1004;
            snprintf(out_res->message, sizeof(out_res->message), "删除失败");
            return -1;
        }
    }

    csilk_json_free(tx_rows);
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
                                 filter->ledger_id,
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
tx_usecase_monthly(
    void* pool, int64_t user_id, int64_t ledger_id, const char* month, tx_usecase_result_t* out_res)
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

    csilk_json_t* res = tx_monthly((csilk_db_pool_t*)pool, user_id, ledger_id, pattern);
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
