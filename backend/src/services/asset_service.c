#include "services/asset_service.h"
#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "common/balance.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void assets_list(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    int64_t page = 1, page_size = 20;
    parse_page_params(c, &page, &page_size);

    csilk_db_pool_t* pool = db_get_pool();
    const char* cat_id = csilk_get_query(c, "category_id");

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    char limit_buf[32], offset_buf[32];
    snprintf(limit_buf, sizeof(limit_buf), "%lld", (long long)page_size);
    snprintf(offset_buf, sizeof(offset_buf), "%lld", (long long)((page - 1) * page_size));

    const char* params[8];
    const char* cnt_params[4];
    int pidx = 0;
    params[pidx++] = uid_str;
    cnt_params[0] = uid_str;
    int cnt_pidx = 1;

    char sql[512], count_sql[256];
    if (cat_id && strlen(cat_id) > 0) {
        snprintf(sql, sizeof(sql),
            "SELECT a.id, a.category_id, a.name, a.account_no, a.current_value, a.currency, "
            "a.note, a.created_at, a.updated_at, c.name as category_name, c.asset_type, "
            "a.quantity, a.cost_basis, a.net_value "
            "FROM assets a LEFT JOIN categories c ON a.category_id=c.id "
            "WHERE a.user_id=? AND a.category_id=? ORDER BY a.name LIMIT ? OFFSET ?");
        snprintf(count_sql, sizeof(count_sql),
            "SELECT COUNT(*) AS cnt FROM assets a WHERE a.user_id=? AND a.category_id=?");
        params[pidx++] = cat_id;
        cnt_params[cnt_pidx++] = cat_id;
    } else {
        snprintf(sql, sizeof(sql),
            "SELECT a.id, a.category_id, a.name, a.account_no, a.current_value, a.currency, "
            "a.note, a.created_at, a.updated_at, c.name as category_name, c.asset_type, "
            "a.quantity, a.cost_basis, a.net_value "
            "FROM assets a LEFT JOIN categories c ON a.category_id=c.id "
            "WHERE a.user_id=? ORDER BY c.name, a.name LIMIT ? OFFSET ?");
        snprintf(count_sql, sizeof(count_sql),
            "SELECT COUNT(*) AS cnt FROM assets a WHERE a.user_id=?");
    }
    params[pidx++] = limit_buf;
    params[pidx++] = offset_buf;
    params[pidx] = NULL;
    cnt_params[cnt_pidx] = NULL;

    csilk_json_t* cnt_res = csilk_db_query_param_json(pool, count_sql, cnt_params);
    int64_t total = 0;
    if (cnt_res && csilk_json_array_size(cnt_res) > 0) {
        total = db_get_int(csilk_json_array_get(cnt_res, 0), "cnt");
    }
    if (cnt_res) csilk_json_free(cnt_res);

    csilk_json_t* result = csilk_db_query_param_json(pool, sql, params);
    if (!result) { respond_error(c, 500, "查询失败"); return; }
    respond_page_ok(c, result, total, page, page_size);
}

