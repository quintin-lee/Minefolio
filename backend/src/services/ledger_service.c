#include "services/ledger_service.h"
#include "repositories/ledger_repo.h"
#include "common/ctx.h"
#include "common/response.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void
ledger_service_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    list = ledger_list_by_user(pool, user_id);
    respond_ok(c, list ? list : csilk_json_array());
}

void
ledger_service_create(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "Invalid JSON body");
        return;
    }

    const char* name = csilk_json_get_string(body, "name");
    const char* desc = csilk_json_get_string(body, "description");
    const char* currency = csilk_json_get_string(body, "currency");
    const char* icon = csilk_json_get_string(body, "icon");
    const char* color = csilk_json_get_string(body, "color");

    if (!name || !name[0]) {
        respond_bad_request(c, "Ledger name is required");
        return;
    }

    int64_t new_id = ledger_create(pool, user_id, name, desc, currency, icon, color, false);
    if (new_id <= 0) {
        respond_error(c, 1002, "Failed to create ledger");
        return;
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "id", (double)new_id);
    respond_ok(c, res);
}

void
ledger_service_get(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char*      id_str = csilk_get_param(c, "id");
    int64_t          lid = id_str ? atoll(id_str) : 0;
    csilk_db_pool_t* pool = db_get_pool();

    char role[32] = {0};
    if (!ledger_get_user_role(pool, lid, user_id, role, sizeof(role))) {
        respond_error(c, 1004, "Forbidden: not a member of this ledger");
        return;
    }

    csilk_json_t* l_arr = ledger_get(pool, lid);
    if (!l_arr || csilk_json_array_size(l_arr) == 0) {
        if (l_arr) {
            csilk_json_free(l_arr);
        }
        respond_not_found(c);
        return;
    }
    respond_ok(c, l_arr);
}

void
ledger_service_update(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char*      id_str = csilk_get_param(c, "id");
    int64_t          lid = id_str ? atoll(id_str) : 0;
    csilk_db_pool_t* pool = db_get_pool();

    char role[32] = {0};
    if (!ledger_get_user_role(pool, lid, user_id, role, sizeof(role)) ||
        strcmp(role, "owner") != 0) {
        respond_error(c, 1004, "Forbidden: owner permission required");
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "Invalid JSON body");
        return;
    }

    const char* name = csilk_json_get_string(body, "name");
    const char* desc = csilk_json_get_string(body, "description");
    const char* currency = csilk_json_get_string(body, "currency");
    const char* icon = csilk_json_get_string(body, "icon");
    const char* color = csilk_json_get_string(body, "color");

    int ret = ledger_update(pool, lid, name, desc, currency, icon, color);
    if (ret != 0) {
        respond_error(c, 1002, "Failed to update ledger");
        return;
    }
    respond_ok(c, NULL);
}

void
ledger_service_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char*      id_str = csilk_get_param(c, "id");
    int64_t          lid = id_str ? atoll(id_str) : 0;
    csilk_db_pool_t* pool = db_get_pool();

    char role[32] = {0};
    if (!ledger_get_user_role(pool, lid, user_id, role, sizeof(role)) ||
        strcmp(role, "owner") != 0) {
        respond_error(c, 1004, "Forbidden: owner permission required");
        return;
    }

    int ret = ledger_delete(pool, lid);
    if (ret != 0) {
        respond_error(c, 1002, "Failed to delete ledger");
        return;
    }
    respond_ok(c, NULL);
}

void
ledger_service_list_members(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char*      id_str = csilk_get_param(c, "id");
    int64_t          lid = id_str ? atoll(id_str) : 0;
    csilk_db_pool_t* pool = db_get_pool();

    char role[32] = {0};
    if (!ledger_get_user_role(pool, lid, user_id, role, sizeof(role))) {
        respond_error(c, 1004, "Forbidden: not a member of this ledger");
        return;
    }

    csilk_json_t* members = ledger_member_list(pool, lid);
    respond_ok(c, members ? members : csilk_json_array());
}

void
ledger_service_add_member(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char*      id_str = csilk_get_param(c, "id");
    int64_t          lid = id_str ? atoll(id_str) : 0;
    csilk_db_pool_t* pool = db_get_pool();

    char role[32] = {0};
    if (!ledger_get_user_role(pool, lid, user_id, role, sizeof(role)) ||
        strcmp(role, "owner") != 0) {
        respond_error(c, 1004, "Forbidden: owner permission required");
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "Invalid JSON body");
        return;
    }

    const char* username = csilk_json_get_string(body, "username");
    const char* target_role = csilk_json_get_string(body, "role");
    if (!username || !username[0]) {
        respond_bad_request(c, "Username is required");
        return;
    }
    if (!target_role ||
        (strcmp(target_role, "editor") != 0 && strcmp(target_role, "viewer") != 0)) {
        target_role = "editor";
    }

    /* Find user by username */
    csilk_json_t* u_res = csilk_db_query_param_json(
        pool, "SELECT id FROM users WHERE username = ?", (const char*[]){username, NULL});
    if (!u_res || csilk_json_array_size(u_res) == 0) {
        if (u_res) {
            csilk_json_free(u_res);
        }
        respond_bad_request(c, "User not found");
        return;
    }

    int64_t target_uid = (int64_t)db_get_int(csilk_json_array_get(u_res, 0), "id");
    csilk_json_free(u_res);

    int ret = ledger_member_add(pool, lid, target_uid, target_role);
    if (ret != 0) {
        respond_error(c, 1002, "Failed to add member (user may already be in this ledger)");
        return;
    }
    respond_ok(c, NULL);
}

