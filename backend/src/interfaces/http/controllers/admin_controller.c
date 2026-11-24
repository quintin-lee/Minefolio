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

void
system_backup_status(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        respond_unauthorized(c);
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
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        respond_unauthorized(c);
        return;
    }

    if (db_is_postgres()) {
        respond_error(c, 1005, "执行数据库在线热备份失败");
        return;
    }

    bool    deduplicated = false;
    int64_t task_id = backup_usecase_create_submit(db_get_pool(), user_id, &deduplicated);
    if (task_id <= 0) {
        respond_error(c, 1005, "执行数据库在线热备份失败");
        return;
    }

    mf_async_task_respond_submitted(c, task_id, "system_backup_create", deduplicated, NULL);
}

void
system_backup_download(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        respond_unauthorized(c);
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
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        respond_unauthorized(c);
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
}
