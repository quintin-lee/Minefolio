#include "common/balance.h"
#include "common/db.h"
#include "core/ledger/ledger_engine.h"
#include "services/market/exchange_rate_service.h"
#include "csilk/csilk.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 将金额折算到资产本币；跨币种时优先使用历史汇率（交易发生时固化的 to-CNY 汇率）。
 *
 * 历史汇率因子 = fx_rate_to_cny(源币种) / target_to_cny(资产币种)，
 * 使历史回放/回滚不受当前汇率波动影响。任一步骤失败则退回当前汇率路径。
 */
static money_t
balance_convert_money_fx(money_t delta, currency_t asset_cur, double fx_rate_to_cny)
{
    if (currency_equals(delta.currency, asset_cur)) {
        return delta;
    }
    if (fx_rate_to_cny > 0.0) {
        double target_to_cny = exchange_rate_get_to_cny(currency_code(&asset_cur));
        if (target_to_cny > 0.0) {
            double    factor = fx_rate_to_cny / target_to_cny;
            decimal_t f, prod;
            money_t   out;
            if (isfinite(factor) && factor > 0.0 &&
                decimal_from_double(factor, 6, &f) == DECIMAL_OK &&
                decimal_mul(delta.amount, f, &prod) == DECIMAL_OK &&
                decimal_round(
                    prod, (int32_t)currency_precision(asset_cur), ROUND_HALF_UP, &out.amount) ==
                    DECIMAL_OK) {
                out.currency = asset_cur;
                return out;
            }
        }
    }
    return exchange_rate_convert_m(delta, asset_cur);
}

price_t
balance_convert_price_fx(price_t price, currency_t asset_cur, double fx_rate_to_cny)
{
    if (currency_equals(price.currency, asset_cur)) {
        return price;
    }
    if (fx_rate_to_cny > 0.0) {
        double target_to_cny = exchange_rate_get_to_cny(currency_code(&asset_cur));
        if (target_to_cny > 0.0) {
            double    factor = fx_rate_to_cny / target_to_cny;
            decimal_t f, prod, rounded;
            if (isfinite(factor) && factor > 0.0 &&
                decimal_from_double(factor, 8, &f) == DECIMAL_OK &&
                decimal_mul(price.unit_price, f, &prod) == DECIMAL_OK &&
                decimal_round(prod, 8, ROUND_HALF_UP, &rounded) == DECIMAL_OK) {
                return price_from_decimal(rounded, asset_cur);
            }
        }
    }
    rate_t    r = exchange_rate_get_rate(price.currency, asset_cur);
    decimal_t prod, rounded;
    if (decimal_mul(price.unit_price, r.factor, &prod) == DECIMAL_OK &&
        decimal_round(prod, 8, ROUND_HALF_UP, &rounded) == DECIMAL_OK) {
        return price_from_decimal(rounded, asset_cur);
    }
    return price_from_decimal(price.unit_price, asset_cur);
}

int
balance_direction(const char* asset_type)
{
    if (!asset_type) {
        return 1;
    }
    if (strcmp(asset_type, "credit_card") == 0 || strcmp(asset_type, "loan") == 0 ||
        strcmp(asset_type, "other_liability") == 0) {
        return -1;
    }
    return 1;
}

int
is_investment_type(const char* atype)
{
    return atype && (strcmp(atype, "stock") == 0 || strcmp(atype, "fund") == 0 ||
                     strcmp(atype, "bond") == 0 || strcmp(atype, "crypto") == 0);
}

