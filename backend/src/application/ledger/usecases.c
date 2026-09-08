/**
 * @file usecases.c
 * @brief 账本用例编排实现 (Application Ledger Use Cases)
 */

#include "application/ledger/usecases.h"
#include "domain/ledger/rules.h"
#include "domain/ledger/repository.h"
#include "infrastructure/repositories/ledger_repo_impl.h"
#include "common/db.h"
#include "common/response.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ===== Internal helpers ===== */

static void
ensure_default(void* pool, int64_t user_id)
{
    mf_ledger_repo_get_or_create_default(pool, user_id);
}

static bool
check_member_role(void* pool, int64_t ledger_id, int64_t user_id, char* role, size_t len)
{
    return mf_ledger_repo_get_user_role(pool, ledger_id, user_id, role, len) != NULL;
}

/**
 * @brief 将账本列表项转换为 JSON 对象
 */
static csilk_json_t*
ledger_list_item_to_json(const mf_ledger_list_item_t* item)
{
    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_number(obj, "id", (double)item->id);
    csilk_json_add_number(obj, "owner_id", (double)item->owner_id);
    csilk_json_add_string(obj, "name", item->name);
    csilk_json_add_string(obj, "description", item->description);
    csilk_json_add_string(obj, "currency", item->currency);
    csilk_json_add_string(obj, "icon", item->icon);
    csilk_json_add_string(obj, "color", item->color);
    csilk_json_add_bool(obj, "is_default", item->is_default);
    csilk_json_add_string(obj, "invite_code", item->invite_code);
    csilk_json_add_string(obj, "invite_expires_at", item->invite_expires_at);
    csilk_json_add_string(obj, "created_at", item->created_at);
    csilk_json_add_string(obj, "updated_at", item->updated_at);
    csilk_json_add_string(obj, "my_role", item->my_role);
    csilk_json_add_string(obj, "owner_username", item->owner_username);
    csilk_json_add_number(obj, "member_count", (double)item->member_count);
    csilk_json_add_number(obj, "total_assets", item->total_assets);
    return obj;
}

/**
 * @brief 将账本详情转换为 JSON 对象
 */
static csilk_json_t*
ledger_to_json(const mf_ledger_t* ledger)
{
    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_number(obj, "id", (double)ledger->id);
    csilk_json_add_number(obj, "owner_id", (double)ledger->owner_id);
    csilk_json_add_string(obj, "name", ledger->name);
    csilk_json_add_string(obj, "description", ledger->description);
    csilk_json_add_string(obj, "currency", ledger->currency);
    csilk_json_add_string(obj, "icon", ledger->icon);
    csilk_json_add_string(obj, "color", ledger->color);
    csilk_json_add_bool(obj, "is_default", ledger->is_default);
    csilk_json_add_string(obj, "invite_code", ledger->invite_code);
    csilk_json_add_string(obj, "invite_expires_at", ledger->invite_expires_at);
    csilk_json_add_string(obj, "created_at", ledger->created_at);
    csilk_json_add_string(obj, "updated_at", ledger->updated_at);
    return obj;
}

/**
 * @brief 将成员列表转换为 JSON 数组
 */
static csilk_json_t*
member_list_to_json(const mf_ledger_member_t* list, size_t count)
{
    csilk_json_t* arr = csilk_json_array();
    for (size_t i = 0; i < count; i++) {
        csilk_json_t* obj = csilk_json_object();
        csilk_json_add_number(obj, "id", (double)list[i].id);
        csilk_json_add_number(obj, "ledger_id", (double)list[i].ledger_id);
        csilk_json_add_number(obj, "user_id", (double)list[i].user_id);
        csilk_json_add_string(obj, "username", list[i].username);
        csilk_json_add_string(obj, "role", list[i].role);
        csilk_json_add_string(obj, "joined_at", list[i].joined_at);
        csilk_json_add_item(arr, obj);
    }
    return arr;
}

/* ===== Use case implementations ===== */

csilk_json_t*
ledger_usecase_list(void* pool, int64_t user_id)
{
    ensure_default(pool, user_id);

    mf_ledger_list_item_t* list = NULL;
    size_t                 count = 0;
    if (mf_ledger_repo_list_by_user(pool, user_id, &list, &count) != 0) {
        return csilk_json_array();
    }

    csilk_json_t* arr = csilk_json_array();
    for (size_t i = 0; i < count; i++) {
        csilk_json_add_item(arr, ledger_list_item_to_json(&list[i]));
    }
    mf_ledger_repo_free_list(list);
    return arr;
}

csilk_json_t*
ledger_usecase_get(void* pool, int64_t user_id, int64_t ledger_id, ledger_usecase_result_t* out_res)
{
    char role[32] = {0};
    if (!check_member_role(pool, ledger_id, user_id, role, sizeof(role))) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "Forbidden: not a member");
        return NULL;
    }

    mf_ledger_t ledger = {0};
    int         rc = mf_ledger_repo_get(pool, ledger_id, &ledger);
    if (rc == 1) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "Ledger not found");
        return NULL;
    }
    if (rc != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "Database error");
        return NULL;
    }
    return ledger_to_json(&ledger);
}

