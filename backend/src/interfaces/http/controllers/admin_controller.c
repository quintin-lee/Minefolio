/**
 * @file admin_controller.c
 * @brief 系统管理与初始引导控制器实现 (DDD 接口层)
 */

#include "interfaces/http/controllers/admin_controller.h"
#include "application/auth/usecases.h"
#include "application/task/async_task_manager.h"
#include "application/task/backup_usecases.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <stdio.h>

#ifndef MINEFOLIO_VERSION
#define MINEFOLIO_VERSION "1.4.0"
#endif

void
system_status(csilk_ctx_t* c)
{
    int count = auth_usecase_user_count(db_get_pool());

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_bool(resp, "initialized", count > 0);
    csilk_json_add_string(resp, "version", MINEFOLIO_VERSION);
    /* user_count intentionally omitted: leaking the number of registered
     * users assists brute-force and social-engineering attacks. */
    respond_ok(c, resp);
}

void
system_setup(csilk_ctx_t* c)
{
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    const char*           username = csilk_json_get_string(body, "username");
    const char*           password_enc = csilk_json_get_string(body, "password_enc");
    csilk_json_t*         resp = NULL;
    auth_usecase_result_t res = {0};

    int rc = auth_usecase_setup_first_admin(db_get_pool(), c, username, password_enc, &resp, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0 && resp) {
        respond_ok(c, resp);
    } else if (res.code == 1004) {
        respond_forbidden(c, res.message);
    } else if (res.code == 1002) {
        respond_bad_request(c, res.message);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message[0] ? res.message : "初始化失败");
    }
}

static int
require_admin_actor(csilk_ctx_t* c, int64_t* out_user_id)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return 0;
    }
    if (!auth_usecase_is_instance_admin(db_get_pool(), user_id)) {
        respond_forbidden(c, "仅管理员可执行系统管理操作");
        return 0;
    }
    if (out_user_id) {
        *out_user_id = user_id;
    }
    return 1;
}

void
system_backup_status(csilk_ctx_t* c)
{
    int64_t actor_id = 0;
    if (!require_admin_actor(c, &actor_id)) {
        return;
    }

    csilk_json_t* status = NULL;
    if (backup_usecase_get_status(db_get_pool(), "data/backups", &status) != 0 || !status) {
        respond_error(c, 1005, "获取备份存储状态失败");
        return;
    }
    respond_ok(c, status);
}

void
system_backup_create(csilk_ctx_t* c)
{
    int64_t actor_id = 0;
    if (!require_admin_actor(c, &actor_id)) {
        return;
    }

    if (db_is_postgres()) {
        respond_error(c, 1005, "执行数据库在线热备份失败");
        return;
    }

    bool    deduplicated = false;
    int64_t task_id = backup_usecase_create_submit(db_get_pool(), actor_id, &deduplicated);
    if (task_id <= 0) {
        respond_error(c, 1005, "执行数据库在线热备份失败");
        return;
    }

    mf_async_task_respond_submitted(c, task_id, "system_backup_create", deduplicated, NULL);
}

void
system_backup_download(csilk_ctx_t* c)
{
    int64_t actor_id = 0;
    if (!require_admin_actor(c, &actor_id)) {
        return;
    }

    const char* filename = csilk_get_param(c, "filename");
    if (!filename || !backup_usecase_is_valid_filename(filename)) {
        respond_error(c, 1002, "非法文件名或检测到路径穿越");
        return;
    }

    char full_path[512];
    snprintf(full_path, sizeof(full_path), "data/backups/%s", filename);

    FILE* fp = fopen(full_path, "rb");
    if (!fp) {
        respond_error(c, 1002, "备份文件不存在");
        return;
    }

    char disp[256];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", filename);

    csilk_status(c, CSILK_STATUS_OK);
    csilk_set_header(c, "Content-Type", "application/x-sqlite3");
    csilk_set_header(c, "Content-Disposition", disp);

    uint8_t buf[16384];
    size_t  n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        csilk_response_write(c, buf, n);
    }
    fclose(fp);
    csilk_response_end(c);
}

