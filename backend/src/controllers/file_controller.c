#include "controllers/file_controller.h"
#include "services/file_parser.h"
#include "common/db.h"
#include "common/ctx.h"
#include "common/response.h"
#include "csilk/csilk.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void
file_upload_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        respond_unauthorized(c);
        return;
    }

    /* Parse multipart form data */
    char* filename = NULL;
    char* content_type = NULL;
    char* file_data = NULL;
    size_t file_data_len = 0;

    csilk_multipart_parse(c, ^(csilk_multipart_part_t* part) {
        if (part->is_file) {
            filename = strdup(part->filename ?: "upload");
            content_type = strdup(part->content_type ?: "application/octet-stream");
            file_data = malloc(part->data_len);
            if (file_data) {
                memcpy(file_data, part->data, part->data_len);
                file_data_len = part->data_len;
            }
        }
    });

    if (!filename || !file_data) {
        free(filename);
        free(content_type);
        respond_bad_request(c, "No file uploaded");
        return;
    }

    /* Parse the file */
    char parsed[50000];
    int rc = file_parse(db_get_pool(), file_data, file_data_len, filename, parsed, sizeof(parsed));

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "filename", filename);
    csilk_json_add_number(resp, "size", (double)file_data_len);
    
    if (rc == 0) {
        csilk_json_add_string(resp, "content", parsed);
        csilk_json_add_string(resp, "status", "parsed");
    } else {
        csilk_json_add_string(resp, "error", "failed to parse file");
        csilk_json_add_string(resp, "status", "error");
    }

    respond_ok(c, resp);

    free(filename);
    free(content_type);
    free(file_data);
}

void
register_file_routes(csilk_app_t* app)
{
    csilk_app_post_ext(app,
                       "/api/files/upload",
                       file_upload_handler,
                       NULL,
                       NULL,
                       "Upload and parse file",
                       "Accepts multipart file upload (PDF, TXT, CSV, ZIP) and returns parsed text");
}
