#include "application/asset/usecases.h"
#include "domain/asset/entity.h"
#include "domain/asset/rules.h"
#include "domain/asset/performance.h"
#include "infrastructure/repositories/asset_repo_impl.h"
#include "infrastructure/repositories/market_repo_impl.h"
#include "common/balance.h"
#include "common/db.h"
#include "core/financial/price.h"
#include "core/financial/quantity.h"
#include "core/ledger/ledger_engine.h"
#include "application/account/usecases.h"
#include "infrastructure/repositories/account_repo_impl.h"
#include "common/ledger_utils.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 解析目标账户 id：account_id > 0 时校验归属（user）；account_id <= 0 时
   兜底「默认账户」（幂等创建），保证无 account_id 的旧调用路径仍可创建资产。 */
static int64_t
asset_resolve_account_id(csilk_db_pool_t* pool,
                         int64_t          user_id,
                         int64_t          ledger_id,
                         int64_t          account_id)
{
    if (account_id > 0) {
        csilk_json_t* ac = account_get(pool, user_id, account_id);
        int           ok = ac && csilk_json_array_size(ac) > 0;
        if (ac) {
            csilk_json_free(ac);
        }
        return ok ? account_id : 0;
    }
    if (ledger_id <= 0) {
        ledger_id = ledger_get_default(pool, user_id);
    }
    if (ledger_id <= 0) {
        return 0;
    }
    return account_create_default(pool, user_id, ledger_id);
}

int
asset_usecase_create(void* pool, const create_asset_cmd_t* cmd, asset_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));

    if (!cmd || !cmd->name || cmd->category_id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "name 和 category_id 为必填");
        return -1;
    }

    const char* currency = (cmd->currency && cmd->currency[0]) ? cmd->currency : "CNY";
    currency_t  cur = currency_from_str(currency);

    char* asset_type =
        asset_get_category_type((csilk_db_pool_t*)pool, cmd->user_id, cmd->category_id);

    mf_asset_t asset = {0};
    asset.user_id = cmd->user_id;
    asset.category_id = cmd->category_id;
    snprintf(asset.name, sizeof(asset.name), "%s", cmd->name);
    if (cmd->account_no) {
        snprintf(asset.account_no, sizeof(asset.account_no), "%s", cmd->account_no);
    }
    if (asset_type) {
        snprintf(asset.asset_type, sizeof(asset.asset_type), "%s", asset_type);
    }
    asset.currency = cur;
    if (cmd->note) {
        snprintf(asset.note, sizeof(asset.note), "%s", cmd->note);
    }
    if (cmd->symbol) {
        snprintf(asset.symbol, sizeof(asset.symbol), "%s", cmd->symbol);
    }
    if (cmd->quote_source) {
        snprintf(asset.quote_source, sizeof(asset.quote_source), "%s", cmd->quote_source);
    }
    if (cmd->cost_method && cmd->cost_method[0]) {
        snprintf(asset.cost_method, sizeof(asset.cost_method), "%s", cmd->cost_method);
    } else {
        snprintf(asset.cost_method, sizeof(asset.cost_method), "weighted_average");
    }

    money_from_double(cmd->current_value, cur, &asset.current_value);
    quantity_from_double(cmd->quantity, 4, &asset.quantity);
    money_from_double(cmd->cost_basis, cur, &asset.cost_basis);
    price_from_double(cmd->net_value, 4, cur, &asset.net_value);

    /* 业务规则：投资品市值与成本自动推导 */
    mf_asset_rule_derive_investment_values(&asset);

    /* 领域规则验证 */
    char err_buf[256] = {0};
    if (mf_asset_rule_validate(&asset, err_buf, sizeof(err_buf)) != 0) {
        if (asset_type) {
            free(asset_type);
        }
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "%s", err_buf);
        return -1;
    }
    if (asset_type) {
        free(asset_type);
    }
    /* 账户维度：account_id > 0 需校验归属（查不到 → 1002）；account_id <= 0 兜底「默认账户」 */
    int64_t resolved_account_id = asset_resolve_account_id(
        (csilk_db_pool_t*)pool, cmd->user_id, cmd->ledger_id, cmd->account_id);
    if (cmd->account_id > 0 && resolved_account_id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "账户不存在或不属于该用户");
        return -1;
    }

    int64_t id = asset_insert((csilk_db_pool_t*)pool,
                              asset.user_id,
                              asset.category_id,
                              resolved_account_id,
                              asset.name,
                              asset.account_no,
                              money_to_double(asset.current_value),
                              currency,
                              asset.note,
                              quantity_to_double(asset.quantity),
                              money_to_double(asset.cost_basis),
                              price_to_double(asset.net_value),
                              asset.symbol,
                              asset.quote_source,
                              asset.cost_method);
    if (id <= 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "创建失败");
        return -1;
    }

    out_res->code = 0;
    out_res->id = id;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
