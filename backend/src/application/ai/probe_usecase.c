#include "application/ai/probe_usecase.h"
#include "application/ai/config_provider.h"
#include "config/key_manager.h"
#include "csilk/drivers/ai.h"
#include <curl/curl.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char*
get_driver_name(const char* prov_id)
{
    if (prov_id && strcmp(prov_id, "ollama") == 0) {
        return "ollama";
    }
    return "openai";
}

static void
format_friendly_error(const char* raw_err, char* out_buf, size_t out_cap)
{
    if (!raw_err || !raw_err[0]) {
        snprintf(out_buf, out_cap, "AI 请求失败");
        return;
    }
    if (strstr(raw_err, "401") || strstr(raw_err, "Unauthorized")) {
        snprintf(out_buf, out_cap, "API Key 无效或已过期，请在设置中检查对应 Provider 的密钥配置");
    } else if (strstr(raw_err, "403") || strstr(raw_err, "Forbidden")) {
        snprintf(out_buf, out_cap, "权限不足，请确认该 API Key 是否具备调用当前模型的权限");
    } else if (strstr(raw_err, "404") || strstr(raw_err, "Not Found")) {
        snprintf(out_buf, out_cap, "模型或端点不存在，请检查模型名称和 Base URL 配置");
    } else if (strstr(raw_err, "429") || strstr(raw_err, "Too Many Requests") ||
               strstr(raw_err, "rate limit") || strstr(raw_err, "quota")) {
        snprintf(out_buf, out_cap, "超出调用频率限制或账户配额不足，请稍后重试或检查账户余额");
    } else if (strstr(raw_err, "500") || strstr(raw_err, "Internal Server Error")) {
        snprintf(out_buf, out_cap, "AI 服务商服务端发生错误，请稍后重试");
    } else if (strstr(raw_err, "502") || strstr(raw_err, "503") || strstr(raw_err, "504") ||
               strstr(raw_err, "Bad Gateway") || strstr(raw_err, "Service Unavailable")) {
        snprintf(out_buf, out_cap, "AI 服务暂不可用或网关超时，请检查网络连接或稍后重试");
    } else if (strstr(raw_err, "timeout") || strstr(raw_err, "timed out") ||
               strstr(raw_err, "Timeout")) {
        snprintf(out_buf, out_cap, "请求超时，请检查网络连接或更换更快的推理服务");
    } else if (strstr(raw_err, "connection refused") || strstr(raw_err, "connect failed") ||
               strstr(raw_err, "Couldn't connect")) {
        snprintf(
            out_buf, out_cap, "无法连接到 AI 服务，请检查 Base URL 是否正确、服务是否正常启动");
    } else {
        snprintf(out_buf, out_cap, "%s", raw_err);
    }
}

