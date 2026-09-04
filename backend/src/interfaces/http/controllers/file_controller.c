/**
 * @file file_controller.c
 * @brief 文件上传与多格式文档解析控制器实现文件
 *
 * 遵循三层 C 架构规范，本控制器负责处理基于 multipart/form-data 的文件流式上传，
 * 缓冲分片数据后调用底层 services/file_parser.h 解析器提取文本。
 */

#include "interfaces/http/controllers/file_controller.h"
#include "services/file_parser.h"
#include "common/db.h"
#include "common/ctx.h"
#include "common/response.h"
#include "csilk/csilk.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Multipart 数据分片解析状态全局缓冲区
 */
static char*  g_upload_filename = NULL;     /**< 上传文件名 */
static char*  g_upload_content_type = NULL; /**< MIME 内容类型 */
static char*  g_upload_data = NULL;         /**< 文件二进制内容缓冲 */
static size_t g_upload_data_len = 0;        /**< 当前已接收字节数 */
static size_t g_upload_data_cap = 0;        /**< 缓冲区总容量 */

/**
 * @brief Multipart 上传数据分片回调函数
 *
 * @param[in] part Csilk Multipart 分片指针
 */
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

/**
 * @brief 文件上传并解析 HTTP 处理函数
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void
file_upload_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
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

    /* Parse the file */
    char parsed[50000];
    int  rc = file_parse(
        db_get_pool(), g_upload_data, g_upload_data_len, g_upload_filename, parsed, sizeof(parsed));

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "filename", g_upload_filename);
    csilk_json_add_number(resp, "size", (double)g_upload_data_len);

    if (rc == 0) {
        csilk_json_add_string(resp, "content", parsed);
        csilk_json_add_string(resp, "status", "parsed");
    } else {
        csilk_json_add_string(resp, "error", "failed to parse file");
        csilk_json_add_string(resp, "status", "error");
    }

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

/**
 * @brief 注册文件上传与解析相关的 HTTP 路由
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
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

void
api_file_upload_handler(csilk_ctx_t* c)
{
    file_upload_handler(c);
}