int64_t
ledger_usecase_create(void* pool, const create_ledger_cmd_t* cmd, ledger_usecase_result_t* out_res)
{
    if (!mf_ledger_rule_validate_name(cmd->name)) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "Ledger name is required");
        return 0;
    }

    mf_ledger_t ledger = {0};
    ledger.owner_id = cmd->user_id;
    strncpy(ledger.name, cmd->name, sizeof(ledger.name) - 1);
    if (cmd->description) {
        strncpy(ledger.description, cmd->description, sizeof(ledger.description) - 1);
    }
    strncpy(ledger.currency,
            cmd->currency && cmd->currency[0] ? cmd->currency : "CNY",
            sizeof(ledger.currency) - 1);
    strncpy(
        ledger.icon, cmd->icon && cmd->icon[0] ? cmd->icon : "ph:wallet", sizeof(ledger.icon) - 1);
    strncpy(ledger.color,
            cmd->color && cmd->color[0] ? cmd->color : "#3b82f6",
            sizeof(ledger.color) - 1);
    ledger.is_default = false;

    int64_t id = mf_ledger_repo_create(pool, cmd->user_id, &ledger);
    if (id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "Failed to create ledger");
    }
    return id;
}

int
ledger_usecase_update(void* pool, const update_ledger_cmd_t* cmd, ledger_usecase_result_t* out_res)
{
    char role[32] = {0};
    if (!check_member_role(pool, cmd->ledger_id, cmd->user_id, role, sizeof(role)) ||
        !mf_ledger_rule_can_manage(role)) {
        out_res->code = 1004;
        snprintf(
            out_res->message, sizeof(out_res->message), "Forbidden: owner permission required");
        return -1;
    }

    mf_ledger_t ledger = {0};
    if (cmd->name) {
        strncpy(ledger.name, cmd->name, sizeof(ledger.name) - 1);
    }
    if (cmd->description) {
        strncpy(ledger.description, cmd->description, sizeof(ledger.description) - 1);
    }
    if (cmd->currency) {
        strncpy(ledger.currency, cmd->currency, sizeof(ledger.currency) - 1);
    }
    if (cmd->icon) {
        strncpy(ledger.icon, cmd->icon, sizeof(ledger.icon) - 1);
    }
    if (cmd->color) {
        strncpy(ledger.color, cmd->color, sizeof(ledger.color) - 1);
    }

    int rc = mf_ledger_repo_update(pool, cmd->ledger_id, &ledger);
    if (rc != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "Failed to update ledger");
    }
    return rc;
}

int
ledger_usecase_delete(void*                    pool,
                      int64_t                  user_id,
                      int64_t                  ledger_id,
                      ledger_usecase_result_t* out_res)
{
    char role[32] = {0};
    if (!check_member_role(pool, ledger_id, user_id, role, sizeof(role)) ||
        !mf_ledger_rule_can_manage(role)) {
        out_res->code = 1004;
        snprintf(
            out_res->message, sizeof(out_res->message), "Forbidden: owner permission required");
        return -1;
    }

    int rc = mf_ledger_repo_delete(pool, ledger_id);
    if (rc != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "Failed to delete ledger");
    }
    return rc;
}

csilk_json_t*
ledger_usecase_list_members(void*                    pool,
                            int64_t                  user_id,
                            int64_t                  ledger_id,
                            ledger_usecase_result_t* out_res)
{
    char role[32] = {0};
    if (!check_member_role(pool, ledger_id, user_id, role, sizeof(role))) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "Forbidden: not a member");
        return NULL;
    }

    mf_ledger_member_t* list = NULL;
    size_t              count = 0;
    if (mf_ledger_repo_member_list(pool, ledger_id, &list, &count) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "Database error");
        return NULL;
    }

    csilk_json_t* arr = member_list_to_json(list, count);
    mf_ledger_repo_free_list(list);
    return arr;
}

int
ledger_usecase_add_member(void* pool, const add_member_cmd_t* cmd, ledger_usecase_result_t* out_res)
{
    char role[32] = {0};
    if (!check_member_role(pool, cmd->ledger_id, cmd->user_id, role, sizeof(role)) ||
        !mf_ledger_rule_can_manage(role)) {
        out_res->code = 1004;
        snprintf(
            out_res->message, sizeof(out_res->message), "Forbidden: owner permission required");
        return -1;
    }

    if (!cmd->username || !cmd->username[0]) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "Username is required");
        return -1;
    }

    const char* target_role = cmd->role && cmd->role[0] ? cmd->role : "editor";
    if (!mf_ledger_rule_validate_role(target_role)) {
        target_role = "editor";
    }

    int64_t target_uid = mf_ledger_repo_find_user_by_username(pool, cmd->username);
    if (target_uid <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "User not found");
        return -1;
    }

    int rc = mf_ledger_repo_member_add(pool, cmd->ledger_id, target_uid, target_role);
    if (rc != 0) {
        out_res->code = 1002;
        snprintf(out_res->message,
                 sizeof(out_res->message),
                 "Failed to add member (user may already be in this ledger)");
    }
    return rc;
}