static int
balance_apply_delta_m_impl(csilk_db_pool_t* pool,
                           int64_t          asset_id,
                           int64_t          user_id,
                           money_t          delta,
                           double           fx_rate_to_cny,
                           const char*      source_type,
                           int64_t          source_id,
                           const char*      note)
{
    if (!pool || asset_id <= 0 || user_id <= 0 || !source_type) {
        return -1;
    }

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "mf_balance_delta", &scope) != 0) {
        return -2;
    }

    char asset_id_str[32], user_id_str[32];
    snprintf(asset_id_str, sizeof(asset_id_str), "%lld", (long long)asset_id);
    snprintf(user_id_str, sizeof(user_id_str), "%lld", (long long)user_id);

    // 1. 查询资产归属与类型
    const char*   params1[] = {asset_id_str, user_id_str, NULL};
    csilk_json_t* row =
        csilk_db_query_param_json(pool,
                                  "SELECT a.current_value, a.currency, c.asset_type "
                                  "FROM assets a JOIN categories c ON a.category_id = c.id "
                                  "WHERE a.id=? AND a.user_id=?",
                                  params1);
    if (!row) {
        db_tx_scope_rollback(pool, &scope);
        return -2;
    }
    if (csilk_json_array_size(row) == 0) {
        csilk_json_free(row);
        CSILK_LOG_W("balance_apply_delta: asset not found asset_id=%lld user_id=%lld",
                    (long long)asset_id,
                    (long long)user_id);
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    const csilk_json_t* asset = csilk_json_array_get(row, 0);
    const char*         asset_type = csilk_json_get_string(asset, "asset_type");
    const char*         asset_cur_str = csilk_json_get_string(asset, "currency");
    currency_t          asset_cur = currency_from_str(asset_cur_str);

    /* Multi-currency normalization: convert delta to the asset's native currency if different */
    money_t target_delta = balance_convert_money_fx(delta, asset_cur, fx_rate_to_cny);

    // 2. Normalize delta (liability balance direction flip)
    money_t signed_delta = target_delta;
    if (balance_direction(asset_type) < 0) {
        signed_delta = money_neg(target_delta);
    }
    csilk_json_free(row);

    // 3. 原子更新余额并读取变动后余额（RETURNING 合并两次查询为一次）
    char signed_delta_str[64];
    money_to_string(signed_delta, signed_delta_str, sizeof(signed_delta_str));

    const char*   params3[] = {signed_delta_str, asset_id_str, user_id_str, NULL};
    csilk_json_t* res3 =
        csilk_db_query_param_json(pool,
                                  "UPDATE assets SET current_value = current_value + ?, "
                                  "updated_at = CURRENT_TIMESTAMP WHERE id=? AND user_id=? "
                                  "RETURNING current_value",
                                  params3);
    if (!res3 || csilk_json_array_size(res3) == 0) {
        if (res3) {
            csilk_json_free(res3);
        }
        CSILK_LOG_E("balance_apply_delta: failed to update balance asset_id=%lld",
                    (long long)asset_id);
        db_tx_scope_rollback(pool, &scope);
        return -2;
    }
    money_t balance_after = db_get_money(csilk_json_array_get(res3, 0), "current_value", asset_cur);
    csilk_json_free(res3);

    // 5. 写审计日志
    char balance_after_str[64], source_id_str[32];
    money_to_string(balance_after, balance_after_str, sizeof(balance_after_str));
    snprintf(source_id_str, sizeof(source_id_str), "%lld", (long long)source_id);

    const char*   params5[] = {asset_id_str,
                               user_id_str,
                               signed_delta_str,
                               balance_after_str,
                               source_type,
                               source_id_str,
                             note ? note : "",
                               NULL};
    csilk_json_t* res5 = csilk_db_query_param_json(
        pool,
        "INSERT INTO asset_balance_logs (asset_id, user_id, delta, balance_after, "
        "source_type, source_id, note) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)",
        params5);
    if (!res5) {
        CSILK_LOG_E("balance_apply_delta: failed to insert audit log asset_id=%lld",
                    (long long)asset_id);
        db_tx_scope_rollback(pool, &scope);
        return -2;
    }
    csilk_json_free(res5);

    if (db_tx_scope_commit(pool, &scope) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -2;
    }

    return 0;
}

int
balance_apply_delta_m(csilk_db_pool_t* pool,
                      int64_t          asset_id,
                      int64_t          user_id,
                      money_t          delta,
                      const char*      source_type,
                      int64_t          source_id,
                      const char*      note)
{
    return balance_apply_delta_m_impl(
        pool, asset_id, user_id, delta, 0.0, source_type, source_id, note);
}

int
balance_apply_delta_m_fx(csilk_db_pool_t* pool,
                         int64_t          asset_id,
                         int64_t          user_id,
                         money_t          delta,
                         double           fx_rate_to_cny,
                         const char*      source_type,
                         int64_t          source_id,
                         const char*      note)
{
    return balance_apply_delta_m_impl(
        pool, asset_id, user_id, delta, fx_rate_to_cny, source_type, source_id, note);
}

