#include "interfaces/http/controllers/transaction_controller.h"
#include "application/transaction/usecases.h"
#include "application/transaction/commands.h"
#include "application/transaction/dtos.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
api_transactions_create(csilk_ctx_t* c)
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

    const char* type = csilk_json_get_string(body, "transaction_type");
    if (!type || strlen(type) == 0) {
        type = csilk_json_get_string(body, "type");
    }

    const char* src_type = csilk_json_get_string(body, "source_type");
    if (!src_type) {
        src_type = "expense";
    }

    create_tx_cmd_t cmd = {
        .user_id = user_id,
        .asset_id = db_get_int(body, "asset_id"),
        .linked_asset_id = db_get_int(body, "linked_asset_id"),
        .category_id = db_get_int(body, "category_id"),
        .type = type,
        .amount = db_get_num(body, "amount"),
        .price = db_get_num(body, "price_per_unit"),
        .quantity = db_get_num(body, "quantity"),
        .fee = db_get_num(body, "fee"),
        .currency = csilk_json_get_string(body, "currency"),
        .note = csilk_json_get_string(body, "note"),
        .date = csilk_json_get_string(body, "transaction_date"),
        .source_type = src_type,
    };

    tx_usecase_result_t res = {0};
    int                 rc = tx_usecase_create(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        csilk_json_t* d = csilk_json_object();
        csilk_json_add_number(d, "id", (double)res.created_id);
        respond_ok(c, d);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else if (res.code == 1002) {
        respond_bad_request(c, res.message);
    } else {
        respond_error(c, 500, res.message[0] ? res.message : "创建交易失败");
    }
}

void
api_transactions_update(csilk_ctx_t* c)
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

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    const char* type = csilk_json_get_string(body, "transaction_type");
    if (!type || strlen(type) == 0) {
        type = csilk_json_get_string(body, "type");
    }

    update_tx_cmd_t cmd = {
        .user_id = user_id,
        .tx_id = tx_id_val,
        .asset_id = db_get_int(body, "asset_id"),
        .linked_asset_id = db_get_int(body, "linked_asset_id"),
        .category_id = db_get_int(body, "category_id"),
        .type = type,
        .amount = db_get_num(body, "amount"),
        .price = db_get_num(body, "price_per_unit"),
        .quantity = db_get_num(body, "quantity"),
        .fee = db_get_num(body, "fee"),
        .currency = csilk_json_get_string(body, "currency"),
        .note = csilk_json_get_string(body, "note"),
        .date = csilk_json_get_string(body, "transaction_date"),
    };

    tx_usecase_result_t res = {0};
    int                 rc = tx_usecase_update(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else if (res.code == 1002) {
        respond_bad_request(c, res.message);
    } else {
        respond_error(c, 500, res.message[0] ? res.message : "更新交易失败");
    }
}

void
api_transactions_delete(csilk_ctx_t* c)
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

    delete_tx_cmd_t cmd = {
        .user_id = user_id,
        .tx_id = tx_id_val,
    };

    tx_usecase_result_t res = {0};
    int                 rc = tx_usecase_delete(db_get_pool(), &cmd, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else if (res.code == 1002) {
        respond_bad_request(c, res.message);
    } else {
        respond_error(c, 500, res.message[0] ? res.message : "删除交易失败");
    }
}

void
api_transactions_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t page = 1, page_size = 20;
    parse_page_params(c, &page, &page_size);

    const char* aid_str = csilk_get_query(c, "asset_id");
    const char* cid_str = csilk_get_query(c, "category_id");
    const char* type = csilk_get_query(c, "transaction_type");
    if (!type || strlen(type) == 0) {
        type = csilk_get_query(c, "type");
    }
    const char* source_type = csilk_get_query(c, "source_type");
    const char* start_date = csilk_get_query(c, "start_date");
    const char* end_date = csilk_get_query(c, "end_date");

    query_tx_filter_t filter = {
        .user_id = user_id,
        .page = page,
        .page_size = page_size,
        .asset_id = aid_str ? atoll(aid_str) : 0,
        .category_id = cid_str ? atoll(cid_str) : 0,
        .type = type,
        .source_type = source_type,
        .start_date = start_date,
        .end_date = end_date,
    };

    tx_usecase_result_t res = {0};
    int                 rc = tx_usecase_query(db_get_pool(), &filter, &res);
    if (rc != 0 || res.code != 0) {
        respond_error(c, 500, res.message[0] ? res.message : "查询交易列表失败");
        return;
    }

    csilk_json_t* list = (csilk_json_t*)res.data_payload;
    respond_page_ok(c, list ? list : csilk_json_array(), res.total, page, page_size);
}

void
api_transactions_monthly(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char*         month = csilk_get_query(c, "month");
    tx_usecase_result_t res = {0};
    int                 rc = tx_usecase_monthly(db_get_pool(), user_id, month, &res);
    if (rc != 0 || res.code != 0) {
        if (res.code == 1002) {
            respond_bad_request(c, res.message);
        } else {
            respond_error(c, 500, res.message[0] ? res.message : "查询失败");
        }
        return;
    }

    respond_ok(c, (csilk_json_t*)res.data_payload);
}

void
register_transaction_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/transactions",
                      api_transactions_list,
                      nullptr,
                      "transaction_resp_t",
                      "List transactions",
                      "Returns paginated transaction list");
    csilk_app_get_ext(app,
                      "/api/transactions/monthly",
                      api_transactions_monthly,
                      nullptr,
                      nullptr,
                      "Monthly transaction summary",
                      "Returns monthly aggregated transaction totals");
    csilk_app_post_ext(app,
                       "/api/transactions",
                       api_transactions_create,
                       "transaction_req_t",
                       "transaction_resp_t",
                       "Create transaction",
                       "Create a new transaction");
    csilk_app_put_ext(app,
                      "/api/transactions/:id",
                      api_transactions_update,
                      "transaction_req_t",
                      "transaction_resp_t",
                      "Update transaction",
                      "Update an existing transaction");
    csilk_app_delete_ext(app,
                         "/api/transactions/:id",
                         api_transactions_delete,
                         nullptr,
                         nullptr,
                         "Delete transaction",
                         "Delete a transaction");
}

void
register_interfaces_transaction_routes(csilk_app_t* app)
{
    register_transaction_routes(app);
}
