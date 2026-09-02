#include "common/balance.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int
balance_apply_delta_m(csilk_db_pool_t* pool,
                      int64_t          asset_id,
                      int64_t          user_id,
                      money_t          delta,
                      const char*      source_type,
                      int64_t          source_id,
                      const char*      note)
{
    if (!pool || asset_id <= 0 || user_id <= 0 || !source_type) {
        return -1;
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
        return -2;
    }
    if (csilk_json_array_size(row) == 0) {
        csilk_json_free(row);
        CSILK_LOG_W("balance_apply_delta: asset not found asset_id=%lld user_id=%lld",
                    (long long)asset_id,
                    (long long)user_id);
        return -1;
    }
    const csilk_json_t* asset = csilk_json_array_get(row, 0);
    const char*         asset_type = csilk_json_get_string(asset, "asset_type");
    const char*         asset_cur_str = csilk_json_get_string(asset, "currency");
    currency_t          asset_cur = currency_from_str(asset_cur_str);

    // 2. 归一化 delta（负债方向反转）
    money_t signed_delta = delta;
    if (balance_direction(asset_type) < 0) {
        signed_delta = money_neg(delta);
    }
    csilk_json_free(row);

    // 3. 原子更新余额（定点数字符串格式化）
    char signed_delta_str[64];
    money_to_string(signed_delta, signed_delta_str, sizeof(signed_delta_str));

    const char*   params3[] = {signed_delta_str, asset_id_str, user_id_str, NULL};
    csilk_json_t* res3 =
        csilk_db_query_param_json(pool,
                                  "UPDATE assets SET current_value = current_value + ?, "
                                  "updated_at = CURRENT_TIMESTAMP WHERE id=? AND user_id=?",
                                  params3);
    if (!res3) {
        CSILK_LOG_E("balance_apply_delta: failed to update balance asset_id=%lld",
                    (long long)asset_id);
        return -2;
    }
    csilk_json_free(res3);

    // 4. 读取变动后余额（balance_after 快照）
    const char*   params4[] = {asset_id_str, user_id_str, NULL};
    csilk_json_t* after = csilk_db_query_param_json(
        pool, "SELECT current_value FROM assets WHERE id=? AND user_id=?", params4);
    if (!after || csilk_json_array_size(after) == 0) {
        if (after) {
            csilk_json_free(after);
        }
        CSILK_LOG_E("balance_apply_delta: failed to read balance_after asset_id=%lld",
                    (long long)asset_id);
        return -2;
    }
    money_t balance_after =
        db_get_money(csilk_json_array_get(after, 0), "current_value", asset_cur);
    csilk_json_free(after);

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
        return -2;
    }
    csilk_json_free(res5);

    return 0;
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
    money_t delta_m;
    money_from_double(delta, CURRENCY_CNY, &delta_m);
    return balance_apply_delta_m(pool, asset_id, user_id, delta_m, source_type, source_id, note);
}

int
apply_position_fc(csilk_db_pool_t* pool,
                  int64_t          asset_id,
                  const char*      type,
                  money_t          amount,
                  money_t          fee,
                  price_t          price,
                  quantity_t       qty,
                  money_t*         out_position_delta)
{
    char aid_str[32];
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    csilk_json_t* pos = csilk_db_query_param_json(
        pool,
        "SELECT a.quantity, a.cost_basis, a.net_value, a.current_value, a.currency, c.asset_type "
        "FROM assets a JOIN categories c ON a.category_id=c.id WHERE a.id=?",
        (const char*[]){aid_str, NULL});
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

    quantity_t old_qty = db_get_quantity(pr, "quantity");
    money_t    old_cost = db_get_money(pr, "cost_basis", cur);
    price_t    old_net = db_get_price(pr, "net_value", cur);
    money_t    old_current = db_get_money(pr, "current_value", cur);

    quantity_t new_qty;
    money_t    new_cost;
    price_t    new_net;

    if (strcmp(type, "buy") == 0) {
        quantity_add(old_qty, qty, &new_qty);
        money_t tmp;
        money_add(old_cost, amount, &tmp);
        money_add(tmp, fee, &new_cost);
        new_net = price;
    } else if (strcmp(type, "sell") == 0) {
        if (quantity_cmp(old_qty, qty) < 0) {
            csilk_json_free(pos);
            CSILK_LOG_W("apply_position: insufficient quantity asset_id=%lld", (long long)asset_id);
            return -1;
        }
        price_t avg_cost;
        if (!quantity_is_zero(old_qty)) {
            money_div_quantity(old_cost, old_qty, 6, ROUND_HALF_UP, &avg_cost);
        } else {
            avg_cost = price_zero(cur);
        }
        quantity_sub(old_qty, qty, &new_qty);
        money_t cost_sold;
        price_times_quantity(avg_cost, qty, &cost_sold);
        money_sub(old_cost, cost_sold, &new_cost);
        new_net = old_net;
    } else {
        csilk_json_free(pos);
        return 0;
    }

    money_t new_val;
    price_times_quantity(new_net, new_qty, &new_val);

    money_t delta_m;
    money_sub(new_val, old_current, &delta_m);

    if (out_position_delta) {
        *out_position_delta = delta_m;
    }

    char nq[64], nc[64], nv[64];
    quantity_to_string_fixed(new_qty, 4, nq, sizeof(nq));
    money_to_string(new_cost, nc, sizeof(nc));
    price_to_string_fixed(new_net, 4, nv, sizeof(nv));

    const char*   p[] = {nq, nc, nv, aid_str, NULL};
    csilk_json_t* upd_res = csilk_db_query_param_json(
        pool, "UPDATE assets SET quantity=?, cost_basis=?, net_value=? WHERE id=?", p);
    if (upd_res) {
        csilk_json_free(upd_res);
    }
    csilk_json_free(pos);
    return 0;
}

