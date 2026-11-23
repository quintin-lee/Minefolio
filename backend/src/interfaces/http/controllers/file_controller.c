/**
 * @file file_controller.c
 * @brief 文件上传与多格式文档解析控制器实现文件 (DDD 接口层)
 */

#include "interfaces/http/controllers/file_controller.h"
#include "application/file/usecases.h"
#include "application/task/async_task_manager.h"
#include "domain/file/entity.h"
#include "infrastructure/repositories/file_repo_impl.h"
#include "common/db.h"
#include "common/ctx.h"
#include "common/response.h"
#include "csilk/csilk.h"
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Multipart 数据分片解析状态缓冲区
 *
 * 必须为线程局部存储：csilk 多 worker 并发处理上传请求时，若为进程级 static，
 * 两个并发上传会交替写同一缓冲区，导致跨用户文件内容串号与 realloc/free 竞态堆破坏。
 * 与 receipt_controller.c、ai_mcp_controller.c 的同类缓冲区保持 _Thread_local 一致。
 */
_Thread_local static char*  g_upload_filename = NULL;
_Thread_local static char*  g_upload_content_type = NULL;
_Thread_local static char*  g_upload_data = NULL;
_Thread_local static size_t g_upload_data_len = 0;
_Thread_local static size_t g_upload_data_cap = 0;

static void
upload_part_handler(csilk_multipart_part_t* part)
{
    if (!g_upload_filename && part->filename[0]) {
        g_upload_filename = strdup(part->filename);
    }
    if (!g_upload_content_type && part->content_type[0]) {
        g_upload_content_type = strdup(part->content_type);
    }
    if (part->data && part->data_len > 0) {
        size_t need = g_upload_data_len + part->data_len;
        if (need > g_upload_data_cap) {
            size_t cap = g_upload_data_cap ? g_upload_data_cap : 4096;
            while (cap < need) {
                cap *= 2;
            }
            char* nd = realloc(g_upload_data, cap);
            if (!nd) {
                return;
            }
            g_upload_data = nd;
            g_upload_data_cap = cap;
        }
        memcpy(g_upload_data + g_upload_data_len, part->data, part->data_len);
        g_upload_data_len += part->data_len;
    }
}

typedef struct {
    char*  filename;
    char*  data;
    size_t data_len;
} file_parse_task_arg_t;

static void
file_parse_task_cleanup(void* raw)
{
    file_parse_task_arg_t* arg = (file_parse_task_arg_t*)raw;
    if (!arg) {
        return;
    }
    free(arg->filename);
    free(arg->data);
    free(arg);
}

static csilk_json_t*
build_file_parse_response(const char*                   filename,
                          size_t                        data_len,
                          int                           parse_rc,
                          const mf_file_parse_result_t* result)
{
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "filename", filename);
    csilk_json_add_number(resp, "size", (double)data_len);

    if (parse_rc == 0) {
        csilk_json_add_string(resp, "content", result->content);
        csilk_json_add_string(resp, "status", "parsed");
    } else {
        csilk_json_add_string(
            resp, "error", result->error[0] ? result->error : "failed to parse file");
        csilk_json_add_string(resp, "status", "error");
    }
    return resp;
}

static int
file_parse_task_fn(void*   pool,
                   int64_t task_id,
                   int64_t user_id,
                   int64_t ledger_id,
                   void*   arg,
                   char**  out_result_json,
                   char*   err_buf,
                   size_t  err_cap)
{
    (void)user_id;
    (void)ledger_id;
    file_parse_task_arg_t* targ = (file_parse_task_arg_t*)arg;
    if (!targ || !targ->filename || !targ->data) {
        snprintf(err_buf, err_cap, "参数错误");
        return -1;
    }

    mf_async_task_update_progress(pool, task_id, 20, "正在后台提取并解析文档内容");

    mf_file_parse_result_t result = {0};
    int rc = file_usecase_parse(pool, targ->data, targ->data_len, targ->filename, &result);
    csilk_json_t* resp = build_file_parse_response(targ->filename, targ->data_len, rc, &result);
    if (out_result_json) {
        *out_result_json = csilk_json_serialize(resp, NULL);
    }
    csilk_json_free(resp);
    return 0;
}