asset_usecase_update(void* pool, const update_asset_cmd_t* cmd, asset_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));

    if (!cmd || cmd->id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "缺少 id");
        return -1;
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    if (!asset_exists(db_pool, cmd->user_id, cmd->id)) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "资产不存在");
        return -1;
    }

    csilk_json_t* cur = asset_get(db_pool, cmd->user_id, cmd->id);
    if (!cur || csilk_json_array_size(cur) == 0) {
        if (cur) {
            csilk_json_free(cur);
        }
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "资产不存在");
        return -1;
    }

    const csilk_json_t* cr = csilk_json_array_get(cur, 0);
    const char* name = (cmd->name && cmd->name[0]) ? cmd->name : csilk_json_get_string(cr, "name");
    const char* account_no =
        cmd->account_no ? cmd->account_no : csilk_json_get_string(cr, "account_no");
    const char* currency =
        (cmd->currency && cmd->currency[0]) ? cmd->currency : csilk_json_get_string(cr, "currency");
    const char* note = cmd->note ? cmd->note : csilk_json_get_string(cr, "note");
    const char* symbol = cmd->symbol ? cmd->symbol : csilk_json_get_string(cr, "symbol");
    const char* quote_source =
        cmd->quote_source ? cmd->quote_source : csilk_json_get_string(cr, "quote_source");
    const char* asset_type = csilk_json_get_string(cr, "asset_type");

    /* RF-2: 成本结转方法变更检查与防篡改规则 */
    if (cmd->cost_method && cmd->cost_method[0] != '\0') {
        if (!mf_asset_rule_is_valid_cost_method(cmd->cost_method)) {
            csilk_json_free(cur);
            out_res->code = 1002;
            snprintf(out_res->message,
                     sizeof(out_res->message),
                     "无效的成本结转方法: 仅支持 weighted_average 或 fifo");
            return -1;
        }

        const char* cur_method = csilk_json_get_string(cr, "cost_method");
        if (!cur_method || cur_method[0] == '\0') {
            cur_method = "weighted_average";
        }

        if (strcmp(cmd->cost_method, cur_method) != 0) {
            char uid_str[32], aid_str[32];
            snprintf(uid_str, sizeof(uid_str), "%lld", (long long)cmd->user_id);
            snprintf(aid_str, sizeof(aid_str), "%lld", (long long)cmd->id);
            csilk_json_t* tx_check = csilk_db_query_param_json(
                db_pool,
                "SELECT COUNT(*) as cnt FROM transactions WHERE user_id=? AND asset_id=?",
                (const char*[]){uid_str, aid_str, NULL});
            int64_t tx_cnt = 0;
            if (tx_check && csilk_json_array_size(tx_check) > 0) {
                tx_cnt = db_get_int(csilk_json_array_get(tx_check, 0), "cnt");
            }
            if (tx_check) {
                csilk_json_free(tx_check);
            }
            if (tx_cnt > 0) {
                csilk_json_free(cur);
                out_res->code = 1004;
                snprintf(out_res->message,
                         sizeof(out_res->message),
                         "资产已存在交易记录，禁止变更成本结转方法");
                return -1;
            }

            if (asset_update_cost_method(db_pool, cmd->user_id, cmd->id, cmd->cost_method) != 0) {
                csilk_json_free(cur);
                out_res->code = 1004;
                snprintf(out_res->message, sizeof(out_res->message), "更新成本结转方法失败");
                return -1;
            }
        }
    }

    int is_investment = mf_asset_is_investment_type_str(asset_type);

    /* 账户重挂：body 携带 account_id 时生效；>0 须属于该用户否则 1002，<=0 沿资产账本兜底默认账户 */
    int64_t reattach_acct = 0;
    int     need_reattach = 0;
    if (cmd->has_account_id) {
        char lid_buf[32], uid_buf[32];
        snprintf(lid_buf, sizeof(lid_buf), "%lld", (long long)cmd->id);
        snprintf(uid_buf, sizeof(uid_buf), "%lld", (long long)cmd->user_id);
        int64_t       asset_ledger = 0;
        csilk_json_t* lid_res =
            csilk_db_query_param_json(db_pool,
                                      "SELECT ledger_id FROM assets WHERE id=? AND user_id=?",
                                      (const char*[]){lid_buf, uid_buf, NULL});
        if (lid_res && csilk_json_array_size(lid_res) > 0) {
            asset_ledger = db_get_int(csilk_json_array_get(lid_res, 0), "ledger_id");
        }
        if (lid_res) {
            csilk_json_free(lid_res);
        }
        reattach_acct =
            asset_resolve_account_id(db_pool, cmd->user_id, asset_ledger, cmd->account_id);
        if (cmd->account_id > 0 && reattach_acct <= 0) {
            csilk_json_free(cur);
            out_res->code = 1002;
            snprintf(out_res->message, sizeof(out_res->message), "账户不存在或不属于该用户");
            return -1;
        }
        need_reattach = 1;
    }

    if (cmd->has_net_value || cmd->has_quantity || cmd->has_cost_basis) {
        if (is_investment) {
            double old_qty = db_get_num(cr, "quantity");
            double old_cost = db_get_num(cr, "cost_basis");
            double old_net = db_get_num(cr, "net_value");
            double old_current = db_get_num(cr, "current_value");

            double new_qty = cmd->has_quantity ? cmd->quantity : old_qty;
            double new_net = cmd->has_net_value ? cmd->net_value : old_net;
            double new_cost = cmd->has_cost_basis ? cmd->cost_basis : old_cost;

            /* Pre-compute values before starting the transaction scope */
            currency_t asset_currency = currency_from_str(currency);

            quantity_t qty_obj;
            quantity_from_double(new_qty, 4, &qty_obj);

            price_t net_obj;
            price_from_double(new_net, 4, asset_currency, &net_obj);

            /* Multiply: money_t (2 decimal places) = price_times_quantity(net_obj, qty_obj, &new_val_m) */
            money_t new_val_m;
            if (price_times_quantity(net_obj, qty_obj, &new_val_m) != DECIMAL_OK) {
                csilk_json_free(cur);
                out_res->code = 1004;
                snprintf(out_res->message, sizeof(out_res->message), "金额计算溢出");
                return -1;
            }

            money_t old_val_m;
            money_from_double(old_current, asset_currency, &old_val_m);

            money_t delta_m;
            if (money_sub(new_val_m, old_val_m, &delta_m) != DECIMAL_OK) {
                csilk_json_free(cur);
                out_res->code = 1004;
                snprintf(out_res->message, sizeof(out_res->message), "金额计算溢出");
                return -1;
            }

            db_tx_scope_t scope;
            if (db_tx_scope_begin(db_pool, "mf_asset_update_investment", &scope) != 0) {
                csilk_json_free(cur);
                out_res->code = 1004;
                snprintf(out_res->message, sizeof(out_res->message), "数据库错误");
                return -1;
            }

            if (!money_is_zero(delta_m)) {
                int b_rc = balance_apply_delta_m(db_pool,
                                                 cmd->id,
                                                 cmd->user_id,
                                                 delta_m,
                                                 "asset_netvalue",
                                                 cmd->id,
                                                 "net_value update");
                if (b_rc != 0) {
                    db_tx_scope_rollback(db_pool, &scope);
                    csilk_json_free(cur);
                    out_res->code = (b_rc == -1) ? 1002 : 1004;
                    snprintf(out_res->message, sizeof(out_res->message), "更新资产余额失败");
                    return -1;
                }
            }

            double updated_current = money_to_double(new_val_m);
            if (!asset_update_position(
                    db_pool, cmd->user_id, cmd->id, new_net, new_qty, new_cost) ||
                !asset_update_basic(db_pool,
                                    cmd->user_id,
                                    cmd->id,
                                    name,
                                    account_no,
                                    updated_current,
                                    currency,
                                    note,
                                    symbol,
                                    quote_source)) {
                db_tx_scope_rollback(db_pool, &scope);
                csilk_json_free(cur);
                out_res->code = 1004;
                snprintf(out_res->message, sizeof(out_res->message), "更新资产持仓或基本信息失败");
                return -1;
            }

            if (need_reattach &&
                asset_update_account(db_pool, cmd->user_id, cmd->id, reattach_acct)) {
                db_tx_scope_rollback(db_pool, &scope);
                csilk_json_free(cur);
                out_res->code = 1004;
                snprintf(out_res->message, sizeof(out_res->message), "账户重挂失败");
                return -1;
            }

            if (db_tx_scope_commit(db_pool, &scope) != 0) {
                db_tx_scope_rollback(db_pool, &scope);
                csilk_json_free(cur);
                out_res->code = 1004;
                snprintf(out_res->message, sizeof(out_res->message), "提交事务失败");
                return -1;
            }
        }
        csilk_json_free(cur);
        out_res->code = 0;
        snprintf(out_res->message, sizeof(out_res->message), "ok");
        return 0;
    }

    /* 普通资产更新 */
    double upd_current_val =
        (cmd->current_value != 0.0) ? cmd->current_value : db_get_num(cr, "current_value");
    if (!asset_update_basic(db_pool,
                            cmd->user_id,
                            cmd->id,
                            name,
                            account_no,
                            upd_current_val,
                            currency,
                            note,
                            symbol,
                            quote_source)) {
        csilk_json_free(cur);
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "更新失败");
        return -1;
    }

    if (need_reattach && asset_update_account(db_pool, cmd->user_id, cmd->id, reattach_acct)) {
        csilk_json_free(cur);
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "账户重挂失败");
        return -1;
    }

    csilk_json_free(cur);
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
asset_usecase_delete(void* pool, const delete_asset_cmd_t* cmd, asset_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));

    if (!cmd || cmd->id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "缺少 id");
        return -1;
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    int has_tx = 0;
    if (asset_has_transactions(db_pool, cmd->user_id, cmd->id, &has_tx) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询关联交易失败");
        return -1;
    }
    if (has_tx) {
        out_res->code = 1004;
        snprintf(out_res->message,
                 sizeof(out_res->message),
                 "该资产存在关联交易记录，无法直接删除，请先清理交易或归档资产");
        return -1;
    }

    if (!asset_delete(db_pool, cmd->user_id, cmd->id)) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "资产不存在或删除失败");
        return -1;
    }

    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