void assets_create(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* name = csilk_json_get_string(body, "name");
    int64_t category_id = db_get_int(body, "category_id");
    if (!name || category_id <= 0) {
        csilk_json_free(body);
        respond_bad_request(c, "name 和 category_id 为必填");
        return;
    }

    const char* account_no = csilk_json_get_string(body, "account_no");
    double value = db_get_num(body, "current_value");
    const char* currency = csilk_json_get_string(body, "currency");
    if (!currency) currency = "CNY";
    const char* note = csilk_json_get_string(body, "note");
    double quantity = db_get_num(body, "quantity");
    double cost_basis = db_get_num(body, "cost_basis");
    double net_value = db_get_num(body, "net_value");

    csilk_db_pool_t* pool = db_get_pool();
    char uid_str[32], cat_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(cat_str, sizeof(cat_str), "%lld", (long long)category_id);

    // 投资类资产（stock/fund/bond/crypto）特殊处理：缺省时自动推导市值与成本
    // current_value = quantity × net_value；cost_basis 缺省（≤0）时 = 市值，
    // 保证持仓页浮动盈亏初始为 0（与 reports.c 的 market = net_value*quantity 口径一致）
    int is_investment = 0;
    {
        const char* cat_params[] = { cat_str, uid_str, NULL };
        csilk_json_t* cat_row = csilk_db_query_param_json(pool,
            "SELECT asset_type FROM categories WHERE id=? AND user_id=?", cat_params);
        if (cat_row && csilk_json_array_size(cat_row) > 0) {
            const char* atype = csilk_json_get_string(csilk_json_array_get(cat_row, 0), "asset_type");
            is_investment = (atype && (strcmp(atype, "stock") == 0 ||
                                       strcmp(atype, "fund") == 0 ||
                                       strcmp(atype, "bond") == 0 ||
                                       strcmp(atype, "crypto") == 0));
        }
        if (cat_row) csilk_json_free(cat_row);
    }
    if (is_investment && quantity > 0 && net_value > 0) {
        double market = quantity * net_value;
        if (cost_basis <= 0) cost_basis = market;
        value = market;  // 当前市值 = 份额 × 净值（忽略请求里的 current_value）
    }

    char val_str[64];
    snprintf(val_str, sizeof(val_str), "%.6f", value);

    char qty_str[64], cb_str[64], nv_str[64];
    snprintf(qty_str, sizeof(qty_str), "%.4f", quantity);
    snprintf(cb_str, sizeof(cb_str), "%.4f", cost_basis);
    snprintf(nv_str, sizeof(nv_str), "%.4f", net_value);

    const char* params[] = {
        uid_str, cat_str, name, account_no ? account_no : "", val_str, currency, note ? note : "",
        qty_str, cb_str, nv_str, NULL
    };
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "INSERT INTO assets (user_id, category_id, name, account_no, current_value, currency, note, "
        "quantity, cost_basis, net_value) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", params);

    if (!res) {
        csilk_json_free(body);
        respond_error(c, 500, "创建失败");
        return;
    }
    csilk_json_free(res);
    csilk_json_free(body);
    respond_ok_null(c);
}