int
balance_apply_delta(csilk_db_pool_t* pool,
                    int64_t          asset_id,
                    int64_t          user_id,
                    double           delta,
                    const char*      source_type,
                    int64_t          source_id,
                    const char*      note)
{
    char asset_id_str[32];
    snprintf(asset_id_str, sizeof(asset_id_str), "%lld", (long long)asset_id);
    csilk_json_t* r = csilk_db_query_param_json(
        pool, "SELECT currency FROM assets WHERE id=?", (const char*[]){asset_id_str, NULL});
    currency_t cur = CURRENCY_CNY;
    if (r && csilk_json_array_size(r) > 0) {
        const char* cstr = csilk_json_get_string(csilk_json_array_get(r, 0), "currency");
        cur = currency_from_str(cstr);
        csilk_json_free(r);
    } else if (r) {
        csilk_json_free(r);
    }

    money_t delta_m;
    money_from_double(delta, cur, &delta_m);
    return balance_apply_delta_m(pool, asset_id, user_id, delta_m, source_type, source_id, note);
}

int
apply_position_fc(csilk_db_pool_t* pool,
                  int64_t          asset_id,
                  int64_t          user_id,
                  const char*      type,
                  money_t          amount,
                  money_t          fee,
                  price_t          price,
                  quantity_t       qty,
                  double           fx_rate_to_cny,
                  money_t*         out_position_delta)
{
    char aid_str[32], uid_str[32];
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    csilk_json_t* pos = csilk_db_query_param_json(
        pool,
        "SELECT a.quantity, a.cost_basis, a.net_value, a.current_value, a.currency, a.cost_method, "
        "c.asset_type "
        "FROM assets a JOIN categories c ON a.category_id=c.id WHERE a.id=? AND a.user_id=?",
        (const char*[]){aid_str, uid_str, NULL});
    if (!pos || csilk_json_array_size(pos) == 0) {
        if (pos) {
            csilk_json_free(pos);
        }
        return 0;
    }
    const csilk_json_t* pr = csilk_json_array_get(pos, 0);
    const char*         atype = csilk_json_get_string(pr, "asset_type");
    if (!is_investment_type(atype)) {
        csilk_json_free(pos);
        return 0;
    }

    const char* cur_str = csilk_json_get_string(pr, "currency");
    currency_t  cur = currency_from_str(cur_str);

    /* Normalize amount, fee, and price to the asset's native currency */
    amount = balance_convert_money_fx(amount, cur, fx_rate_to_cny);
    fee = balance_convert_money_fx(fee, cur, fx_rate_to_cny);
    price = balance_convert_price_fx(price, cur, fx_rate_to_cny);

    quantity_t old_qty = db_get_quantity(pr, "quantity");
    money_t    old_cost = db_get_money(pr, "cost_basis", cur);
    price_t    old_net = db_get_price(pr, "net_value", cur);
    money_t    old_current = db_get_money(pr, "current_value", cur);

    quantity_t new_qty;
    money_t    new_cost;
    price_t    new_net;

    if (strcmp(type, "buy") == 0) {
        money_t tmp;
        if (quantity_add(old_qty, qty, &new_qty) != DECIMAL_OK ||
            money_add(old_cost, amount, &tmp) != DECIMAL_OK ||
            money_add(tmp, fee, &new_cost) != DECIMAL_OK) {
            csilk_json_free(pos);
            CSILK_LOG_W("apply_position: buy arithmetic overflow asset_id=%lld",
                        (long long)asset_id);
            return -1;
        }
        new_net = price;
    } else if (strcmp(type, "sell") == 0) {
        const char* cost_method = csilk_json_get_string(pr, "cost_method");
        money_t     cost_red, trade_pnl;
        if (cost_method && strcmp(cost_method, "fifo") == 0) {
            if (ledger_calc_sell_position_fifo(pool,
                                               user_id,
                                               asset_id,
                                               qty,
                                               price,
                                               fee,
                                               &new_qty,
                                               &new_cost,
                                               &cost_red,
                                               &trade_pnl) != 0) {
                csilk_json_free(pos);
                CSILK_LOG_W("apply_position: fifo sell calculation failed asset_id=%lld",
                            (long long)asset_id);
                return -1;
            }
        } else {
            if (ledger_calc_sell_position(old_qty,
                                          old_cost,
                                          qty,
                                          price,
                                          fee,
                                          &new_qty,
                                          &new_cost,
                                          &cost_red,
                                          &trade_pnl) != 0) {
                csilk_json_free(pos);
                CSILK_LOG_W("apply_position: sell calculation failed asset_id=%lld",
                            (long long)asset_id);
                return -1;
            }
        }
        new_net = old_net;
    } else {
        csilk_json_free(pos);
        return 0;
    }

    money_t new_val;
    money_t delta_m;
    if (price_times_quantity(new_net, new_qty, &new_val) != DECIMAL_OK ||
        money_sub(new_val, old_current, &delta_m) != DECIMAL_OK) {
        csilk_json_free(pos);
        CSILK_LOG_W("apply_position: market value arithmetic failed asset_id=%lld",
                    (long long)asset_id);
        return -1;
    }

    if (out_position_delta) {
        *out_position_delta = delta_m;
    }

    char nq[64], nc[64], nv[64];
    quantity_to_string_fixed(new_qty, 8, nq, sizeof(nq));
    money_to_string(new_cost, nc, sizeof(nc));
    price_to_string_fixed(new_net, 8, nv, sizeof(nv));

    const char*   p[] = {nq, nc, nv, aid_str, uid_str, NULL};
    csilk_json_t* upd_res = csilk_db_query_param_json(
        pool,
        "UPDATE assets SET quantity=?, cost_basis=?, net_value=? WHERE id=? AND user_id=?",
        p);
    if (!upd_res) {
        csilk_json_free(pos);
        return -2;
    }
    csilk_json_free(upd_res);
    csilk_json_free(pos);
    return 0;
}

