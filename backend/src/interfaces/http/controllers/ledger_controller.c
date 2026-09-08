/**
 * @file ledger_controller.c
 * @brief 账本 HTTP 控制器 (Interfaces Layer)
 *
 * 改造为调用 application/ledger/usecases，删除直接的 repository 调用。
 */

#include "interfaces/http/controllers/ledger_controller.h"
#include "application/ledger/usecases.h"
#include "application/ledger/commands.h"
#include "domain/ledger/entity.h"
#include "infrastructure/repositories/ledger_repo_impl.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===== Route handlers ===== */

void
ledger_service_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_json_t* list = ledger_usecase_list(db_get_pool(), user_id);
    respond_ok(c, list ? list : csilk_json_array());
}

void
ledger_service_create(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "Invalid JSON body");
        return;
    }

    create_ledger_cmd_t cmd = {
        .user_id = user_id,
        .name = csilk_json_get_string(body, "name"),
        .description = csilk_json_get_string(body, "description"),
        .currency = csilk_json_get_string(body, "currency"),
        .icon = csilk_json_get_string(body, "icon"),
        .color = csilk_json_get_string(body, "color"),
    };

    ledger_usecase_result_t res = {0};
    int64_t                 new_id = ledger_usecase_create(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (new_id > 0 && res.code == 0) {
        csilk_json_t* r = csilk_json_object();
        csilk_json_add_number(r, "id", (double)new_id);
        respond_ok(c, r);
    } else {
        respond_error(c,
                      res.code ? res.code : 1002,
                      res.message[0] ? res.message : "Failed to create ledger");
    }
}

void
ledger_service_get(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     lid = id_str ? atoll(id_str) : 0;

    ledger_usecase_result_t res = {0};
    csilk_json_t*           detail = ledger_usecase_get(db_get_pool(), user_id, lid, &res);

    if (detail) {
        respond_ok(c, detail);
    } else {
        respond_error(c, res.code ? res.code : 1003, res.message[0] ? res.message : "Not found");
    }
}

void
ledger_service_update(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     lid = id_str ? atoll(id_str) : 0;

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "Invalid JSON body");
        return;
    }

    update_ledger_cmd_t cmd = {
        .ledger_id = lid,
        .user_id = user_id,
        .name = csilk_json_get_string(body, "name"),
        .description = csilk_json_get_string(body, "description"),
        .currency = csilk_json_get_string(body, "currency"),
        .icon = csilk_json_get_string(body, "icon"),
        .color = csilk_json_get_string(body, "color"),
    };

    ledger_usecase_result_t res = {0};
    int                     rc = ledger_usecase_update(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, NULL);
    } else {
        respond_error(c,
                      res.code ? res.code : 1002,
                      res.message[0] ? res.message : "Failed to update ledger");
    }
}

void
ledger_service_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     lid = id_str ? atoll(id_str) : 0;

    ledger_usecase_result_t res = {0};
    int                     rc = ledger_usecase_delete(db_get_pool(), user_id, lid, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, NULL);
    } else {
        respond_error(c,
                      res.code ? res.code : 1002,
                      res.message[0] ? res.message : "Failed to delete ledger");
    }
}

void
ledger_service_list_members(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     lid = id_str ? atoll(id_str) : 0;

    ledger_usecase_result_t res = {0};
    csilk_json_t* members = ledger_usecase_list_members(db_get_pool(), user_id, lid, &res);

    if (members) {
        respond_ok(c, members);
    } else {
        respond_error(c, res.code ? res.code : 1004, res.message[0] ? res.message : "Forbidden");
    }
}

void
ledger_service_add_member(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     lid = id_str ? atoll(id_str) : 0;

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "Invalid JSON body");
        return;
    }

    add_member_cmd_t cmd = {
        .ledger_id = lid,
        .user_id = user_id,
        .username = csilk_json_get_string(body, "username"),
        .role = csilk_json_get_string(body, "role"),
    };

    ledger_usecase_result_t res = {0};
    int                     rc = ledger_usecase_add_member(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, NULL);
    } else {
        respond_error(
            c, res.code ? res.code : 1002, res.message[0] ? res.message : "Failed to add member");
    }
}

void
ledger_service_update_member(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    const char* uid_str = csilk_get_param(c, "user_id");
    int64_t     lid = id_str ? atoll(id_str) : 0;
    int64_t     target_uid = uid_str ? atoll(uid_str) : 0;

    csilk_json_t* body = csilk_bind_json(c);
    const char*   new_role = body ? csilk_json_get_string(body, "role") : "editor";

    update_member_cmd_t cmd = {
        .ledger_id = lid,
        .user_id = user_id,
        .target_user_id = target_uid,
        .new_role = new_role,
    };

    ledger_usecase_result_t res = {0};
    int                     rc = ledger_usecase_update_member(db_get_pool(), &cmd, &res);
    if (body) {
        csilk_json_free(body);
    }

    if (rc == 0 && res.code == 0) {
        respond_ok(c, NULL);
    } else {
        respond_error(c,
                      res.code ? res.code : 1002,
                      res.message[0] ? res.message : "Failed to update member role");
    }
}

