#include "interfaces/http/controllers/receipt_controller.h"
#include "application/import_export/receipt_usecases.h"
#include "application/task/async_task_manager.h"
#include "common/db.h"
#include "common/response.h"
#include "common/ctx.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Per-thread scratch state for the multipart callback. csilk serves each
 * request on a pool of worker threads that share one address space, so this
 * state must be thread-local: two concurrent receipt uploads must not clobber
 * each other's buffer. */
#define MF_MAX_RECEIPT_BYTES ((size_t)(10 * 1024 * 1024))

_Thread_local static char*  g_receipt_filename = NULL;
_Thread_local static char*  g_receipt_content_type = NULL;
_Thread_local static char*  g_receipt_data = NULL;
_Thread_local static size_t g_receipt_data_len = 0;
_Thread_local static size_t g_receipt_data_cap = 0;
_Thread_local static bool   g_receipt_overflow = false;

static void
receipt_part_handler(csilk_multipart_part_t* part)
{
    if (!part || g_receipt_overflow) {
        return;
    }
    if (!g_receipt_filename && part->filename[0]) {
        g_receipt_filename = strdup(part->filename);
    }
    if (!g_receipt_content_type && part->content_type[0]) {
        g_receipt_content_type = strdup(part->content_type);
    }
    if (part->data && part->data_len > 0) {
        if (part->data_len > MF_MAX_RECEIPT_BYTES ||
            g_receipt_data_len > MF_MAX_RECEIPT_BYTES - part->data_len) {
            g_receipt_overflow = true;
            return;
        }
        size_t need = g_receipt_data_len + part->data_len;
        if (need > g_receipt_data_cap) {
            size_t cap = g_receipt_data_cap ? g_receipt_data_cap : 16384;
            while (cap < need && cap < MF_MAX_RECEIPT_BYTES) {
                if (cap > MF_MAX_RECEIPT_BYTES / 2) {
                    cap = MF_MAX_RECEIPT_BYTES;
                    break;
                }
                cap *= 2;
            }
            if (cap < need) {
                cap = need;
            }
            char* nd = realloc(g_receipt_data, cap);
            if (!nd) {
                g_receipt_overflow = true;
                return;
            }
            g_receipt_data = nd;
            g_receipt_data_cap = cap;
        }
        memcpy(g_receipt_data + g_receipt_data_len, part->data, part->data_len);
        g_receipt_data_len += part->data_len;
    }
}

char*
receipt_base64_encode(const unsigned char* data, size_t input_length, size_t* output_length)
{
    static const char encoding_table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (output_length) {
        *output_length = 0;
    }
    if (!data && input_length > 0) {
        return NULL;
    }
    size_t groups = input_length / 3 + (input_length % 3 ? 1 : 0);
    if (groups > (SIZE_MAX - 1) / 4) {
        return NULL;
    }
    size_t out_len = groups * 4;
    char*  encoded_data = malloc(out_len + 1);
    if (!encoded_data) {
        return NULL;
    }

    size_t i = 0, j = 0;
    while (i < input_length) {
        size_t   rem = input_length - i;
        uint32_t octet_a = data[i++];
        uint32_t octet_b = (rem > 1) ? data[i++] : 0;
        uint32_t octet_c = (rem > 2) ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        encoded_data[j++] = encoding_table[(triple >> 18) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 12) & 0x3F];
        encoded_data[j++] = (rem <= 1) ? '=' : encoding_table[(triple >> 6) & 0x3F];
        encoded_data[j++] = (rem <= 2) ? '=' : encoding_table[triple & 0x3F];
    }
    encoded_data[out_len] = '\0';
    if (output_length) {
        *output_length = out_len;
    }
    return encoded_data;
}

static char*
base64_encode(const unsigned char* data, size_t input_length)
{
    return receipt_base64_encode(data, input_length, NULL);
}

static void
receipt_clear_tls_multipart(void)
{
    free(g_receipt_filename);
    free(g_receipt_content_type);
    free(g_receipt_data);
    g_receipt_filename = NULL;
    g_receipt_content_type = NULL;
    g_receipt_data = NULL;
    g_receipt_data_len = 0;
    g_receipt_data_cap = 0;
    g_receipt_overflow = false;
}

void
receipt_service_scan(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    char*         image_url_buf = NULL;
    const char*   model_override = NULL;
    const char*   provider_override = NULL;
    csilk_json_t* body = csilk_bind_json(c);

    if (body) {
        const char* img = csilk_json_get_string(body, "image");
        if (!img) {
            img = csilk_json_get_string(body, "image_base64");
        }
        if (!img) {
            img = csilk_json_get_string(body, "data");
        }
        model_override = csilk_json_get_string(body, "model");
        provider_override = csilk_json_get_string(body, "provider");

        if (img && img[0]) {
            if (strlen(img) > MF_MAX_RECEIPT_BYTES * 2) {
                csilk_json_free(body);
                respond_error(c, 413, "票据图片数据超过 10MB 上限");
                return;
            }
            if (strncmp(img, "data:image/", 11) == 0) {
                image_url_buf = strdup(img);
            } else {
                size_t sz = strlen(img) + 32;
                image_url_buf = malloc(sz);
                if (image_url_buf) {
                    snprintf(image_url_buf, sz, "data:image/jpeg;base64,%s", img);
                }
            }
        }
    }

    if (!image_url_buf) {
        receipt_clear_tls_multipart();
        csilk_multipart_parse(c, receipt_part_handler);

        if (g_receipt_overflow) {
            receipt_clear_tls_multipart();
            if (body) {
                csilk_json_free(body);
            }
            respond_error(c, 413, "上传票据图片超过 10MB 上限");
            return;
        }

        if (g_receipt_data && g_receipt_data_len > 0) {
            char* b64 = base64_encode((const unsigned char*)g_receipt_data, g_receipt_data_len);
            if (b64) {
                const char* mime = g_receipt_content_type ? g_receipt_content_type : "image/jpeg";
                size_t      sz = strlen(b64) + strlen(mime) + 32;
                image_url_buf = malloc(sz);
                if (image_url_buf) {
                    snprintf(image_url_buf, sz, "data:%s;base64,%s", mime, b64);
                }
                free(b64);
            }
        }
        receipt_clear_tls_multipart();
    }

    if (!image_url_buf) {
        if (body) {
            csilk_json_free(body);
        }
        respond_bad_request(c, "未提供票据图片数据");
        return;
    }

    bool    deduplicated = false;
    int64_t task_id = receipt_usecase_scan_submit(
        db_get_pool(), user_id, image_url_buf, model_override, provider_override, &deduplicated);
    free(image_url_buf);
    if (body) {
        csilk_json_free(body);
    }

    if (task_id <= 0) {
        respond_error(c, 500, "提交异步票据识别任务失败");
        return;
    }

    mf_async_task_respond_submitted(c, task_id, "receipt_scan", deduplicated, NULL);
}

void
api_receipt_scan_handler(csilk_ctx_t* c)
{
    receipt_service_scan(c);
}
void
receipt_scan_handler(csilk_ctx_t* c)
{
    receipt_service_scan(c);
}

void
register_receipt_routes(csilk_app_t* app)
{
    csilk_app_post_ext(app,
                       "/api/receipts/scan",
                       api_receipt_scan_handler,
                       NULL,
                       NULL,
                       "Scan receipt",
                       "Upload receipt image for AI OCR extraction");
}