int
ai_usecase_test_connection(const csilk_json_t*  body,
                           csilk_json_t**       out_resp,
                           ai_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!body || !out_resp) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "请求参数无效");
        return -1;
    }

    ai_config_t* cfg = ai_config_acquire();

    const char* id =
        csilk_json_get_string(body, "id") ? csilk_json_get_string(body, "id") : "openai";
    const char* base_url = csilk_json_get_string(body, "base_url");
    const char* k_enc = csilk_json_get_string(body, "api_key_enc");
    const char* k = csilk_json_get_string(body, "api_key");
    const char* model = csilk_json_get_string(body, "model");

    char        key_buf[512] = {0};
    const char* raw_k = (k_enc && k_enc[0]) ? k_enc : k;
    if (raw_k && raw_k[0]) {
        size_t dec_len = sizeof(key_buf);
        if (auth_key_decrypt(raw_k, key_buf, &dec_len) != 0) {
            size_t _k_len = strlen(raw_k);
            size_t _copy_len = _k_len < sizeof(key_buf) - 1 ? _k_len : sizeof(key_buf) - 1;
            memcpy(key_buf, raw_k, _copy_len);
            key_buf[_copy_len] = '\0';
        }
    } else {
        ai_provider_t* old_p = ai_config_find_provider(cfg, id);
        if (old_p && old_p->api_key[0]) {
            size_t _src_len = strlen(old_p->api_key);
            size_t _copy_len = _src_len < sizeof(key_buf) - 1 ? _src_len : sizeof(key_buf) - 1;
            memcpy(key_buf, old_p->api_key, _copy_len);
            key_buf[_copy_len] = '\0';
        } else {
            strcpy(key_buf, "dummy");
        }
    }

    const char* dname = get_driver_name(id);
    csilk_ai_t* test_ai = csilk_ai_new(dname, key_buf, (base_url && base_url[0]) ? base_url : NULL);
    if (!test_ai) {
        csilk_json_t* resp = csilk_json_object();
        csilk_json_add_bool(resp, "success", false);
        csilk_json_add_number(resp, "latency_ms", 0);
        csilk_json_add_string(resp, "message", "创建 AI 驱动失败");
        ai_config_release(cfg);
        *out_resp = resp;
        out_res->code = 0;
        snprintf(out_res->message, sizeof(out_res->message), "ok");
        return 0;
    }

    struct timespec t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t1);

    const char* test_model = model;
    if (!test_model || !test_model[0]) {
        ai_provider_t* p = ai_config_find_provider(cfg, id);
        if (p && p->model_count > 0 && p->models[0] && p->models[0][0]) {
            test_model = p->models[0];
        } else if (strcmp(id, "deepseek") == 0) {
            test_model = "deepseek-chat";
        } else if (strcmp(id, "openai") == 0) {
            test_model = "gpt-4o-mini";
        } else if (strcmp(id, "qwen") == 0) {
            test_model = "qwen-plus";
        } else if (strcmp(id, "ollama") == 0) {
            test_model = "llama3";
        } else {
            test_model = "default";
        }
    }

    csilk_ai_message_t      msg = {.role = "user", .content = "ping"};
    csilk_ai_chat_request_t req = {
        .model = test_model,
        .messages = &msg,
        .message_count = 1,
        .stream = 0,
        .timeout_ms = 10000,
    };
    csilk_ai_chat_response_t res = {0};
    int                      rc = csilk_ai_chat(test_ai, &req, &res);
    clock_gettime(CLOCK_MONOTONIC, &t2);

    long latency_ms = (t2.tv_sec - t1.tv_sec) * 1000 + (t2.tv_nsec - t1.tv_nsec) / 1000000;
    if (latency_ms < 0) {
        latency_ms = 0;
    }

    csilk_json_t* resp = csilk_json_object();
    if (rc == 0) {
        csilk_json_add_bool(resp, "success", true);
        csilk_json_add_number(resp, "latency_ms", (double)latency_ms);
        csilk_json_add_string(resp, "message", "连接成功");
    } else {
        const char* raw_err =
            (res.error_message && res.error_message[0]) ? res.error_message : "连接超时或失败";
        char friendly_err[512];
        format_friendly_error(raw_err, friendly_err, sizeof(friendly_err));
        csilk_json_add_bool(resp, "success", false);
        csilk_json_add_number(resp, "latency_ms", (double)latency_ms);
        csilk_json_add_string(resp, "message", friendly_err);
    }

    csilk_ai_chat_response_free(&res);
    csilk_ai_free(test_ai);
    ai_config_release(cfg);

    *out_resp = resp;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

typedef struct {
    char*  data;
    size_t size;
} memory_buf_t;

static size_t
curl_write_memory_cb(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t        realsize = size * nmemb;
    memory_buf_t* mem = (memory_buf_t*)userp;
    char*         ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) {
        return 0;
    }
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    return realsize;
}