static bool
file_should_parse_async(csilk_ctx_t* c, const char* filename, size_t data_len)
{
    const char* async_q = csilk_get_query(c, "async");
    if (async_q && (strcmp(async_q, "1") == 0 || strcasecmp(async_q, "true") == 0)) {
        return true;
    }
    if (data_len >= (size_t)(1024 * 1024)) {
        return true;
    }
    if (filename) {
        size_t flen = strlen(filename);
        if (flen >= 4) {
            const char* ext = filename + flen - 4;
            if (strcasecmp(ext, ".pdf") == 0 || strcasecmp(ext, ".zip") == 0) {
                return true;
            }
        }
    }
    return false;
}

void
file_upload_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        respond_unauthorized(c);
        return;
    }

    /* Reset globals */
    free(g_upload_filename);
    free(g_upload_content_type);
    free(g_upload_data);
    g_upload_filename = NULL;
    g_upload_content_type = NULL;
    g_upload_data = NULL;
    g_upload_data_len = 0;
    g_upload_data_cap = 0;

    /* Parse multipart form data */
    csilk_multipart_parse(c, upload_part_handler);

    if (!g_upload_filename || !g_upload_data) {
        free(g_upload_filename);
        free(g_upload_content_type);
        free(g_upload_data);
        g_upload_filename = NULL;
        g_upload_content_type = NULL;
        g_upload_data = NULL;
        g_upload_data_len = 0;
        g_upload_data_cap = 0;
        respond_bad_request(c, "No file uploaded");
        return;
    }

    if (file_should_parse_async(c, g_upload_filename, g_upload_data_len)) {
        file_parse_task_arg_t* targ = calloc(1, sizeof(*targ));
        if (!targ) {
            free(g_upload_filename);
            free(g_upload_content_type);
            free(g_upload_data);
            g_upload_filename = NULL;
            g_upload_content_type = NULL;
            g_upload_data = NULL;
            g_upload_data_len = 0;
            g_upload_data_cap = 0;
            respond_error(c, 500, "内存不足");
            return;
        }
        targ->filename = g_upload_filename;
        targ->data = g_upload_data;
        targ->data_len = g_upload_data_len;
        free(g_upload_content_type);
        g_upload_filename = NULL;
        g_upload_content_type = NULL;
        g_upload_data = NULL;
        g_upload_data_len = 0;
        g_upload_data_cap = 0;

        char   fname_copy[256];
        size_t fsize = targ->data_len;
        snprintf(fname_copy, sizeof(fname_copy), "%s", targ->filename);

        bool    deduplicated = false;
        int64_t task_id = mf_async_task_submit(db_get_pool(),
                                               user_id,
                                               0,
                                               "file_parse",
                                               NULL,
                                               "正在排队解析文档",
                                               file_parse_task_fn,
                                               targ,
                                               file_parse_task_cleanup,
                                               &deduplicated);
        if (task_id <= 0) {
            respond_error(c, 500, "提交异步文件解析任务失败");
            return;
        }

        csilk_json_t* extra = csilk_json_object();
        csilk_json_add_string(extra, "filename", fname_copy);
        csilk_json_add_number(extra, "size", (double)fsize);
        mf_async_task_respond_submitted(c, task_id, "file_parse", deduplicated, extra);
        return;
    }

    /* Small plain-text files keep synchronous fast parse */
    mf_file_parse_result_t result = {0};
    int                    rc = file_usecase_parse(
        db_get_pool(), g_upload_data, g_upload_data_len, g_upload_filename, &result);
    csilk_json_t* resp =
        build_file_parse_response(g_upload_filename, g_upload_data_len, rc, &result);
    respond_ok(c, resp);

    free(g_upload_filename);
    free(g_upload_content_type);
    free(g_upload_data);
    g_upload_filename = NULL;
    g_upload_content_type = NULL;
    g_upload_data = NULL;
    g_upload_data_len = 0;
    g_upload_data_cap = 0;
}

void
api_file_upload_handler(csilk_ctx_t* c)
{
    file_upload_handler(c);
}

void
register_file_routes(csilk_app_t* app)
{
    csilk_app_post_ext(
        app,
        "/api/files/upload",
        file_upload_handler,
        NULL,
        NULL,
        "Upload and parse file",
        "Accepts multipart file upload (PDF, TXT, CSV, ZIP) and returns parsed text");
}
