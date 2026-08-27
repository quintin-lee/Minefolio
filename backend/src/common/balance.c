#include "common/balance.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <string.h>

int
balance_direction(const char* asset_type)
{
    if (!asset_type) {
        return 1;
    }
    if (strcmp(asset_type, "loan") == 0 || strcmp(asset_type, "credit_card") == 0 ||
        strcmp(asset_type, "other_liability") == 0) {
        return -1;
    }
    return 1;
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
    if (!pool || asset_id <= 0 || user_id <= 0 || !source_type) {
        return -1;
    }

    char asset_id_str[32], user_id_str[32];
    snprintf(asset_id_str, sizeof(asset_id_str), "%lld", (long long)asset_id);
    snprintf(user_id_str, sizeof(user_id_str), "%lld", (long long)user_id);

    // 1. 查询资产归属与类型（asset_type 存于 categories，必须 JOIN）
    const char*   params1[] = {asset_id_str, user_id_str, NULL};
    csilk_json_t* row =
        csilk_db_query_param_json(pool,
                                  "SELECT a.current_value, c.asset_type "
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

    // 2. 归一化 delta（负债方向反转）
    double signed_delta = delta * balance_direction(asset_type);
    CSILK_LOG_D("balance_apply_delta asset_id=%lld delta=%.6f asset_type=%s signed_delta=%.6f "
                "source=%s id=%lld",
                (long long)asset_id,
                delta,
                asset_type ? asset_type : "(null)",
                signed_delta,
                source_type,
                (long long)source_id);
    csilk_json_free(row);

    // 3. 原子更新余额（避免读改写竞态）
    char signed_delta_str[64];
    snprintf(signed_delta_str, sizeof(signed_delta_str), "%.6f", signed_delta);
    const char*   params3[] = {signed_delta_str, asset_id_str, user_id_str, NULL};
    csilk_json_t* res3 =
        csilk_db_query_param_json(pool,
                                  "UPDATE assets SET current_value = current_value + ?, "
                                  "updated_at = CURRENT_TIMESTAMP WHERE id=? AND user_id=?",
                                  params3);
    if (res3) {
        csilk_json_free(res3);
    }

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
    double balance_after = db_get_num(csilk_json_array_get(after, 0), "current_value");
    csilk_json_free(after);

    // 5. 写审计日志（delta 存已反转的 signed_delta）
    char balance_after_str[64], source_id_str[32];
    snprintf(balance_after_str, sizeof(balance_after_str), "%.6f", balance_after);
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
    if (res5) {
        csilk_json_free(res5);
    }

    return 0;
}

int
is_investment_type(const char* atype)
{
    return atype && (strcmp(atype, "stock") == 0 || strcmp(atype, "fund") == 0 ||
                     strcmp(atype, "bond") == 0 || strcmp(atype, "crypto") == 0);
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
    char aid_str[32];
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    csilk_json_t* pos = csilk_db_query_param_json(
        pool,
        "SELECT a.quantity, a.cost_basis, a.net_value, a.current_value, c.asset_type "
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
    double old_qty = db_get_num(pr, "quantity");
    double old_cost = db_get_num(pr, "cost_basis");
    double old_net = db_get_num(pr, "net_value");
    double old_current = db_get_num(pr, "current_value");
    double new_qty, new_cost, new_net;

    CSILK_LOG_I("apply_position asset_id=%lld type=%s old_qty=%.4f old_cost=%.4f "
                "old_net=%.4f old_current=%.4f amount=%.2f fee=%.2f price=%.4f qty=%.4f",
                (long long)asset_id,
                type,
                old_qty,
                old_cost,
                old_net,
                old_current,
                amount,
                fee,
                price,
                qty);

    if (strcmp(type, "buy") == 0) {
        new_qty = old_qty + qty;
        new_cost = old_cost + amount + fee;
        new_net = price;
    } else if (strcmp(type, "sell") == 0) {
        if (old_qty < qty) {
            csilk_json_free(pos);
            CSILK_LOG_W("apply_position: insufficient quantity asset_id=%lld have=%.4f need=%.4f",
                        (long long)asset_id,
                        old_qty,
                        qty);
            return -1;
        }
        double avg_cost = old_qty > 0 ? old_cost / old_qty : 0;
        new_qty = old_qty - qty;
        new_cost = old_cost - qty * avg_cost;
        new_net = old_net;
    } else {
        csilk_json_free(pos);
        return 0;
    }
    double delta = new_qty * new_net - old_current;
    if (out_position_delta) {
        *out_position_delta = delta;
    }
    CSILK_LOG_I("apply_position asset_id=%lld type=%s new_qty=%.4f new_cost=%.4f "
                "new_net=%.4f delta=%.6f",
                (long long)asset_id,
                type,
                new_qty,
                new_cost,
                new_net,
                delta);

    char nq[64], nc[64], nv[64];
    snprintf(nq, sizeof(nq), "%.4f", new_qty);
    snprintf(nc, sizeof(nc), "%.4f", new_cost);
    snprintf(nv, sizeof(nv), "%.4f", new_net);
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
    if (!pool || asset_id <= 0 || !type) {
        return 0;
    }

    char aid_str[32];
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    csilk_json_t* pos = csilk_db_query_param_json(
        pool,
        "SELECT a.quantity, a.cost_basis, a.net_value, a.current_value, c.asset_type "
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
    double old_qty = db_get_num(pr, "quantity");
    double old_cost = db_get_num(pr, "cost_basis");
    double old_net = db_get_num(pr, "net_value");
    double old_current = db_get_num(pr, "current_value");
    double new_qty, new_cost, new_net;

    if (strcmp(type, "buy") == 0) {
        new_qty = (old_qty > qty) ? (old_qty - qty) : 0;
        new_cost = (old_cost > (amount + fee)) ? (old_cost - (amount + fee)) : 0;
        new_net = (new_qty > 0) ? old_net : 0;
    } else if (strcmp(type, "sell") == 0) {
        double avg_cost = (old_qty > 0) ? (old_cost / old_qty) : price;
        new_qty = old_qty + qty;
        new_cost = old_cost + qty * avg_cost;
        new_net = old_net;
    } else {
        csilk_json_free(pos);
        return 0;
    }
    double delta = new_qty * new_net - old_current;
    if (out_position_delta) {
        *out_position_delta = delta;
    }

    char nq[64], nc[64], nv[64];
    snprintf(nq, sizeof(nq), "%.4f", new_qty);
    snprintf(nc, sizeof(nc), "%.4f", new_cost);
    snprintf(nv, sizeof(nv), "%.4f", new_net);
    const char*   p[] = {nq, nc, nv, aid_str, NULL};
    csilk_json_t* upd_res = csilk_db_query_param_json(
        pool, "UPDATE assets SET quantity=?, cost_basis=?, net_value=? WHERE id=?", p);
    if (upd_res) {
        csilk_json_free(upd_res);
    }
    csilk_json_free(pos);
    return 0;
}
