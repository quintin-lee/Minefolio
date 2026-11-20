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
    respond_ok(c, rows);
}

void
api_accounts_get(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     aid = parse_query_int_strict(id_str, -1);
    if (aid <= 0) {
        respond_bad_request(c, "缺少或无效的 id");
        return;
    }

    csilk_json_t*            obj = NULL;
    account_usecase_result_t res = {0};
    int rc = account_usecase_get(db_get_pool(), user_id, ledger_id, aid, &obj, &res);
    if (rc == 0 && res.code == 0 && obj) {
        respond_ok(c, obj);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else if (res.code == 1004) {
        respond_error(c, 1004, res.message[0] ? res.message : "Forbidden");
    } else {
        respond_error(c, 500, "查询失败");
    }
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
        csilk_json_add_int(data, "id", res.id);
        respond_ok(c, data);
    } else if (res.code == 1004) {
        respond_error(c, 1004, res.message);
    } else if (res.code == 1002) {
        respond_bad_request(c, res.message);
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

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
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

    int has_sort = csilk_json_get(body, "sort_order") != NULL;
    int has_arch = csilk_json_get(body, "archived") != NULL;

    update_account_cmd_t cmd = {
        .user_id = user_id,
        .id = aid,
        .name = csilk_json_get_string(body, "name"),
        .institution = csilk_json_get_string(body, "institution"),
        .account_no = csilk_json_get_string(body, "account_no"),
        .currency = csilk_json_get_string(body, "currency"),
        .icon = csilk_json_get_string(body, "icon"),
        .note = csilk_json_get_string(body, "note"),
        .sort_order = db_get_int(body, "sort_order"),
        .has_sort_order = has_sort,
        .archived = db_get_int(body, "archived"),
        .has_archived = has_arch,
    };

    account_usecase_result_t res = {0};
    int                      rc = account_usecase_update(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        csilk_json_t* data = csilk_json_object();
        csilk_json_add_int(data, "id", res.id);
        respond_ok(c, data);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else if (res.code == 1004) {
        respond_error(c, 1004, res.message);
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

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
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

/* ── 账户 RBAC 细粒度权限控制端点 ────────────────────────────── */

void
api_account_permissions_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* lid_str = csilk_get_param(c, "ledger_id");
    int64_t     lid = parse_query_int_strict(lid_str, -1);
    const char* aid_str = csilk_get_param(c, "id");
    int64_t     aid = parse_query_int_strict(aid_str, -1);

    if (lid <= 0 || aid <= 0) {
        respond_bad_request(c, "无效的 ledger_id 或 account_id");
        return;
    }

    csilk_json_t*            list = NULL;
    account_usecase_result_t res = {0};
    int                      rc =
        account_permission_usecase_list_by_account(db_get_pool(), user_id, lid, aid, &list, &res);
    if (rc == 0 && res.code == 0) {
        respond_ok(c, list);
    } else if (res.code == 1004) {
        respond_error(c, 1004, res.message[0] ? res.message : "Forbidden");
    } else {
        respond_error(c, 500, "查询权限列表失败");
    }
}

void
api_account_permissions_batch_set(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* lid_str = csilk_get_param(c, "ledger_id");
    int64_t     lid = parse_query_int_strict(lid_str, -1);
    const char* aid_str = csilk_get_param(c, "id");
    int64_t     aid = parse_query_int_strict(aid_str, -1);

    if (lid <= 0 || aid <= 0) {
        respond_bad_request(c, "无效的 ledger_id 或 account_id");
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    const csilk_json_t* perms =
        csilk_json_is_array(body) ? body : csilk_json_get(body, "permissions");
    if (!perms || !csilk_json_is_array(perms)) {
        csilk_json_free(body);
        respond_bad_request(c, "缺少 permissions 数组");
        return;
    }

    account_usecase_result_t res = {0};
    int rc = account_permission_usecase_batch_set(db_get_pool(), user_id, lid, aid, perms, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, NULL);
    } else if (res.code == 1004) {
        respond_error(c, 1004, res.message[0] ? res.message : "Forbidden");
    } else {
        respond_error(c, 500, res.message[0] ? res.message : "保存权限失败");
    }
}

void
api_member_account_permissions_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* lid_str = csilk_get_param(c, "ledger_id");
    int64_t     lid = parse_query_int_strict(lid_str, -1);
    const char* uid_str = csilk_get_param(c, "user_id");
    int64_t     target_uid = parse_query_int_strict(uid_str, -1);

    if (lid <= 0 || target_uid <= 0) {
        respond_bad_request(c, "无效的 ledger_id 或 user_id");
        return;
    }

    csilk_json_t*            list = NULL;
    account_usecase_result_t res = {0};
    int                      rc = account_permission_usecase_list_by_user(
        db_get_pool(), user_id, lid, target_uid, &list, &res);
    if (rc == 0 && res.code == 0) {
        respond_ok(c, list);
    } else if (res.code == 1004) {
        respond_error(c, 1004, res.message[0] ? res.message : "Forbidden");
    } else {
        respond_error(c, 500, "查询成员权限列表失败");
    }
}

void
api_member_account_permissions_batch_set(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* lid_str = csilk_get_param(c, "ledger_id");
    int64_t     lid = parse_query_int_strict(lid_str, -1);
    const char* uid_str = csilk_get_param(c, "user_id");
    int64_t     target_uid = parse_query_int_strict(uid_str, -1);

    if (lid <= 0 || target_uid <= 0) {
        respond_bad_request(c, "无效的 ledger_id 或 user_id");
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    const csilk_json_t* perms =
        csilk_json_is_array(body) ? body : csilk_json_get(body, "permissions");
    if (!perms || !csilk_json_is_array(perms)) {
        csilk_json_free(body);
        respond_bad_request(c, "缺少 permissions 数组");
        return;
    }

    account_usecase_result_t res = {0};
    int                      rc = account_permission_usecase_batch_set_for_user(
        db_get_pool(), user_id, lid, target_uid, perms, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, NULL);
    } else if (res.code == 1004) {
        respond_error(c, 1004, res.message[0] ? res.message : "Forbidden");
    } else {
        respond_error(c, 500, res.message[0] ? res.message : "保存权限失败");
    }
}

void
api_account_permissions_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* lid_str = csilk_get_param(c, "ledger_id");
    int64_t     lid = parse_query_int_strict(lid_str, -1);
    const char* aid_str = csilk_get_param(c, "id");
    int64_t     aid = parse_query_int_strict(aid_str, -1);
    const char* uid_str = csilk_get_param(c, "user_id");
    int64_t     target_uid = parse_query_int_strict(uid_str, -1);

    if (lid <= 0 || aid <= 0 || target_uid <= 0) {
        respond_bad_request(c, "参数不完整");
        return;
    }

    account_usecase_result_t res = {0};
    int rc = account_permission_usecase_delete(db_get_pool(), user_id, lid, aid, target_uid, &res);
    if (rc == 0 && res.code == 0) {
        respond_ok(c, NULL);
    } else if (res.code == 1004) {
        respond_error(c, 1004, res.message[0] ? res.message : "Forbidden");
    } else {
        respond_error(c, 500, "删除权限配置失败");
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
    csilk_app_get_ext(
        app,
        "/api/accounts/:id",
        api_accounts_get,
        NULL,
        "account_resp_t",
        "Get account",
        "Returns account detail with permission info and masked fields if applicable");
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

    /* 细粒度 RBAC 权限管理路由 */
    csilk_app_get_ext(app,
                      "/api/ledgers/:ledger_id/accounts/:id/permissions",
                      api_account_permissions_list,
                      NULL,
                      NULL,
                      "List account permissions",
                      "Returns all member permissions configured on this account (owner only)");
    csilk_app_put_ext(app,
                      "/api/ledgers/:ledger_id/accounts/:id/permissions",
                      api_account_permissions_batch_set,
                      NULL,
                      NULL,
                      "Batch set account permissions",
                      "Sets permissions for multiple members on this account (owner only)");
    csilk_app_get_ext(
        app,
        "/api/ledgers/:ledger_id/members/:user_id/account-permissions",
        api_member_account_permissions_list,
        NULL,
        NULL,
        "List member account permissions",
        "Returns all account permissions for a target member in this ledger (owner only)");
    csilk_app_put_ext(
        app,
        "/api/ledgers/:ledger_id/members/:user_id/account-permissions",
        api_member_account_permissions_batch_set,
        NULL,
        NULL,
        "Batch set member account permissions",
        "Sets permissions across accounts for a target member in this ledger (owner only)");
    csilk_app_delete_ext(app,
                         "/api/ledgers/:ledger_id/accounts/:id/permissions/:user_id",
                         api_account_permissions_delete,
                         NULL,
                         NULL,
                         "Delete account permission override",
                         "Resets a member's permission on an account back to inherit (owner only)");
}

void
register_account_routes(csilk_app_t* app)
{
    register_account_http_routes(app);
}