void
ledger_service_update_member(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char*      id_str = csilk_get_param(c, "id");
    const char*      uid_str = csilk_get_param(c, "user_id");
    int64_t          lid = id_str ? atoll(id_str) : 0;
    int64_t          target_uid = uid_str ? atoll(uid_str) : 0;
    csilk_db_pool_t* pool = db_get_pool();

    char role[32] = {0};
    if (!ledger_get_user_role(pool, lid, user_id, role, sizeof(role)) ||
        strcmp(role, "owner") != 0) {
        respond_error(c, 1004, "Forbidden: owner permission required");
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    const char*   new_role = body ? csilk_json_get_string(body, "role") : "editor";
    if (!new_role || (strcmp(new_role, "editor") != 0 && strcmp(new_role, "viewer") != 0)) {
        respond_bad_request(c, "Invalid role (must be editor or viewer)");
        return;
    }

    int ret = ledger_member_update_role(pool, lid, target_uid, new_role);
    if (ret != 0) {
        respond_error(c, 1002, "Failed to update member role");
        return;
    }
    respond_ok(c, NULL);
}

void
ledger_service_remove_member(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char*      id_str = csilk_get_param(c, "id");
    const char*      uid_str = csilk_get_param(c, "user_id");
    int64_t          lid = id_str ? atoll(id_str) : 0;
    int64_t          target_uid = uid_str ? atoll(uid_str) : 0;
    csilk_db_pool_t* pool = db_get_pool();

    char role[32] = {0};
    if (!ledger_get_user_role(pool, lid, user_id, role, sizeof(role))) {
        respond_error(c, 1004, "Forbidden: not a member of this ledger");
        return;
    }

    /* Only owner can remove others; members can only remove themselves (leave) */
    if (strcmp(role, "owner") != 0 && user_id != target_uid) {
        respond_error(c, 1004, "Forbidden: cannot remove other members");
        return;
    }

    /* Owner cannot leave directly */
    if (strcmp(role, "owner") == 0 && user_id == target_uid) {
        respond_bad_request(c, "Owner cannot leave ledger (delete ledger instead)");
        return;
    }

    int ret = ledger_member_remove(pool, lid, target_uid);
    if (ret != 0) {
        respond_error(c, 1002, "Failed to remove member");
        return;
    }
    respond_ok(c, NULL);
}

void
ledger_service_create_invite_code(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char*      id_str = csilk_get_param(c, "id");
    int64_t          lid = id_str ? atoll(id_str) : 0;
    csilk_db_pool_t* pool = db_get_pool();

    char role[32] = {0};
    if (!ledger_get_user_role(pool, lid, user_id, role, sizeof(role)) ||
        strcmp(role, "owner") != 0) {
        respond_error(c, 1004, "Forbidden: owner permission required");
        return;
    }

    /* Generate 6-digit random uppercase code */
    srand((unsigned int)(time(NULL) ^ user_id ^ lid));
    char       code[8];
    const char charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (int i = 0; i < 6; ++i) {
        code[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    code[6] = '\0';

    time_t    exp_t = time(NULL) + 7 * 86400; /* 7 days */
    struct tm exp_tm;
    gmtime_r(&exp_t, &exp_tm);
    char exp_str[32];
    strftime(exp_str, sizeof(exp_str), "%Y-%m-%d %H:%M:%S", &exp_tm);

    int ret = ledger_update_invite_code(pool, lid, code, exp_str);
    if (ret != 0) {
        respond_error(c, 1002, "Failed to generate invite code");
        return;
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "invite_code", code);
    csilk_json_add_string(res, "expires_at", exp_str);
    respond_ok(c, res);
}

void
ledger_service_join_by_invite(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "Invalid JSON body");
        return;
    }

    const char* invite_code = csilk_json_get_string(body, "invite_code");
    if (!invite_code || !invite_code[0]) {
        respond_bad_request(c, "Invite code is required");
        return;
    }

    csilk_json_t* l_arr = ledger_find_by_invite_code(pool, invite_code);
    if (!l_arr || csilk_json_array_size(l_arr) == 0) {
        if (l_arr) {
            csilk_json_free(l_arr);
        }
        respond_error(c, 1003, "Invalid or expired invite code");
        return;
    }

    csilk_json_t* l_obj = csilk_json_array_get(l_arr, 0);
    int64_t       lid = (int64_t)db_get_int(l_obj, "id");
    const char*   name = csilk_json_get_string(l_obj, "name");

    char role[32] = {0};
    if (ledger_get_user_role(pool, lid, user_id, role, sizeof(role))) {
        /* Already member */
        csilk_json_t* res = csilk_json_object();
        csilk_json_add_number(res, "id", (double)lid);
        csilk_json_add_string(res, "name", name ? name : "");
        csilk_json_free(l_arr);
        respond_ok(c, res);
        return;
    }

    int ret = ledger_member_add(pool, lid, user_id, "editor");
    if (ret != 0) {
        csilk_json_free(l_arr);
        respond_error(c, 1002, "Failed to join ledger");
        return;
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "id", (double)lid);
    csilk_json_add_string(res, "name", name ? name : "");
    csilk_json_free(l_arr);
    respond_ok(c, res);
}
