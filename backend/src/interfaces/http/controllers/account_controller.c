#include "interfaces/http/controllers/account_controller.h"
#include "application/account/commands.h"
#include "application/account/dtos.h"
#include "application/account/usecases.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/response.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
api_accounts_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    /* 账本缺省 → 默认账本；查看侧成员即可（viewer+），写操作限 owner */
    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
        return;
    }
    int         include_archived = 0;
    const char* inc_str = csilk_get_query(c, "include_archived");
    if (inc_str) {
        include_archived = (int)parse_query_int_strict(inc_str, 0) != 0;
    }

    csilk_json_t* rows = NULL;
    if (account_usecase_query(db_get_pool(), user_id, ledger_id, include_archived, &rows, NULL) !=
            0 ||
        !rows) {
        respond_error(c, 500, "查询失败");
        return;
    }
    /* 列表无分页语义：全部行一次返回；respond_ok 接管所有权 */
    respond_ok(c, rows);
}

void
api_accounts_create(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "owner");
    if (ledger_id < 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    create_account_cmd_t cmd = {
        .user_id = user_id,
        .ledger_id = db_get_int(body, "ledger_id") > 0 ? db_get_int(body, "ledger_id") : ledger_id,
        .name = csilk_json_get_string(body, "name"),
        .institution = csilk_json_get_string(body, "institution"),
        .account_no = csilk_json_get_string(body, "account_no"),
        .currency = csilk_json_get_string(body, "currency"),
        .icon = csilk_json_get_string(body, "icon"),
        .note = csilk_json_get_string(body, "note"),
        .sort_order = db_get_int(body, "sort_order"),
    };

    account_usecase_result_t res = {0};
    int                      rc = account_usecase_create(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        csilk_json_t* data = csilk_json_object();
        csilk_json_add_number(data, "id", (double)res.id);
        respond_ok(c, data);
    } else if (res.code == 1002) {
        respond_bad_request(c, res.message);
    } else if (res.code == 1004) {
        respond_conflict(c, res.message);
    } else {
        respond_error(c, 500, res.message[0] ? res.message : "创建失败");
    }
}

void
api_accounts_update(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "owner");
    if (ledger_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     aid = parse_query_int_strict(id_str, -1);
    if (aid <= 0) {
        respond_bad_request(c, "缺少或无效的 id");
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    int has_archived = csilk_json_get(body, "archived") != NULL;

    update_account_cmd_t cmd = {
        .user_id = user_id,
        .id = aid,
        .name = csilk_json_get_string(body, "name"),
        .institution = csilk_json_get_string(body, "institution"),
        .account_no = csilk_json_get_string(body, "account_no"),
        .currency = csilk_json_get_string(body, "currency"),
        .icon = csilk_json_get_string(body, "icon"),
        .note = csilk_json_get_string(body, "note"),
        .has_sort_order = csilk_json_get(body, "sort_order") != NULL,
        .sort_order = db_get_int(body, "sort_order"),
        .has_archived = has_archived,
        .archived = db_get_int(body, "archived") != 0,
    };

    account_usecase_result_t res = {0};
    int                      rc = account_usecase_update(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, NULL);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else if (res.code == 1002) {
        respond_bad_request(c, res.message);
    } else {
        respond_error(c, 500, res.message[0] ? res.message : "更新失败");
    }
}

void
api_accounts_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "owner");
    if (ledger_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     aid = parse_query_int_strict(id_str, -1);
    if (aid <= 0) {
        respond_bad_request(c, "缺少或无效的 id");
        return;
    }

    delete_account_cmd_t cmd = {
        .user_id = user_id,
        .id = aid,
    };

    account_usecase_result_t res = {0};
    int                      rc = account_usecase_delete(db_get_pool(), &cmd, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, NULL);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else if (res.code == 1004) {
        respond_error(c, 1004, res.message);
    } else if (res.code == 1002) {
        respond_bad_request(c, res.message);
    } else {
        respond_error(c, 500, res.message[0] ? res.message : "删除失败");
    }
}

void
register_account_http_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/accounts",
                      api_accounts_list,
                      NULL,
                      "account_resp_t",
                      "List accounts",
                      "Returns accounts under the active ledger with asset_count and total_value");
    csilk_app_post_ext(app,
                       "/api/accounts",
                       api_accounts_create,
                       "account_req_t",
                       "account_resp_t",
                       "Create account",
                       "Create a new account (bank card, platform, broker, etc.)");
    csilk_app_put_ext(app,
                      "/api/accounts/:id",
                      api_accounts_update,
                      "account_req_t",
                      "account_resp_t",
                      "Update account",
                      "Update account fields; archived=true archives, false restores");
    csilk_app_delete_ext(app,
                         "/api/accounts/:id",
                         api_accounts_delete,
                         NULL,
                         NULL,
                         "Delete account",
                         "Delete an account; refused when it still holds assets or transactions");
}

void
register_account_routes(csilk_app_t* app)
{
    register_account_http_routes(app);
}
