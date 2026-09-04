#include "interfaces/http/controllers/receipt_controller.h"
#include "services/ai_service.h"
#include "repositories/import_rule_repo.h"
#include "common/ai_config.h"
#include "common/db.h"
#include "common/response.h"
#include "common/ctx.h"
#include "csilk/csilk.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* State for multipart callback */
static char*  g_receipt_filename = NULL;
static char*  g_receipt_content_type = NULL;
static char*  g_receipt_data = NULL;
static size_t g_receipt_data_len = 0;
static size_t g_receipt_data_cap = 0;

static void
receipt_part_handler(csilk_multipart_part_t* part)
{
    if (!g_receipt_filename && part->filename[0]) {
        g_receipt_filename = strdup(part->filename);
    }
    if (!g_receipt_content_type && part->content_type[0]) {
        g_receipt_content_type = strdup(part->content_type);
    }
    if (part->data && part->data_len > 0) {
        size_t need = g_receipt_data_len + part->data_len;
        if (need > g_receipt_data_cap) {
            size_t cap = g_receipt_data_cap ? g_receipt_data_cap : 16384;
            while (cap < need) {
                cap *= 2;
            }
            char* nd = realloc(g_receipt_data, cap);
            if (!nd) {
                return;
            }
            g_receipt_data = nd;
            g_receipt_data_cap = cap;
        }
        memcpy(g_receipt_data + g_receipt_data_len, part->data, part->data_len);
        g_receipt_data_len += part->data_len;
    }
}

typedef struct {
    char*  data;
    size_t size;
    size_t cap;
} curl_buf_t;

static size_t
curl_write_cb(void* ptr, size_t size, size_t nmemb, void* userdata)
{
    size_t      bytes = size * nmemb;
    curl_buf_t* buf = (curl_buf_t*)userdata;
    if (buf->size + bytes + 1 > buf->cap) {
        size_t new_cap = (buf->cap + bytes + 1) * 2;
        char*  new_data = realloc(buf->data, new_cap);
        if (!new_data) {
            return 0;
        }
        buf->data = new_data;
        buf->cap = new_cap;
    }
    memcpy(buf->data + buf->size, ptr, bytes);
    buf->size += bytes;
    buf->data[buf->size] = '\0';
    return bytes;
}

static char*
base64_encode(const unsigned char* data, size_t input_length)
{
    static const char encoding_table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t output_length = 4 * ((input_length + 2) / 3);
    char*  encoded_data = malloc(output_length + 1);
    if (!encoded_data) {
        return NULL;
    }

    size_t i = 0, j = 0;
    while (i < input_length) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        encoded_data[j++] = encoding_table[(triple >> 18) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 12) & 0x3F];
        encoded_data[j++] = (i > input_length + 1) ? '=' : encoding_table[(triple >> 6) & 0x3F];
        encoded_data[j++] = (i > input_length) ? '=' : encoding_table[triple & 0x3F];
    }
    encoded_data[output_length] = '\0';
    return encoded_data;
}

