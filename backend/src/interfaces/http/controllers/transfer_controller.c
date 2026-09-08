/**
 * @file transfer_controller.c
 * @brief 转账 HTTP 控制器 (Interfaces Layer)
 */

#include "interfaces/http/controllers/transfer_controller.h"
#include "application/transfer/usecases.h"
#include "application/transfer/commands.h"
#include "domain/transfer/entity.h"
#include "infrastructure/repositories/transfer_repo_impl.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <string.h>

void
api_transfers_create(csilk_ctx_t* c)
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

    create_transfer_cmd_t cmd = {
        .user_id = user_id,
        .from_asset_id = db_get_int(body, "from_asset_id"),
        .to_asset_id = db_get_int(body, "to_asset_id"),
        .amount = db_get_num(body, "amount"),
        .currency = csilk_json_get_string(body, "currency"),
        .note = csilk_json_get_string(body, "note"),
    };

    /* Try multiple date field names */
    const char* date = csilk_json_get_string(body, "transfer_date");
    if (!date || date[0] == '\0') {
        date = csilk_json_get_string(body, "transaction_date");
    }
    if (!date || date[0] == '\0') {
        date = csilk_json_get_string(body, "date");
    }
    cmd.transfer_date = date;

    transfer_usecase_result_t res = {0};
    int                       rc = transfer_usecase_create(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message[0] ? res.message : "转账失败");
    }
}

/* Backward-compatibility alias */
void
transfers_create(csilk_ctx_t* c)
{
    api_transfers_create(c);
}

void
register_transfer_routes(csilk_app_t* app)
{
    csilk_app_post_ext(app,
                       "/api/transfers",
                       api_transfers_create,
                       "transfer_req_t",
                       nullptr,
                       "Create transfer",
                       "Create a transfer between two assets (debit one, credit other)");
}
