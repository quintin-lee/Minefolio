#include "interfaces/http/controllers/asset_controller.h"
#include "application/asset/commands.h"
#include "application/asset/dtos.h"
#include "application/asset/usecases.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/response.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
api_assets_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t page = 1, page_size = 20;
    parse_page_params(c, &page, &page_size);

    query_asset_filter_t filter = {
        .user_id = user_id,
        .category_id = csilk_get_query(c, "category_id"),
        .page = page,
        .page_size = page_size,
    };

    csilk_json_t* result = NULL;
    int64_t       total = 0;
    if (asset_usecase_query(db_get_pool(), &filter, &result, &total) != 0 || !result) {
        respond_error(c, 500, "查询失败");
        return;
    }
    respond_page_ok(c, result, total, page, page_size);
}

void
api_assets_create(csilk_ctx_t* c)
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

    create_asset_cmd_t cmd = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .category_id = db_get_int(body, "category_id"),
        .name = csilk_json_get_string(body, "name"),
        .account_no = csilk_json_get_string(body, "account_no"),
        .current_value = db_get_num(body, "current_value"),
        .currency = csilk_json_get_string(body, "currency"),
        .note = csilk_json_get_string(body, "note"),
        .quantity = db_get_num(body, "quantity"),
        .cost_basis = db_get_num(body, "cost_basis"),
        .net_value = db_get_num(body, "net_value"),
        .symbol = csilk_json_get_string(body, "symbol"),
        .quote_source = csilk_json_get_string(body, "quote_source"),
    };

    asset_usecase_result_t res = {0};
    int                    rc = asset_usecase_create(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else if (res.code == 1002) {
        respond_bad_request(c, res.message);
    } else {
        respond_error(c, 500, res.message[0] ? res.message : "创建失败");
    }
}

void
api_assets_update(csilk_ctx_t* c)
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

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    update_asset_cmd_t cmd = {
        .user_id = user_id,
        .id = atoll(id_str),
        .name = csilk_json_get_string(body, "name"),
        .account_no = csilk_json_get_string(body, "account_no"),
        .current_value = db_get_num(body, "current_value"),
        .currency = csilk_json_get_string(body, "currency"),
        .note = csilk_json_get_string(body, "note"),
        .has_quantity = (csilk_json_get(body, "quantity") != NULL),
        .quantity = db_get_num(body, "quantity"),
        .has_cost_basis = (csilk_json_get(body, "cost_basis") != NULL),
        .cost_basis = db_get_num(body, "cost_basis"),
        .has_net_value = (csilk_json_get(body, "net_value") != NULL),
        .net_value = db_get_num(body, "net_value"),
        .symbol = csilk_json_get_string(body, "symbol"),
        .quote_source = csilk_json_get_string(body, "quote_source"),
    };

    asset_usecase_result_t res = {0};
    int                    rc = asset_usecase_update(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else if (res.code == 1002) {
        respond_bad_request(c, res.message);
    } else {
        respond_error(c, 500, res.message[0] ? res.message : "更新失败");
    }
}

void
api_assets_delete(csilk_ctx_t* c)
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

    delete_asset_cmd_t cmd = {
        .user_id = user_id,
        .id = atoll(id_str),
    };

    asset_usecase_result_t res = {0};
    int                    rc = asset_usecase_delete(db_get_pool(), &cmd, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_bad_request(c, res.message);
    }
}

void
api_assets_detail(csilk_ctx_t* c)
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

    csilk_json_t* resp = NULL;
    int           rc = asset_usecase_get(db_get_pool(), user_id, atoll(id_str), &resp);
    if (rc == 1003 || !resp) {
        respond_not_found(c);
        return;
    }
    respond_ok(c, resp);
}

void
api_assets_logs_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t page = 1, page_size = 20;
    parse_page_params(c, &page, &page_size);

    query_asset_log_filter_t filter = {
        .user_id = user_id,
        .asset_id = csilk_get_query(c, "asset_id"),
        .page = page,
        .page_size = page_size,
    };

    csilk_json_t* result = NULL;
    int64_t       total = 0;
    if (asset_usecase_query_logs(db_get_pool(), &filter, &result, &total) != 0 || !result) {
        respond_error(c, 500, "查询失败");
        return;
    }
    respond_page_ok(c, result, total, page, page_size);
}

void
api_assets_rebuild_single(csilk_ctx_t* c)
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

    csilk_json_t*          data = NULL;
    asset_usecase_result_t res = {0};
    int rc = asset_usecase_rebuild_single(db_get_pool(), user_id, atoll(id_str), &data, &res);

    if (rc == 0 && res.code == 0 && data) {
        respond_ok(c, data);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_error(c, 500, res.message[0] ? res.message : "重建失败");
    }
}

void
api_assets_rebuild_all(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    asset_usecase_result_t res = {0};
    int                    rc = asset_usecase_rebuild_all(db_get_pool(), user_id, &res);

    if (rc == 0 && res.code == 0) {
        csilk_json_t* data = csilk_json_object();
        csilk_json_add_string(data, "status", "rebuilt");
        respond_ok(c, data);
    } else {
        respond_error(c, 500, "组合账本重建失败");
    }
}

void
register_asset_http_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/assets",
                      api_assets_list,
                      nullptr,
                      "asset_resp_t",
                      "List assets",
                      "Returns paginated list of user's assets with optional category filter");
    csilk_app_post_ext(app,
                       "/api/assets",
                       api_assets_create,
                       "asset_req_t",
                       "asset_resp_t",
                       "Create asset",
                       "Create a new asset (cash, investment, liability, etc.)");
    csilk_app_post_ext(app,
                       "/api/assets/rebuild",
                       api_assets_rebuild_all,
                       nullptr,
                       nullptr,
                       "Rebuild portfolio assets",
                       "Rebuild all assets and portfolio positions from transaction facts");
    csilk_app_post_ext(app,
                       "/api/assets/:id/rebuild",
                       api_assets_rebuild_single,
                       nullptr,
                       nullptr,
                       "Rebuild single asset",
                       "Rebuild a single asset position/balance state from transaction facts");
    csilk_app_put_ext(app,
                      "/api/assets/:id",
                      api_assets_update,
                      "asset_req_t",
                      "asset_resp_t",
                      "Update asset",
                      "Update an existing asset by ID; investment assets recalculate position on "
                      "net_value change");
    csilk_app_delete_ext(app,
                         "/api/assets/:id",
                         api_assets_delete,
                         nullptr,
                         nullptr,
                         "Delete asset",
                         "Delete an asset and its associated transactions by ID");
    csilk_app_get_ext(app,
                      "/api/assets/:id",
                      api_assets_detail,
                      nullptr,
                      "asset_resp_t",
                      "Get asset detail",
                      "Returns full asset details including linked transaction history");
    csilk_app_get_ext(app,
                      "/api/asset-balance-logs",
                      api_assets_logs_list,
                      nullptr,
                      nullptr,
                      "Asset balance logs",
                      "Returns paginated asset balance change logs with optional asset_id filter");
}