void
system_backup_delete(csilk_ctx_t* c)
{
    int64_t actor_id = 0;
    if (!require_admin_actor(c, &actor_id)) {
        return;
    }

    const char* filename = csilk_get_param(c, "filename");
    if (!filename || !backup_usecase_is_valid_filename(filename)) {
        respond_error(c, 1002, "非法文件名或检测到路径穿越");
        return;
    }

    if (backup_usecase_delete("data/backups", filename) != 0) {
        respond_error(c, 1002, "备份文件不存在或删除失败");
        return;
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_bool(resp, "success", true);
    respond_ok(c, resp);
}

void
api_admin_users_list(csilk_ctx_t* c)
{
    int64_t actor_id = 0;
    if (!require_admin_actor(c, &actor_id)) {
        return;
    }

    int64_t page = 1;
    int64_t page_size = 20;
    parse_page_params(c, &page, &page_size);

    admin_list_users_cmd_t cmd = {
        .page = page,
        .page_size = page_size,
        .keyword = csilk_get_query(c, "keyword"),
        .role = csilk_get_query(c, "role"),
        .status = csilk_get_query(c, "status"),
    };

    csilk_json_t*         out_data = NULL;
    auth_usecase_result_t res = {0};
    int                   rc = auth_usecase_admin_list_users(db_get_pool(), &cmd, &out_data, &res);
    if (rc == 0 && res.code == 0 && out_data) {
        respond_ok(c, out_data);
    } else {
        respond_error(
            c, res.code ? res.code : 500, res.message[0] ? res.message : "查询用户列表失败");
    }
}

void
api_admin_users_create(csilk_ctx_t* c)
{
    int64_t actor_id = 0;
    if (!require_admin_actor(c, &actor_id)) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    admin_create_user_cmd_t cmd = {
        .username = csilk_json_get_string(body, "username"),
        .password_enc = csilk_json_get_string(body, "password_enc"),
        .plain_password = csilk_json_get_string(body, "password"),
        .role = csilk_json_get_string(body, "role"),
    };

    csilk_json_t*         out_data = NULL;
    auth_usecase_result_t res = {0};
    int rc = auth_usecase_admin_create_user(db_get_pool(), &cmd, c, &out_data, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0 && out_data) {
        respond_ok(c, out_data);
    } else {
        respond_error(c, res.code ? res.code : 1002, res.message[0] ? res.message : "创建用户失败");
    }
}

void
api_admin_users_update(csilk_ctx_t* c)
{
    int64_t actor_id = 0;
    if (!require_admin_actor(c, &actor_id)) {
        return;
    }

    int64_t target_id = parse_query_int_strict(csilk_get_param(c, "id"), -1);
    if (target_id <= 0) {
        respond_bad_request(c, "无效的用户 ID");
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    const csilk_json_t* disabled_node = csilk_json_get(body, "disabled");
    bool                has_disabled = (disabled_node != NULL);
    bool                disabled_val = false;
    if (has_disabled) {
        disabled_val = csilk_json_as_bool(disabled_node, false);
    }

    admin_update_user_cmd_t cmd = {
        .actor_user_id = actor_id,
        .target_user_id = target_id,
        .role = csilk_json_get_string(body, "role"),
        .has_disabled = has_disabled,
        .disabled = disabled_val,
    };

    csilk_json_t*         out_data = NULL;
    auth_usecase_result_t res = {0};
    int                   rc = auth_usecase_admin_update_user(db_get_pool(), &cmd, &out_data, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, out_data ? out_data : csilk_json_object());
    } else {
        if (out_data) {
            csilk_json_free(out_data);
        }
        respond_error(c, res.code ? res.code : 1002, res.message[0] ? res.message : "更新用户失败");
    }
}

void
api_admin_users_reset_password(csilk_ctx_t* c)
{
    int64_t actor_id = 0;
    if (!require_admin_actor(c, &actor_id)) {
        return;
    }

    int64_t target_id = parse_query_int_strict(csilk_get_param(c, "id"), -1);
    if (target_id <= 0) {
        respond_bad_request(c, "无效的用户 ID");
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    admin_reset_password_cmd_t cmd = {
        .target_user_id = target_id,
        .new_password_enc = csilk_json_get_string(body, "new_password_enc"),
        .plain_password = csilk_json_get_string(body, "new_password"),
    };

    auth_usecase_result_t res = {0};
    int                   rc = auth_usecase_admin_reset_password(db_get_pool(), &cmd, c, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else {
        respond_error(c, res.code ? res.code : 1002, res.message[0] ? res.message : "重置密码失败");
    }
}

void
api_admin_users_disable_2fa(csilk_ctx_t* c)
{
    int64_t actor_id = 0;
    if (!require_admin_actor(c, &actor_id)) {
        return;
    }

    int64_t target_id = parse_query_int_strict(csilk_get_param(c, "id"), -1);
    if (target_id <= 0) {
        respond_bad_request(c, "无效的用户 ID");
        return;
    }

    auth_usecase_result_t res = {0};
    int                   rc = auth_usecase_admin_disable_2fa(db_get_pool(), target_id, &res);
    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else {
        respond_error(
            c, res.code ? res.code : 1002, res.message[0] ? res.message : "关闭两步验证失败");
    }
}

void
api_admin_users_delete(csilk_ctx_t* c)
{
    int64_t actor_id = 0;
    if (!require_admin_actor(c, &actor_id)) {
        return;
    }

    int64_t target_id = parse_query_int_strict(csilk_get_param(c, "id"), -1);
    if (target_id <= 0) {
        respond_bad_request(c, "无效的用户 ID");
        return;
    }

    auth_usecase_result_t res = {0};
    int rc = auth_usecase_admin_delete_user(db_get_pool(), actor_id, target_id, &res);
    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else {
        respond_error(c, res.code ? res.code : 1004, res.message[0] ? res.message : "删除用户失败");
    }
}

void
register_admin_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/system/status",
                      system_status,
                      nullptr,
                      nullptr,
                      "System status",
                      "Returns initialization status and user count");
    csilk_app_post_ext(app,
                       "/api/system/setup",
                       system_setup,
                       "setup_req_t",
                       "token_resp_t",
                       "Initialize system",
                       "Seed the database with default categories for the first admin user");
    csilk_app_get(app, "/api/system/backup/status", system_backup_status);
    csilk_app_post(app, "/api/system/backup/create", system_backup_create);
    csilk_app_get(app, "/api/system/backup/download/:filename", system_backup_download);
    csilk_app_delete(app, "/api/system/backup/:filename", system_backup_delete);

    csilk_app_get(app, "/api/admin/users", api_admin_users_list);
    csilk_app_post(app, "/api/admin/users", api_admin_users_create);
    csilk_app_put(app, "/api/admin/users/:id", api_admin_users_update);
    csilk_app_post(app, "/api/admin/users/:id/reset-password", api_admin_users_reset_password);
    csilk_app_post(app, "/api/admin/users/:id/disable-2fa", api_admin_users_disable_2fa);
    csilk_app_delete(app, "/api/admin/users/:id", api_admin_users_delete);
}