asset_usecase_get(void* pool, int64_t user_id, int64_t id, csilk_json_t** out_json)
{
    if (!out_json) {
        return -1;
    }
    *out_json = NULL;

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    csilk_json_t*    result = asset_get(db_pool, user_id, id);
    if (!result || csilk_json_array_size(result) == 0) {
        if (result) {
            csilk_json_free(result);
        }
        return 1003;
    }

    const csilk_json_t* row = csilk_json_array_get(result, 0);
    csilk_json_t*       resp = csilk_json_object();
    csilk_json_add_number(resp, "id", db_get_num(row, "id"));
    csilk_json_add_number(resp, "category_id", db_get_num(row, "category_id"));
    csilk_json_add_string(resp, "name", csilk_json_get_string(row, "name"));
    csilk_json_add_string(resp, "account_no", csilk_json_get_string(row, "account_no"));
    csilk_json_add_number(resp, "current_value", db_get_num(row, "current_value"));
    csilk_json_add_string(resp, "currency", csilk_json_get_string(row, "currency"));
    csilk_json_add_string(resp, "note", csilk_json_get_string(row, "note"));
    csilk_json_add_string(resp, "category_name", csilk_json_get_string(row, "category_name"));
    csilk_json_add_number(resp, "account_id", db_get_num(row, "account_id"));
    csilk_json_add_string(resp, "account_name", csilk_json_get_string(row, "account_name"));
    csilk_json_add_string(resp, "institution", csilk_json_get_string(row, "institution"));
    csilk_json_add_string(resp, "asset_type", csilk_json_get_string(row, "asset_type"));
    csilk_json_add_number(resp, "quantity", db_get_num(row, "quantity"));
    csilk_json_add_number(resp, "cost_basis", db_get_num(row, "cost_basis"));
    csilk_json_add_number(resp, "net_value", db_get_num(row, "net_value"));
    csilk_json_add_string(resp, "created_at", csilk_json_get_string(row, "created_at"));
    csilk_json_add_string(resp, "updated_at", csilk_json_get_string(row, "updated_at"));

    csilk_json_t* tx_rows = asset_transactions(db_pool, user_id, id);
    csilk_json_t* transactions = csilk_json_array();
    if (tx_rows) {
        size_t n = csilk_json_array_size(tx_rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* tr = csilk_json_array_get(tx_rows, i);
            csilk_json_t* item = csilk_json_object();
            csilk_json_add_number(item, "id", db_get_num(tr, "id"));
            csilk_json_add_number(item, "asset_id", db_get_num(tr, "asset_id"));
            csilk_json_add_string(
                item, "transaction_type", csilk_json_get_string(tr, "transaction_type"));
            csilk_json_add_number(item, "amount", db_get_num(tr, "amount"));
            csilk_json_add_number(item, "quantity", db_get_num(tr, "quantity"));
            csilk_json_add_number(item, "price_per_unit", db_get_num(tr, "price_per_unit"));
            csilk_json_add_string(item, "currency", csilk_json_get_string(tr, "currency"));
            csilk_json_add_string(
                item, "transaction_date", csilk_json_get_string(tr, "transaction_date"));
            csilk_json_add_string(item, "note", csilk_json_get_string(tr, "note"));
            csilk_json_add_string(item, "created_at", csilk_json_get_string(tr, "created_at"));
            csilk_json_array_append(transactions, item);
        }
        csilk_json_free(tx_rows);
    }
    csilk_json_add_array(resp, "transactions", transactions);

    /* ── 扩展：区间收益 + 当日涨跌 + 组合权重 ─────────────────── */

    /* 1. 区间收益（1M/3M/6M/1Y/ALL）：从价格历史序列计算 */
    int64_t aid64 = db_get_int(row, "id");
    /* 取过去 365 天的价格序列 */
    char today_str[16], year_ago_str[16];
    {
        time_t    now_t = time(NULL);
        struct tm tm_now;
        localtime_r(&now_t, &tm_now);
        strftime(today_str, sizeof(today_str), "%Y-%m-%d", &tm_now);
        time_t    year_ago = now_t - 365 * 86400;
        struct tm tm_y;
        localtime_r(&year_ago, &tm_y);
        strftime(year_ago_str, sizeof(year_ago_str), "%Y-%m-%d", &tm_y);
    }
    csilk_json_t* price_rows =
        mf_market_repo_price_history_range(db_pool, user_id, aid64, year_ago_str, today_str);
    if (price_rows && csilk_json_array_size(price_rows) >= 2) {
        size_t            pn = csilk_json_array_size(price_rows);
        mf_price_point_t* series = (mf_price_point_t*)calloc(pn, sizeof(mf_price_point_t));
        if (series) {
            for (size_t i = 0; i < pn; i++) {
                const csilk_json_t* pr = csilk_json_array_get(price_rows, i);
                const char*         pd = csilk_json_get_string(pr, "price_date");
                const char*         cur = csilk_json_get_string(pr, "currency");
                currency_t          c = cur ? currency_from_str(cur) : currency_from_str("CNY");
                if (pd) {
                    snprintf(series[i].date, sizeof(series[i].date), "%s", pd);
                }
                double pv = db_get_num(pr, "price");
                price_from_double(pv, 4, c, &series[i].price);
            }
            mf_interval_returns_t rets;
            memset(&rets, 0, sizeof(rets));
            if (mf_interval_returns_from_series(series, pn, today_str, &rets) == 0) {
                csilk_json_t* ret_obj = csilk_json_object();
                csilk_json_add_number(ret_obj, "ret_1m", rets.ret_1m);
                csilk_json_add_bool(ret_obj, "valid_1m", rets.valid_1m ? 1 : 0);
                csilk_json_add_number(ret_obj, "ret_3m", rets.ret_3m);
                csilk_json_add_bool(ret_obj, "valid_3m", rets.valid_3m ? 1 : 0);
                csilk_json_add_number(ret_obj, "ret_6m", rets.ret_6m);
                csilk_json_add_bool(ret_obj, "valid_6m", rets.valid_6m ? 1 : 0);
                csilk_json_add_number(ret_obj, "ret_1y", rets.ret_1y);
                csilk_json_add_bool(ret_obj, "valid_1y", rets.valid_1y ? 1 : 0);
                csilk_json_add_number(ret_obj, "ret_all", rets.ret_all);
                csilk_json_add_bool(ret_obj, "valid_all", rets.valid_all ? 1 : 0);
                csilk_json_add_object(resp, "interval_returns", ret_obj);
            } else {
                csilk_json_add_object(resp, "interval_returns", NULL);
            }

            /* 2. 当日涨跌（最新价 vs 前一日价） */
            money_t daily_change;
            double  daily_pct = 0.0;
            if (mf_daily_change_from_series(series, pn, &daily_change, &daily_pct) == 0) {
                csilk_json_t* daily_obj = csilk_json_object();
                char          chg_str[64];
                money_to_string(daily_change, chg_str, sizeof(chg_str));
                csilk_json_add_string(daily_obj, "change", chg_str);
                csilk_json_add_number(daily_obj, "pct", daily_pct);
                csilk_json_add_object(resp, "daily_change", daily_obj);
            }

            free(series);
        }
    }
    if (price_rows) {
        csilk_json_free(price_rows);
    }

    /* 3. 组合权重（占该用户全部非负债资产市值比例） */
    {
        double self_val = db_get_num(row, "current_value");
        /* 快速取该用户全部非负债资产合计市值 */
        char uid_str[32];
        snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
        csilk_json_t* all_rows =
            csilk_db_query_param_json(db_pool,
                                      "SELECT a.current_value, c.asset_type FROM assets a "
                                      "JOIN categories c ON a.category_id=c.id "
                                      "WHERE a.user_id=? AND c.asset_type NOT IN "
                                      "('loan','credit_card','other_liability')",
                                      (const char*[]){uid_str, NULL});
        double total_mkt = 0.0;
        if (all_rows) {
            size_t tn = csilk_json_array_size(all_rows);
            for (size_t i = 0; i < tn; i++) {
                total_mkt += db_get_num(csilk_json_array_get(all_rows, i), "current_value");
            }
            csilk_json_free(all_rows);
        }
        double weight = 0.0;
        if (total_mkt > 0.0 && self_val > 0.0) {
            weight = self_val / total_mkt;
        }
        csilk_json_add_number(resp, "portfolio_weight", weight);
    }

    csilk_json_free(result);

    *out_json = resp;
    return 0;
}