void
ledger_service_remove_member(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    const char* uid_str = csilk_get_param(c, "user_id");
    int64_t     lid = id_str ? atoll(id_str) : 0;
    int64_t     target_uid = uid_str ? atoll(uid_str) : 0;

    remove_member_cmd_t cmd = {
        .ledger_id = lid,
        .target_user_id = target_uid,
        .caller_user_id = user_id,
    };

    ledger_usecase_result_t res = {0};
    int                     rc = ledger_usecase_remove_member(db_get_pool(), &cmd, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, NULL);
    } else {
        respond_error(c,
                      res.code ? res.code : 1002,
                      res.message[0] ? res.message : "Failed to remove member");
    }
}

void
ledger_service_create_invite_code(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     lid = id_str ? atoll(id_str) : 0;

    ledger_invite_result_t res = {0};
    int rc = ledger_usecase_create_invite_code(db_get_pool(), user_id, lid, &res);

    if (rc == 0 && res.code == 0) {
        csilk_json_t* r = csilk_json_object();
        csilk_json_add_string(r, "invite_code", res.invite_code);
        csilk_json_add_string(r, "expires_at", res.expires_at);
        respond_ok(c, r);
    } else {
        respond_error(c,
                      res.code ? res.code : 1002,
                      res.message[0] ? res.message : "Failed to generate invite code");
    }
}

void
ledger_service_join_by_invite(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "Invalid JSON body");
        return;
    }
    join_ledger_cmd_t cmd = {
        .user_id = user_id,
        .invite_code = csilk_json_get_string(body, "invite_code"),
    };

    ledger_usecase_result_t res = {0};
    int64_t                 joined_id = 0;
    int rc = ledger_usecase_join_by_invite(db_get_pool(), &cmd, &res, &joined_id);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        csilk_json_t* r = csilk_json_object();
        csilk_json_add_number(r, "id", (double)joined_id);
        respond_ok(c, r);
    } else {
        respond_error(
            c, res.code ? res.code : 1002, res.message[0] ? res.message : "Failed to join ledger");
    }
}

/* ===== Backward-compatibility aliases ===== */

void
api_ledger_list(csilk_ctx_t* c)
{
    ledger_service_list(c);
}
void
api_ledger_create(csilk_ctx_t* c)
{
    ledger_service_create(c);
}
void
api_ledger_get(csilk_ctx_t* c)
{
    ledger_service_get(c);
}
void
api_ledger_update(csilk_ctx_t* c)
{
    ledger_service_update(c);
}
void
api_ledger_delete(csilk_ctx_t* c)
{
    ledger_service_delete(c);
}
void
api_ledger_list_members(csilk_ctx_t* c)
{
    ledger_service_list_members(c);
}
void
api_ledger_add_member(csilk_ctx_t* c)
{
    ledger_service_add_member(c);
}
void
api_ledger_update_member(csilk_ctx_t* c)
{
    ledger_service_update_member(c);
}
void
api_ledger_remove_member(csilk_ctx_t* c)
{
    ledger_service_remove_member(c);
}
void
api_ledger_create_invite_code(csilk_ctx_t* c)
{
    ledger_service_create_invite_code(c);
}
void
api_ledger_join_by_invite(csilk_ctx_t* c)
{
    ledger_service_join_by_invite(c);
}

void
register_ledger_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/ledgers",
                      ledger_service_list,
                      NULL,
                      NULL,
                      "List ledgers",
                      "Get all ledgers current user owns or belongs to");
    csilk_app_post_ext(app,
                       "/api/ledgers",
                       ledger_service_create,
                       NULL,
                       NULL,
                       "Create ledger",
                       "Create a new ledger");
    csilk_app_get_ext(app,
                      "/api/ledgers/:id",
                      ledger_service_get,
                      NULL,
                      NULL,
                      "Get ledger",
                      "Get ledger details");
    csilk_app_put_ext(app,
                      "/api/ledgers/:id",
                      ledger_service_update,
                      NULL,
                      NULL,
                      "Update ledger",
                      "Update ledger metadata");
    csilk_app_delete_ext(app,
                         "/api/ledgers/:id",
                         ledger_service_delete,
                         NULL,
                         NULL,
                         "Delete ledger",
                         "Delete ledger and associated items");

    csilk_app_get_ext(app,
                      "/api/ledgers/:id/members",
                      ledger_service_list_members,
                      NULL,
                      NULL,
                      "List members",
                      "Get ledger members and roles");
    csilk_app_post_ext(app,
                       "/api/ledgers/:id/members",
                       ledger_service_add_member,
                       NULL,
                       NULL,
                       "Add member",
                       "Add member by username");
    csilk_app_put_ext(app,
                      "/api/ledgers/:id/members/:user_id",
                      ledger_service_update_member,
                      NULL,
                      NULL,
                      "Update member role",
                      "Update member role");
    csilk_app_delete_ext(app,
                         "/api/ledgers/:id/members/:user_id",
                         ledger_service_remove_member,
                         NULL,
                         NULL,
                         "Remove member",
                         "Remove member or leave ledger");

    csilk_app_post_ext(app,
                       "/api/ledgers/:id/invite-code",
                       ledger_service_create_invite_code,
                       NULL,
                       NULL,
                       "Generate invite code",
                       "Generate or refresh 6-digit invite code");
    csilk_app_post_ext(app,
                       "/api/ledgers/join",
                       ledger_service_join_by_invite,
                       NULL,
                       NULL,
                       "Join by invite code",
                       "Join a ledger using invite code");
}
