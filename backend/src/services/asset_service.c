#include "services/asset_service.h"
#include "repositories/asset_repo.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/balance.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <string.h>

void assets_list(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

    int64_t page = 1, page_size = 20;
    parse_page_params(c, &page, &page_size);

    csilk_db_pool_t* pool = db_get_pool();
    const char* cat_id = csilk_get_query(c, "category_id");
    int64_t total = 0;

    csilk_json_t* result = asset_list(pool, user_id, page, page_size, cat_id, &total);
    if (!result) { respond_error(c, 500, "查询失败"); return; }
    respond_page_ok(c, result, total, page, page_size);
}

void assets_create(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

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

    // Investment assets: derive current_value from quantity * net_value
    char* asset_type = asset_get_category_type(pool, user_id, category_id);
    int is_investment = (asset_type && (strcmp(asset_type, "stock") == 0 ||
                                        strcmp(asset_type, "fund") == 0 ||
                                        strcmp(asset_type, "bond") == 0 ||
                                        strcmp(asset_type, "crypto") == 0));
    if (is_investment && quantity > 0 && net_value > 0) {
        double market = quantity * net_value;
        if (cost_basis <= 0) cost_basis = market;
        value = market;
    }
    free(asset_type);

    int64_t id = asset_insert(pool, user_id, category_id, name, account_no, value, currency, note, quantity, cost_basis, net_value);
    csilk_json_free(body);
    if (id <= 0) { respond_error(c, 500, "创建失败"); return; }
    respond_ok_null(c);
}

void assets_update(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    int64_t asset_id = atoll(id_str);
    if (!asset_exists(db_get_pool(), user_id, asset_id)) {
        csilk_json_free(body);
        respond_not_found(c);
        return;
    }

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

    csilk_db_pool_t* pool = db_get_pool();

    // A2 patch: preserve DB values for missing fields
    if (!name || !account_no || !currency || !note) {
        csilk_json_t* cur = asset_get(pool, user_id, asset_id);
        if (cur && csilk_json_array_size(cur) > 0) {
            const csilk_json_t* cr = csilk_json_array_get(cur, 0);
            if (!name) name = csilk_json_get_string(cr, "name");
            if (!account_no) account_no = csilk_json_get_string(cr, "account_no");
            if (!currency) currency = csilk_json_get_string(cr, "currency");
            if (!note) note = csilk_json_get_string(cr, "note");
        }
        if (cur) csilk_json_free(cur);
    }

    // Investment asset position update
    if (has_net_value || has_quantity || has_cost_basis) {
        char* asset_type = asset_get_category_type(pool, user_id, asset_id);
        int is_investment = (asset_type && (strcmp(asset_type, "stock") == 0 ||
                                            strcmp(asset_type, "fund") == 0 ||
                                            strcmp(asset_type, "bond") == 0 ||
                                            strcmp(asset_type, "crypto") == 0));
        if (is_investment) {
            csilk_json_t* holder = asset_get(pool, user_id, asset_id);
            if (holder && csilk_json_array_size(holder) > 0) {
                const csilk_json_t* hr = csilk_json_array_get(holder, 0);
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
                    balance_apply_delta(pool, asset_id, user_id, delta, "asset_netvalue", asset_id, "net_value update");
                }
                asset_update_position(pool, user_id, asset_id, new_net, new_qty, new_cost);
            }
            if (holder) csilk_json_free(holder);
        }
        free(asset_type);
        csilk_json_free(body);
        respond_ok_null(c);
        return;
    }

    // Normal asset update
    if (!asset_update_basic(pool, user_id, asset_id, name, account_no, value, currency, note)) {
        csilk_json_free(body);
        respond_not_found(c);
        return;
    }
    csilk_json_free(body);
    respond_ok_null(c);
}

void assets_delete(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();
    int64_t asset_id = atoll(id_str);
    if (!asset_delete(pool, user_id, asset_id)) {
        respond_not_found(c);
        return;
    }
    respond_ok_null(c);
}

void assets_detail(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();
    int64_t asset_id = atoll(id_str);

    csilk_json_t* result = asset_get(pool, user_id, asset_id);
    if (!result || csilk_json_array_size(result) == 0) {
        if (result) csilk_json_free(result);
        respond_not_found(c);
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

    csilk_json_t* tx_rows = asset_transactions(pool, user_id, asset_id);
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

void register_asset_routes(csilk_app_t* app) {
    csilk_app_get_ext(app, "/api/assets", assets_list, nullptr, "asset_resp_t", "List assets", "Returns paginated list of user's assets with optional category filter");
    csilk_app_post_ext(app, "/api/assets", assets_create, "asset_req_t", "asset_resp_t", "Create asset", "Create a new asset (cash, investment, liability, etc.)");
    csilk_app_put_ext(app, "/api/assets/:id", assets_update, "asset_req_t", "asset_resp_t", "Update asset", "Update an existing asset by ID; investment assets recalculate position on net_value change");
    csilk_app_delete_ext(app, "/api/assets/:id", assets_delete, nullptr, nullptr, "Delete asset", "Delete an asset and its associated transactions by ID");
    csilk_app_get_ext(app, "/api/assets/:id", assets_detail, nullptr, "asset_resp_t", "Get asset detail", "Returns full asset details including linked transaction history");
}