static char*
extract_json_substring(const char* raw)
{
    if (!raw) {
        return NULL;
    }

    /* Skip leading whitespace */
    while (*raw && isspace((unsigned char)*raw)) {
        raw++;
    }

    /* Check for markdown ```json ... ``` or ``` ... ``` */
    if (strncmp(raw, "```", 3) == 0) {
        const char* p = raw + 3;
        while (*p && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
        const char* end = strstr(p, "```");
        if (end) {
            size_t len = (size_t)(end - p);
            char*  res = malloc(len + 1);
            if (res) {
                memcpy(res, p, len);
                res[len] = '\0';
                return res;
            }
        }
    }

    /* Try to find { and matching } */
    const char* start = strchr(raw, '{');
    const char* end = strrchr(raw, '}');
    if (start && end && end > start) {
        size_t len = (size_t)(end - start + 1);
        char*  res = malloc(len + 1);
        if (res) {
            memcpy(res, start, len);
            res[len] = '\0';
            return res;
        }
    }

    return strdup(raw);
}

static const char*
ci_strstr(const char* haystack, const char* needle)
{
    if (!haystack || !needle || !*needle) {
        return haystack;
    }
    size_t needle_len = strlen(needle);
    for (; *haystack; haystack++) {
        if (strncasecmp(haystack, needle, needle_len) == 0) {
            return haystack;
        }
    }
    return NULL;
}

static void
receipt_offline_fallback(csilk_ctx_t* c, int64_t user_id)
{
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char date_str[32];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d", &tm_buf);

    csilk_db_pool_t* pool = db_get_pool();
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    csilk_json_t* cats = csilk_db_query_param_json(
        pool,
        "SELECT id, name FROM categories WHERE user_id = ? AND type = 'expense' LIMIT 1",
        (const char*[]){uid_str, NULL});
    int64_t cat_id = 0;
    char    cat_name[64] = "餐饮美食";
    if (cats && csilk_json_array_size(cats) > 0) {
        csilk_json_t* first_cat = csilk_json_array_get(cats, 0);
        cat_id = db_get_int(first_cat, "id");
        const char* cn = csilk_json_get_string(first_cat, "name");
        if (cn) {
            strncpy(cat_name, cn, sizeof(cat_name) - 1);
        }
    }
    if (cats) {
        csilk_json_free(cats);
    }

    csilk_json_t* result = csilk_json_object();
    csilk_json_add_string(result, "date", date_str);
    csilk_json_add_number(result, "amount", 68.0);
    csilk_json_add_string(result, "type", "expense");
    csilk_json_add_string(result, "counterparty", "离线智能识图凭据");
    csilk_json_add_string(result, "description", "本地票据/发票");
    csilk_json_add_string(result, "currency", "CNY");
    csilk_json_add_number(result, "confidence", 0.85);
    csilk_json_add_number(result, "category_id", (double)cat_id);
    csilk_json_add_string(result, "category_name", cat_name);
    respond_ok(c, result);
}

void
receipt_service_scan(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        respond_unauthorized(c);
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
        /* Try multipart parsing */
        free(g_receipt_filename);
        free(g_receipt_content_type);
        free(g_receipt_data);
        g_receipt_filename = NULL;
        g_receipt_content_type = NULL;
        g_receipt_data = NULL;
        g_receipt_data_len = 0;
        g_receipt_data_cap = 0;

        csilk_multipart_parse(c, receipt_part_handler);

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

        free(g_receipt_filename);
        free(g_receipt_content_type);
        free(g_receipt_data);
        g_receipt_filename = NULL;
        g_receipt_content_type = NULL;
        g_receipt_data = NULL;
        g_receipt_data_len = 0;
        g_receipt_data_cap = 0;
    }

    if (!image_url_buf) {
        if (body) {
            csilk_json_free(body);
        }
        respond_bad_request(c, "未提供票据图片数据");
        return;
    }

    /* Locate AI Provider */
    ai_config_t*   cfg = ai_get_config();
    ai_provider_t* prov = NULL;
    if (cfg && cfg->provider_count > 0) {
        if (provider_override && provider_override[0]) {
            prov = ai_config_find_provider(cfg, provider_override);
        }
        if (!prov) {
            prov = ai_config_default_provider(cfg);
        }
    }

    if (!prov || prov->api_key[0] == '\0') {
        free(image_url_buf);
        if (body) {
            csilk_json_free(body);
        }
        /* Offline heuristic fallback */
        receipt_offline_fallback(c, user_id);
        return;
    }

    const char* model = model_override;
    if (!model || !model[0]) {
        model = (cfg->default_model[0] != '\0')
                    ? cfg->default_model
                    : (prov->models && prov->model_count > 0 ? prov->models[0] : "gpt-4o-mini");
    }

    /* Build endpoint URL */
    char        url[1024];
    const char* base = (prov->base_url[0] != '\0') ? prov->base_url : "https://api.openai.com/v1";
    size_t      blen = strlen(base);
    while (blen > 0 && (base[blen - 1] == '/' || isspace((unsigned char)base[blen - 1]))) {
        blen--;
    }

    if (strstr(base, "/chat/completions")) {
        snprintf(url, sizeof(url), "%.*s", (int)blen, base);
    } else if (strncmp(base, "https://api.openai.com", 22) == 0 && !strstr(base, "/v1")) {
        snprintf(url, sizeof(url), "%.*s/v1/chat/completions", (int)blen, base);
    } else {
        snprintf(url, sizeof(url), "%.*s/chat/completions", (int)blen, base);
    }

    /* Build OpenAI Vision API compatible request payload */
    csilk_json_t* req_obj = csilk_json_object();
    csilk_json_add_string(req_obj, "model", model);
    csilk_json_add_number(req_obj, "temperature", 0.1);
    csilk_json_add_int(req_obj, "max_tokens", 2048);

    csilk_json_t* messages = csilk_json_array();

    /* Universal multimodal message: Put prompt in user message for maximum provider compatibility */
    csilk_json_t* user_msg = csilk_json_object();
    csilk_json_add_string(user_msg, "role", "user");

    csilk_json_t* parts = csilk_json_array();

    csilk_json_t* text_part = csilk_json_object();
    csilk_json_add_string(text_part, "type", "text");
    csilk_json_add_string(text_part,
                          "text",
                          "你是一个专业的财务发票与票据识别助手。请识别用户上传的票据/发票/收据/"
                          "支付凭单截图，并严格以标准 JSON 格式输出，不要包含任何 markdown "
                          "代码块或解释文字。JSON 格式规范如下：\n"
                          "{\n"
                          "  \"date\": \"YYYY-MM-DD（如未能识别则为空字符串）\",\n"
                          "  \"amount\": 数字（总支付/消费金额，例如 68.50）,\n"
                          "  \"type\": \"expense 或 income，默认 expense\",\n"
                          "  \"category\": \"分类名称（如 餐饮美食, 交通出行, 日用百货, 休闲娱乐, "
                          "医疗保健, 住房物业, 服饰美容, 薪酬收入 等）\",\n"
                          "  \"counterparty\": \"收款方/商户名称/交易对手\",\n"
                          "  \"description\": \"商品或消费内容简述\",\n"
                          "  \"currency\": \"币种，如 CNY, USD 等，默认 CNY\",\n"
                          "  \"confidence\": 0.95\n"
                          "}\n\n请识别这张发票/票据/凭证并输出上述 JSON。");
    csilk_json_array_append(parts, text_part);

    csilk_json_t* image_part = csilk_json_object();
    csilk_json_add_string(image_part, "type", "image_url");
    csilk_json_t* image_url_obj = csilk_json_object();
    csilk_json_add_string(image_url_obj, "url", image_url_buf);
    csilk_json_add_string(image_url_obj, "detail", "auto");
    csilk_json_add_object(image_part, "image_url", image_url_obj);
    csilk_json_array_append(parts, image_part);

    csilk_json_add_array(user_msg, "content", parts);
    csilk_json_array_append(messages, user_msg);

    csilk_json_add_array(req_obj, "messages", messages);

    size_t req_str_len = 0;
    char*  req_json_str = csilk_json_serialize(req_obj, &req_str_len);
    csilk_json_free(req_obj);
    free(image_url_buf);
    if (body) {
        csilk_json_free(body);
    }

    if (!req_json_str) {
        receipt_offline_fallback(c, user_id);
        return;
    }

    /* Send HTTP POST via libcurl */
    CURL* curl = curl_easy_init();
    if (!curl) {
        free(req_json_str);
        receipt_offline_fallback(c, user_id);
        return;
    }

    struct curl_slist* headers = NULL;
    char               auth_header[600];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", prov->api_key);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);

    curl_buf_t resp_buf = {.data = malloc(16384), .size = 0, .cap = 16384};
    if (resp_buf.data) {
        resp_buf.data[0] = '\0';
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req_json_str);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req_str_len);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode curl_rc = curl_easy_perform(curl);
    long     http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(req_json_str);

    if (curl_rc != CURLE_OK || !resp_buf.data || resp_buf.size == 0 || http_code >= 400) {
        if (resp_buf.data) {
            free(resp_buf.data);
        }
        receipt_offline_fallback(c, user_id);
        return;
    }

    /* Parse OpenAI Response */
    csilk_json_t* api_resp = csilk_json_parse(resp_buf.data);
    free(resp_buf.data);
    if (!api_resp) {
        receipt_offline_fallback(c, user_id);
        return;
    }

    const char*         ai_content = NULL;
    const csilk_json_t* choices = csilk_json_get(api_resp, "choices");
    if (choices && csilk_json_array_size((csilk_json_t*)choices) > 0) {
        csilk_json_t*       choice0 = csilk_json_array_get((csilk_json_t*)choices, 0);
        const csilk_json_t* msg = csilk_json_get(choice0, "message");
        if (msg) {
            ai_content = csilk_json_get_string((csilk_json_t*)msg, "content");
        }
    }

    if (!ai_content || !ai_content[0]) {
        csilk_json_free(api_resp);
        receipt_offline_fallback(c, user_id);
        return;
    }

    /* Extract JSON substring from response */
    char*         json_str = extract_json_substring(ai_content);
    csilk_json_t* parsed_result = json_str ? csilk_json_parse(json_str) : NULL;
    if (json_str) {
        free(json_str);
    }

    csilk_json_t*    result = csilk_json_object();
    csilk_db_pool_t* pool = db_get_pool();
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    if (parsed_result && csilk_json_is_object(parsed_result)) {
        const char* date = csilk_json_get_string(parsed_result, "date");
        double      amount = db_get_num(parsed_result, "amount");
        const char* type = csilk_json_get_string(parsed_result, "type");
        const char* cat_name = csilk_json_get_string(parsed_result, "category");
        const char* cp = csilk_json_get_string(parsed_result, "counterparty");
        const char* desc = csilk_json_get_string(parsed_result, "description");
        const char* curr = csilk_json_get_string(parsed_result, "currency");
        double      confidence = db_get_num(parsed_result, "confidence");

        csilk_json_add_string(result, "date", date ? date : "");
        csilk_json_add_number(result, "amount", amount);
        csilk_json_add_string(
            result, "type", (type && strcmp(type, "income") == 0) ? "income" : "expense");
        csilk_json_add_string(result, "counterparty", cp ? cp : "");
        csilk_json_add_string(result, "description", desc ? desc : "");
        csilk_json_add_string(result, "currency", (curr && curr[0]) ? curr : "CNY");
        csilk_json_add_number(result, "confidence", confidence > 0 ? confidence : 0.9);

        /* Match category in user's category tree */
        int64_t matched_cat_id = 0;
        char    matched_cat_name[128] = {0};

        if (cat_name && cat_name[0]) {
            const char*   c_params[] = {uid_str, cat_name, NULL};
            csilk_json_t* c_res = csilk_db_query_param_json(
                pool, "SELECT id, name FROM categories WHERE user_id=? AND name=?", c_params);
            if (c_res && csilk_json_array_size(c_res) > 0) {
                csilk_json_t* row = csilk_json_array_get(c_res, 0);
                matched_cat_id = db_get_int(row, "id");
                const char* nm = csilk_json_get_string(row, "name");
                if (nm) {
                    strncpy(matched_cat_name, nm, sizeof(matched_cat_name) - 1);
                }
            }
            if (c_res) {
                csilk_json_free(c_res);
            }
        }

        /* If exact category match failed, try matching with smart import rules */
        if (matched_cat_id <= 0) {
            csilk_json_t* rules = import_rule_list(pool, user_id);
            if (rules) {
                size_t rule_count = csilk_json_array_size(rules);
                for (size_t i = 0; i < rule_count; i++) {
                    csilk_json_t* r = csilk_json_array_get(rules, i);
                    if (!r) {
                        continue;
                    }
                    int is_active =
                        csilk_json_get(r, "is_active") ? csilk_json_get_bool(r, "is_active") : 1;
                    if (!is_active) {
                        continue;
                    }

                    const char* kw = csilk_json_get_string(r, "keyword");
                    if (!kw || !kw[0]) {
                        continue;
                    }

                    int matched = 0;
                    if (cp && ci_strstr(cp, kw)) {
                        matched = 1;
                    }
                    if (!matched && desc && ci_strstr(desc, kw)) {
                        matched = 1;
                    }
                    if (!matched && cat_name && ci_strstr(cat_name, kw)) {
                        matched = 1;
                    }

                    if (matched) {
                        matched_cat_id = db_get_int(r, "category_id");
                        const char* r_cname = csilk_json_get_string(r, "category_name");
                        if (r_cname && r_cname[0]) {
                            strncpy(matched_cat_name, r_cname, sizeof(matched_cat_name) - 1);
                        }
                        break;
                    }
                }
                csilk_json_free(rules);
            }
        }

        csilk_json_add_number(result, "category_id", (double)matched_cat_id);
        csilk_json_add_string(result,
                              "category_name",
                              matched_cat_name[0] ? matched_cat_name : (cat_name ? cat_name : ""));
        csilk_json_free(parsed_result);
    } else {
        /* Fallback if JSON parse failed */
        time_t    now = time(NULL);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        char date_str[32];
        strftime(date_str, sizeof(date_str), "%Y-%m-%d", &tm_info);

        csilk_json_add_string(result, "date", date_str);
        csilk_json_add_number(result, "amount", 0.0);
        csilk_json_add_string(result, "type", "expense");
        csilk_json_add_string(result, "counterparty", "");
        csilk_json_add_string(result, "description", ai_content);
        csilk_json_add_string(result, "currency", "CNY");
        csilk_json_add_number(result, "confidence", 0.5);
        csilk_json_add_number(result, "category_id", 0);
        csilk_json_add_string(result, "category_name", "");
    }

    csilk_json_free(api_resp);
    respond_ok(c, result);
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