int
apply_position(csilk_db_pool_t* pool,
               int64_t          asset_id,
               const char*      type,
               double           amount,
               double           fee,
               double           price,
               double           qty,
               double*          out_position_delta)
{
    money_t    amount_m, fee_m, delta_m;
    price_t    price_p;
    quantity_t qty_q;

    money_from_double(amount, CURRENCY_CNY, &amount_m);
    money_from_double(fee, CURRENCY_CNY, &fee_m);
    price_from_double(price, 4, CURRENCY_CNY, &price_p);
    quantity_from_double(qty, 4, &qty_q);

    int rc = apply_position_fc(pool, asset_id, type, amount_m, fee_m, price_p, qty_q, &delta_m);
    if (rc == 0 && out_position_delta) {
        *out_position_delta = money_to_double(delta_m);
    }
    return rc;
}

int
rollback_position_fc(csilk_db_pool_t* pool,
                     int64_t          asset_id,
                     const char*      type,
                     money_t          amount,
                     money_t          fee,
                     price_t          price,
                     quantity_t       qty,
                     money_t*         out_position_delta)
{
    if (!pool || asset_id <= 0 || !type) {
        return 0;
    }

    char aid_str[32];
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    csilk_json_t* pos = csilk_db_query_param_json(
        pool,
        "SELECT a.quantity, a.cost_basis, a.net_value, a.current_value, a.currency, c.asset_type "
        "FROM assets a JOIN categories c ON a.category_id=c.id WHERE a.id=?",
        (const char*[]){aid_str, NULL});
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

    quantity_t old_qty = db_get_quantity(pr, "quantity");
    money_t    old_cost = db_get_money(pr, "cost_basis", cur);
    price_t    old_net = db_get_price(pr, "net_value", cur);
    money_t    old_current = db_get_money(pr, "current_value", cur);

    quantity_t new_qty;
    money_t    new_cost;
    price_t    new_net;

    if (strcmp(type, "buy") == 0) {
        if (quantity_cmp(old_qty, qty) > 0) {
            quantity_sub(old_qty, qty, &new_qty);
        } else {
            new_qty = quantity_zero();
        }

        money_t total_paid;
        money_add(amount, fee, &total_paid);
        if (money_cmp(old_cost, total_paid) > 0) {
            money_sub(old_cost, total_paid, &new_cost);
        } else {
            new_cost = money_zero(cur);
        }
        new_net = !quantity_is_zero(new_qty) ? old_net : price_zero(cur);
    } else if (strcmp(type, "sell") == 0) {
        price_t avg_cost;
        if (!quantity_is_zero(old_qty)) {
            money_div_quantity(old_cost, old_qty, 6, ROUND_HALF_UP, &avg_cost);
        } else {
            avg_cost = price;
        }
        quantity_add(old_qty, qty, &new_qty);
        money_t restored_cost;
        price_times_quantity(avg_cost, qty, &restored_cost);
        money_add(old_cost, restored_cost, &new_cost);
        new_net = old_net;
    } else {
        csilk_json_free(pos);
        return 0;
    }

    money_t new_val;
    price_times_quantity(new_net, new_qty, &new_val);

    money_t delta_m;
    money_sub(new_val, old_current, &delta_m);

    if (out_position_delta) {
        *out_position_delta = delta_m;
    }

    char nq[64], nc[64], nv[64];
    quantity_to_string_fixed(new_qty, 4, nq, sizeof(nq));
    money_to_string(new_cost, nc, sizeof(nc));
    price_to_string_fixed(new_net, 4, nv, sizeof(nv));

    const char*   p[] = {nq, nc, nv, aid_str, NULL};
    csilk_json_t* upd_res = csilk_db_query_param_json(
        pool, "UPDATE assets SET quantity=?, cost_basis=?, net_value=? WHERE id=?", p);
    if (upd_res) {
        csilk_json_free(upd_res);
    }
    csilk_json_free(pos);
    return 0;
}

int
rollback_position(csilk_db_pool_t* pool,
                  int64_t          asset_id,
                  const char*      type,
                  double           amount,
                  double           fee,
                  double           price,
                  double           qty,
                  double*          out_position_delta)
{
    money_t    amount_m, fee_m, delta_m;
    price_t    price_p;
    quantity_t qty_q;

    money_from_double(amount, CURRENCY_CNY, &amount_m);
    money_from_double(fee, CURRENCY_CNY, &fee_m);
    price_from_double(price, 4, CURRENCY_CNY, &price_p);
    quantity_from_double(qty, 4, &qty_q);

    int rc = rollback_position_fc(pool, asset_id, type, amount_m, fee_m, price_p, qty_q, &delta_m);
    if (rc == 0 && out_position_delta) {
        *out_position_delta = money_to_double(delta_m);
    }
    return rc;
}