int
ai_usecase_fetch_models(const csilk_json_t*  body,
                        csilk_json_t**       out_resp,
                        ai_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!body || !out_resp) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "请求参数无效");
        return -1;
    }

    ai_config_t* cfg = ai_config_acquire();

    const char* id =
        csilk_json_get_string(body, "id") ? csilk_json_get_string(body, "id") : "openai";
    const char* base_url = csilk_json_get_string(body, "base_url");
    const char* k_enc = csilk_json_get_string(body, "api_key_enc");
    const char* k = csilk_json_get_string(body, "api_key");

    char        key_buf[512] = {0};
    const char* raw_k = (k_enc && k_enc[0]) ? k_enc : k;
    if (raw_k && raw_k[0]) {
        size_t dec_len = sizeof(key_buf);
        if (auth_key_decrypt(raw_k, key_buf, &dec_len) != 0) {
            size_t _k_len = strlen(raw_k);
            size_t _copy_len = _k_len < sizeof(key_buf) - 1 ? _k_len : sizeof(key_buf) - 1;
            memcpy(key_buf, raw_k, _copy_len);
            key_buf[_copy_len] = '\0';
        }
    } else {
        ai_provider_t* old_p = ai_config_find_provider(cfg, id);
        if (old_p && old_p->api_key[0]) {
            size_t _src_len = strlen(old_p->api_key);
            size_t _copy_len = _src_len < sizeof(key_buf) - 1 ? _src_len : sizeof(key_buf) - 1;
            memcpy(key_buf, old_p->api_key, _copy_len);
            key_buf[_copy_len] = '\0';
        } else {
            strcpy(key_buf, "dummy");
        }
    }

    char url[512];
    if (base_url && base_url[0]) {
        size_t blen = strlen(base_url);
        if (base_url[blen - 1] == '/') {
            snprintf(url, sizeof(url), "%smodels", base_url);
        } else {
            snprintf(url, sizeof(url), "%s/models", base_url);
        }
    } else {
        snprintf(url, sizeof(url), "https://api.openai.com/v1/models");
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        ai_config_release(cfg);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "CURL 初始化失败");
        return -1;
    }

    memory_buf_t chunk = {.data = malloc(1), .size = 0};
    if (chunk.data) {
        chunk.data[0] = '\0';
    }

    struct curl_slist* headers = NULL;
    char               auth_hdr[512];
    snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", key_buf);
    headers = curl_slist_append(headers, auth_hdr);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_memory_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    long     http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    csilk_json_t* out = csilk_json_object();
    csilk_json_t* model_arr = csilk_json_array();

    if (res == CURLE_OK && http_code == 200 && chunk.data) {
        csilk_json_t* parsed = csilk_json_parse(chunk.data);
        if (parsed) {
            const csilk_json_t* data_arr = csilk_json_get(parsed, "data");
            if (data_arr && csilk_json_is_array(data_arr)) {
                size_t dsz = csilk_json_array_size(data_arr);
                for (size_t i = 0; i < dsz; i++) {
                    const csilk_json_t* item = csilk_json_array_get(data_arr, i);
                    const char*         mid = csilk_json_get_string(item, "id");
                    if (mid && mid[0]) {
                        csilk_json_array_append(model_arr, csilk_json_string_new(mid));
                    }
                }
            }
            const csilk_json_t* models_arr = csilk_json_get(parsed, "models");
            if (models_arr && csilk_json_is_array(models_arr)) {
                size_t msz = csilk_json_array_size(models_arr);
                for (size_t i = 0; i < msz; i++) {
                    const csilk_json_t* item = csilk_json_array_get(models_arr, i);
                    const char*         mid = csilk_json_get_string(item, "name");
                    if (!mid) {
                        mid = csilk_json_get_string(item, "id");
                    }
                    if (!mid) {
                        mid = csilk_json_string_value(item);
                    }
                    if (mid && mid[0]) {
                        csilk_json_array_append(model_arr, csilk_json_string_new(mid));
                    }
                }
            }
            csilk_json_free(parsed);
        }
    }

    free(chunk.data);
    ai_config_release(cfg);
    csilk_json_add_array(out, "models", model_arr);

    *out_resp = out;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}