void assets_update(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    csilk_db_pool_t* pool = db_get_pool();
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    // Verify ownership
    const char* chk_params[] = { id_str, uid_str, NULL };
    csilk_json_t* chk = csilk_db_query_param_json(pool,
        "SELECT id FROM assets WHERE id=? AND user_id=?", chk_params);
    if (!chk || csilk_json_array_size(chk) == 0) {
        csilk_json_free(body);
        if (chk) csilk_json_free(chk);
        respond_not_found(c);
        return;
    }
    csilk_json_free(chk);

    const char* name = csilk_json_get_string(body, "name");
    const char* account_no = csilk_json_get_string(body, "account_no");
    double value = db_get_num(body, "current_value");
    const char* currency = csilk_json_get_string(body, "currency");
    const char* note = csilk_json_get_string(body, "note");
    double net_value_input = db_get_num(body, "net_value");
    int has_net_value = csilk_json_get(body, "net_value") != NULL;
    double quantity_input = db_get_num(body, "quantity");
    int has_quantity = csilk_json_get(body, "quantity") != NULL;
    double cost_basis_input = db_get_num(body, "cost_basis");
    int has_cost_basis = csilk_json_get(body, "cost_basis") != NULL;

    // A2 补丁语义：请求体中缺失的字段保留数据库原值，避免部分更新清空列
    if (!name || !account_no || !currency || !note) {
        csilk_json_t* cur = csilk_db_query_param_json(pool,
            "SELECT name, account_no, currency, note FROM assets WHERE id=? AND user_id=?",
            chk_params);
        if (cur && csilk_json_array_size(cur) > 0) {
            const csilk_json_t* cr = csilk_json_array_get(cur, 0);
            if (!name) name = csilk_json_get_string(cr, "name");
            if (!account_no) account_no = csilk_json_get_string(cr, "account_no");
            if (!currency) currency = csilk_json_get_string(cr, "currency");
            if (!note) note = csilk_json_get_string(cr, "note");
        }
        if (cur) csilk_json_free(cur);
    }

    char val_str[64];
    snprintf(val_str, sizeof(val_str), "%.6f", value);

    // A2 净值重算：投资类资产且 body 含 net_value/quantity/cost_basis →
    // 持久化持仓字段，并重算 current_value = quantity × net_value
    int64_t asset_id_val = atoll(id_str);
    if (has_net_value || has_quantity || has_cost_basis) {
        csilk_json_t* holder = csilk_db_query_param_json(pool,
            "SELECT a.quantity, a.cost_basis, a.net_value, a.current_value, c.asset_type "
            "FROM assets a JOIN categories c ON a.category_id=c.id "
            "WHERE a.id=? AND a.user_id=?", chk_params);
        if (holder && csilk_json_array_size(holder) > 0) {
            const csilk_json_t* hr = csilk_json_array_get(holder, 0);
            const char* atype = csilk_json_get_string(hr, "asset_type");
            int is_investment = (atype && (strcmp(atype, "stock") == 0 ||
                                            strcmp(atype, "fund") == 0 ||
                                            strcmp(atype, "bond") == 0 ||
                                            strcmp(atype, "crypto") == 0));
            if (is_investment) {
                double old_qty = db_get_num(hr, "quantity");
                double old_cost = db_get_num(hr, "cost_basis");
                double old_net = db_get_num(hr, "net_value");
                double old_current = db_get_num(hr, "current_value");
                double new_qty = has_quantity ? quantity_input : old_qty;
                double new_net = has_net_value ? net_value_input : old_net;
                double new_cost = has_cost_basis ? cost_basis_input : old_cost;
                double new_current = new_qty * new_net;
                double delta = new_current - old_current;
                if (delta != 0) {
                    balance_apply_delta(pool, asset_id_val, user_id, delta,
                                        "asset_netvalue", asset_id_val, "net_value update");
                }
                // UPDATE: 仅更新持仓字段与派生值
                char nv_str[64], qty_str[64], cb_str[64];
                snprintf(nv_str, sizeof(nv_str), "%.4f", new_net);
                snprintf(qty_str, sizeof(qty_str), "%.4f", new_qty);
                snprintf(cb_str, sizeof(cb_str), "%.4f", new_cost);
                const char* upd_params[] = {
                    nv_str, qty_str, cb_str, id_str, uid_str, NULL
                };
                csilk_json_t* ur = csilk_db_query_param_json(pool,
                    "UPDATE assets SET net_value=?, quantity=?, cost_basis=?, "
                    "updated_at=CURRENT_TIMESTAMP WHERE id=? AND user_id=?", upd_params);
                if (ur) csilk_json_free(ur);
                csilk_json_free(holder);
                csilk_json_free(body);
                respond_ok_null(c);
                return;
            }
        }
        if (holder) csilk_json_free(holder);
    }

    // 普通资产：直接更新
    const char* params[] = {
        name ? name : "", account_no ? account_no : "", val_str,
        currency ? currency : "CNY", note ? note : "", id_str, uid_str, NULL
    };
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "UPDATE assets SET name=?, account_no=?, current_value=?, currency=?, note=?, "
        "updated_at=CURRENT_TIMESTAMP WHERE id=? AND user_id=?", params);
    if (res) csilk_json_free(res);

    csilk_json_free(body);
    respond_ok_null(c);
}

void assets_delete(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { id_str, uid_str, NULL };

    csilk_json_t* res = csilk_db_query_param_json(pool,
        "DELETE FROM assets WHERE id=? AND user_id=?", params);
    if (!res) {
        respond_error(c, 500, "删除失败");
        return;
    }
    csilk_json_free(res);
    respond_ok_null(c);
}