int
asset_usecase_query(void*                       pool,
                    const query_asset_filter_t* filter,
                    csilk_json_t**              out_list,
                    int64_t*                    out_total)
{
    if (!out_list || !out_total || !filter) {
        return -1;
    }
    *out_list = asset_list((csilk_db_pool_t*)pool,
                           filter->user_id,
                           filter->page,
                           filter->page_size,
                           filter->category_id,
                           filter->account_id,
                           out_total);
    return (*out_list != NULL) ? 0 : -1;
}

int
asset_usecase_query_logs(void*                           pool,
                         const query_asset_log_filter_t* filter,
                         csilk_json_t**                  out_list,
                         int64_t*                        out_total)
{
    if (!out_list || !out_total || !filter) {
        return -1;
    }
    *out_list = asset_balance_logs_list((csilk_db_pool_t*)pool,
                                        filter->user_id,
                                        filter->page,
                                        filter->page_size,
                                        filter->asset_id,
                                        out_total);
    return (*out_list != NULL) ? 0 : -1;
}

int
asset_usecase_rebuild_single(void*                   pool,
                             int64_t                 user_id,
                             int64_t                 id,
                             csilk_json_t**          out_data,
                             asset_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    csilk_json_t*    cur = asset_get(db_pool, user_id, id);
    if (!cur || csilk_json_array_size(cur) == 0) {
        if (cur) {
            csilk_json_free(cur);
        }
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "资产不存在");
        return -1;
    }

    const csilk_json_t* row = csilk_json_array_get(cur, 0);
    const char*         asset_type = csilk_json_get_string(row, "asset_type");
    int                 is_investment = mf_asset_is_investment_type_str(asset_type);
    csilk_json_free(cur);

    csilk_json_t* data = csilk_json_object();

    if (is_investment) {
        ledger_position_state_t pos_state = {0};
        if (ledger_rebuild_position(db_pool, user_id, id, &pos_state) != 0) {
            csilk_json_free(data);
            out_res->code = 500;
            snprintf(out_res->message, sizeof(out_res->message), "持仓重建失败");
            return -1;
        }
        char q_buf[64], c_buf[64], nv_buf[64], cv_buf[64], rp_buf[64], up_buf[64];
        quantity_to_string(pos_state.quantity, q_buf, sizeof(q_buf));
        money_to_string(pos_state.cost_basis, c_buf, sizeof(c_buf));
        price_to_string(pos_state.net_value, nv_buf, sizeof(nv_buf));
        money_to_string(pos_state.current_value, cv_buf, sizeof(cv_buf));
        money_to_string(pos_state.realized_pnl, rp_buf, sizeof(rp_buf));
        money_to_string(pos_state.unrealized_pnl, up_buf, sizeof(up_buf));

        csilk_json_add_number(data, "asset_id", (double)id);
        csilk_json_add_string(data, "quantity", q_buf);
        csilk_json_add_string(data, "cost_basis", c_buf);
        csilk_json_add_string(data, "net_value", nv_buf);
        csilk_json_add_string(data, "current_value", cv_buf);
        csilk_json_add_string(data, "realized_pnl", rp_buf);
        csilk_json_add_string(data, "unrealized_pnl", up_buf);
    } else {
        ledger_account_state_t acc_state = {0};
        if (ledger_rebuild_account(db_pool, user_id, id, &acc_state) != 0) {
            csilk_json_free(data);
            out_res->code = 500;
            snprintf(out_res->message, sizeof(out_res->message), "账户余额重建失败");
            return -1;
        }
        char bal_buf[64];
        money_to_string(acc_state.balance, bal_buf, sizeof(bal_buf));

        csilk_json_add_number(data, "asset_id", (double)id);
        csilk_json_add_string(data, "balance", bal_buf);
        csilk_json_add_number(data, "tx_count", (double)acc_state.tx_count);
    }

    if (out_data) {
        *out_data = data;
    } else {
        csilk_json_free(data);
    }

    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
asset_usecase_rebuild_all(void* pool, int64_t user_id, asset_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    if (ledger_rebuild_portfolio(db_pool, user_id) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "组合账本重建失败");
        return -1;
    }

    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}