int
ledger_usecase_update_member(void*                      pool,
                             const update_member_cmd_t* cmd,
                             ledger_usecase_result_t*   out_res)
{
    char role[32] = {0};
    if (!check_member_role(pool, cmd->ledger_id, cmd->user_id, role, sizeof(role)) ||
        !mf_ledger_rule_can_manage(role)) {
        out_res->code = 1004;
        snprintf(
            out_res->message, sizeof(out_res->message), "Forbidden: owner permission required");
        return -1;
    }

    if (!cmd->new_role || !mf_ledger_rule_validate_role(cmd->new_role)) {
        out_res->code = 1002;
        snprintf(
            out_res->message, sizeof(out_res->message), "Invalid role (must be editor or viewer)");
        return -1;
    }

    int rc =
        mf_ledger_repo_member_update_role(pool, cmd->ledger_id, cmd->target_user_id, cmd->new_role);
    if (rc != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "Failed to update member role");
    }
    return rc;
}

int
ledger_usecase_remove_member(void*                      pool,
                             const remove_member_cmd_t* cmd,
                             ledger_usecase_result_t*   out_res)
{
    char role[32] = {0};
    if (!check_member_role(pool, cmd->ledger_id, cmd->caller_user_id, role, sizeof(role))) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "Forbidden: not a member");
        return -1;
    }

    bool is_owner = mf_ledger_rule_can_manage(role);
    bool is_self = (cmd->caller_user_id == cmd->target_user_id);

    if (!mf_ledger_rule_can_remove_member(is_owner, is_self)) {
        if (is_owner && is_self) {
            out_res->code = 1002;
            snprintf(out_res->message,
                     sizeof(out_res->message),
                     "Owner cannot leave ledger (delete ledger instead)");
        } else {
            out_res->code = 1004;
            snprintf(out_res->message,
                     sizeof(out_res->message),
                     "Forbidden: cannot remove other members");
        }
        return -1;
    }

    int rc = mf_ledger_repo_member_remove(pool, cmd->ledger_id, cmd->target_user_id);
    if (rc != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "Failed to remove member");
    }
    return rc;
}

int
ledger_usecase_create_invite_code(void*                   pool,
                                  int64_t                 user_id,
                                  int64_t                 ledger_id,
                                  ledger_invite_result_t* out_res)
{
    char role[32] = {0};
    if (!check_member_role(pool, ledger_id, user_id, role, sizeof(role)) ||
        !mf_ledger_rule_can_manage(role)) {
        out_res->code = 1004;
        snprintf(
            out_res->message, sizeof(out_res->message), "Forbidden: owner permission required");
        return -1;
    }

    /* Generate 6-digit random uppercase code */
    srand((unsigned int)(time(NULL) ^ user_id ^ ledger_id));
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

    int rc = mf_ledger_repo_update_invite_code(pool, ledger_id, code, exp_str);
    if (rc != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "Failed to generate invite code");
        return -1;
    }

    strncpy(out_res->invite_code, code, sizeof(out_res->invite_code) - 1);
    strncpy(out_res->expires_at, exp_str, sizeof(out_res->expires_at) - 1);
    return 0;
}
int
ledger_usecase_join_by_invite(void*                    pool,
                              const join_ledger_cmd_t* cmd,
                              ledger_usecase_result_t* out_res,
                              int64_t*                 out_ledger_id)
{
    if (!cmd->invite_code || !cmd->invite_code[0]) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "Invite code is required");
        return -1;
    }

    mf_ledger_t ledger = {0};
    int         rc = mf_ledger_repo_find_by_invite_code(pool, cmd->invite_code, &ledger);
    if (rc == 1) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "Invalid or expired invite code");
        return -1;
    }
    if (rc != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "Database error");
        return -1;
    }

    /* Check if already a member */
    char role[32] = {0};
    if (check_member_role(pool, ledger.id, cmd->user_id, role, sizeof(role))) {
        out_res->code = 0;
        snprintf(out_res->message, sizeof(out_res->message), "Already a member");
        if (out_ledger_id) {
            *out_ledger_id = ledger.id;
        }
        return 0;
    }

    rc = mf_ledger_repo_member_add(pool, ledger.id, cmd->user_id, "editor");
    if (rc != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "Failed to join ledger");
    } else if (out_ledger_id) {
        *out_ledger_id = ledger.id;
    }
    return rc;
}