void assets_detail(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { id_str, uid_str, NULL };

    csilk_json_t* result = csilk_db_query_param_json(pool,
        "SELECT a.id, a.category_id, a.name, a.account_no, a.current_value, a.currency, "
        "a.note, a.created_at, a.updated_at, c.name as category_name, c.asset_type, "
        "a.quantity, a.cost_basis, a.net_value "
        "FROM assets a LEFT JOIN categories c ON a.category_id=c.id "
        "WHERE a.id=? AND a.user_id=?", params);

    if (!result || csilk_json_array_size(result) == 0) {
        respond_not_found(c);
        if (result) csilk_json_free(result);
        return;
    }

    csilk_json_t* row = csilk_json_array_get(result, 0);
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "id", db_get_num(row, "id"));
    csilk_json_add_number(resp, "category_id", db_get_num(row, "category_id"));
    csilk_json_add_string(resp, "name", csilk_json_get_string(row, "name"));
    csilk_json_add_string(resp, "account_no", csilk_json_get_string(row, "account_no"));
    csilk_json_add_number(resp, "current_value", db_get_num(row, "current_value"));
    csilk_json_add_string(resp, "currency", csilk_json_get_string(row, "currency"));
    csilk_json_add_string(resp, "note", csilk_json_get_string(row, "note"));
    csilk_json_add_string(resp, "category_name", csilk_json_get_string(row, "category_name"));
    csilk_json_add_string(resp, "asset_type", csilk_json_get_string(row, "asset_type"));
    csilk_json_add_number(resp, "quantity", db_get_num(row, "quantity"));
    csilk_json_add_number(resp, "cost_basis", db_get_num(row, "cost_basis"));
    csilk_json_add_number(resp, "net_value", db_get_num(row, "net_value"));
    csilk_json_add_string(resp, "created_at", csilk_json_get_string(row, "created_at"));
    csilk_json_add_string(resp, "updated_at", csilk_json_get_string(row, "updated_at"));

    // 历史交易 (spec §4.4: 资产详情 + 历史交易)
    csilk_json_t* tx_rows = csilk_db_query_param_json(pool,
        "SELECT id, asset_id, transaction_type, amount, quantity, price_per_unit, "
        "currency, transaction_date, note, created_at "
        "FROM transactions WHERE asset_id=? AND user_id=? "
        "ORDER BY transaction_date DESC", params);
    csilk_json_t* transactions = csilk_json_array();
    if (tx_rows) {
        size_t n = csilk_json_array_size(tx_rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* tr = csilk_json_array_get(tx_rows, i);
            csilk_json_t* item = csilk_json_object();
            csilk_json_add_number(item, "id", db_get_num(tr, "id"));
            csilk_json_add_number(item, "asset_id", db_get_num(tr, "asset_id"));
            csilk_json_add_string(item, "transaction_type", csilk_json_get_string(tr, "transaction_type"));
            csilk_json_add_number(item, "amount", db_get_num(tr, "amount"));
            csilk_json_add_number(item, "quantity", db_get_num(tr, "quantity"));
            csilk_json_add_number(item, "price_per_unit", db_get_num(tr, "price_per_unit"));
            csilk_json_add_string(item, "currency", csilk_json_get_string(tr, "currency"));
            csilk_json_add_string(item, "transaction_date", csilk_json_get_string(tr, "transaction_date"));
            csilk_json_add_string(item, "note", csilk_json_get_string(tr, "note"));
            csilk_json_add_string(item, "created_at", csilk_json_get_string(tr, "created_at"));
            csilk_json_array_append(transactions, item);
        }
        csilk_json_free(tx_rows);
    }
    csilk_json_add_array(resp, "transactions", transactions);
    csilk_json_free(result);

    respond_ok(c, resp);
}