int
apply_position(csilk_db_pool_t* pool,
               int64_t          asset_id,
               int64_t          user_id,
               const char*      type,
               double           amount,
               double           fee,
               double           price,
               double           qty,
               double*          out_position_delta)
{
    char aid_str[32], uid_str[32];
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    csilk_json_t* pos_cur =
        csilk_db_query_param_json(pool,
                                  "SELECT currency FROM assets WHERE id=? AND user_id=?",
                                  (const char*[]){aid_str, uid_str, NULL});
    currency_t cur = CURRENCY_CNY;
    if (pos_cur && csilk_json_array_size(pos_cur) > 0) {
        const char* cstr = csilk_json_get_string(csilk_json_array_get(pos_cur, 0), "currency");
        cur = currency_from_str(cstr);
        csilk_json_free(pos_cur);
    } else if (pos_cur) {
        csilk_json_free(pos_cur);
    }

    money_t    amount_m, fee_m, delta_m;
    price_t    price_p;
    quantity_t qty_q;

    money_from_double(amount, cur, &amount_m);
    money_from_double(fee, cur, &fee_m);
    price_from_double(price, 8, cur, &price_p);
    quantity_from_double(qty, 8, &qty_q);

    int rc = apply_position_fc(
        pool, asset_id, user_id, type, amount_m, fee_m, price_p, qty_q, 0.0, &delta_m);
    if (rc == 0 && out_position_delta) {
        *out_position_delta = money_to_double(delta_m);
    }
    return rc;
}

int
rollback_position_fc(csilk_db_pool_t* pool,
                     int64_t          asset_id,
                     int64_t          user_id,
                     int64_t          exclude_tx_id,
                     money_t*         out_position_delta)
{
    if (!pool || asset_id <= 0 || user_id <= 0) {
        return -1;
    }

    ledger_position_state_t state;
    int rc = ledger_rebuild_position_ex(pool, user_id, asset_id, &state, exclude_tx_id);
    if (rc != 0) {
        return rc;
    }

    if (out_position_delta) {
        *out_position_delta = money_zero(state.current_value.currency);
    }
    return 0;
}

int
rollback_position(csilk_db_pool_t* pool,
                  int64_t          asset_id,
                  int64_t          user_id,
                  int64_t          exclude_tx_id,
                  double*          out_position_delta)
{
    money_t delta_m;
    int     rc = rollback_position_fc(pool, asset_id, user_id, exclude_tx_id, &delta_m);
    if (rc == 0 && out_position_delta) {
        *out_position_delta = money_to_double(delta_m);
    }
    return rc;
}
