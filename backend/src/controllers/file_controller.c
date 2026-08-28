#include "controllers/file_controller.h"
#include "services/file_parser.h"
#include "common/db.h"
#include "common/ctx.h"
#include "common/response.h"
#include "csilk/csilk.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* State for multipart callback */
static char*  g_upload_filename = NULL;
static char*  g_upload_content_type = NULL;
static char*  g_upload_data = NULL;
static size_t g_upload_data_len = 0;

static void
upload_part_handler(csilk_multipart_part_t* part)
{
    if (!g_upload_filename && part->filename[0]) {
        g_upload_filename = strdup(part->filename);
    }
    if (!g_upload_content_type && part->content_type[0]) {
        g_upload_content_type = strdup(part->content_type);
    }
    if (part->data && part->data_len > 0 && !g_upload_data) {
        g_upload_data = malloc(part->data_len);
        if (g_upload_data) {
            memcpy(g_upload_data, part->data, part->data_len);
            g_upload_data_len = part->data_len;
        }
    }
}

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

    /* Parse multipart form data */
    csilk_multipart_parse(c, upload_part_handler);

    if (!g_upload_filename || !g_upload_data) {
        free(g_upload_filename);
        free(g_upload_content_type);
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
