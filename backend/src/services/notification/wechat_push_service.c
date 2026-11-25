#include "services/notification/wechat_push_service.h"
#include "common/http_safety.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "csilk/csilk.h"

static bool
push_allow_local_env(void)
{
    const char* v = getenv("MINEFOLIO_ALLOW_LOCAL_WEBHOOK");
    return v && (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0);
}

static void
set_err(char* err_buf, size_t err_len, const char* msg)
{
    if (err_buf && err_len > 0 && msg) {
        snprintf(err_buf, err_len, "%s", msg);
    }
}

typedef struct {
    int (*is_alive_fn)(void*);
    void* alive_ctx;
} push_curl_progress_ctx_t;

static int
push_curl_xferinfo_cb(
    void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;
    push_curl_progress_ctx_t* ctx = (push_curl_progress_ctx_t*)clientp;
    if (ctx && ctx->is_alive_fn && !ctx->is_alive_fn(ctx->alive_ctx)) {
        return 1;
    }
    return 0;
}

int
wechat_push_service_send(const push_channel_config_t* cfg,
                         const char*                  title,
                         const char*                  markdown_body,
                         char*                        err_buf,
                         size_t                       err_len)
{
    return wechat_push_service_send_cancellable(
        cfg, title, markdown_body, NULL, NULL, err_buf, err_len);
}

int
wechat_push_service_send_cancellable(const push_channel_config_t* cfg,
                                     const char*                  title,
                                     const char*                  markdown_body,
                                     int (*is_alive_fn)(void*),
                                     void*  alive_ctx,
                                     char*  err_buf,
                                     size_t err_len)
{
    if (err_buf && err_len > 0) {
        err_buf[0] = '\0';
    }
    if (!cfg || !title || !*title) {
        set_err(err_buf, err_len, "Invalid arguments: cfg or title is null");
        return -1;
    }
    if (is_alive_fn && !is_alive_fn(alive_ctx)) {
        set_err(err_buf, err_len, "测试请求已取消");
        return -1;
    }

    char target_url[512];
    target_url[0] = '\0';

    csilk_json_t* payload_json = csilk_json_object();
    if (!payload_json) {
        set_err(err_buf, err_len, "Failed to allocate payload json");
        return -1;
    }

    bool is_serverchan = (strcasecmp(cfg->channel_type, "serverchan") == 0);
    bool is_pushdeer = (strcasecmp(cfg->channel_type, "pushdeer") == 0);
    bool is_wechat_work = (strcasecmp(cfg->channel_type, "wechat_work") == 0 ||
                           strcasecmp(cfg->channel_type, "wechatwork") == 0 ||
                           strcasecmp(cfg->channel_type, "qywx") == 0);
    bool is_webhook = (strcasecmp(cfg->channel_type, "webhook") == 0);

    if (is_serverchan) {
        if (!cfg->secret_key[0]) {
            csilk_json_free(payload_json);
            set_err(err_buf, err_len, "ServerChan secret_key is required");
            return -1;
        }

        if (cfg->webhook_url[0] != '\0') {
            size_t ulen = strlen(cfg->webhook_url);
            if (strstr(cfg->webhook_url, ".send")) {
                snprintf(target_url, sizeof(target_url), "%s", cfg->webhook_url);
            } else if (cfg->webhook_url[ulen - 1] == '/') {
                snprintf(
                    target_url, sizeof(target_url), "%s%s.send", cfg->webhook_url, cfg->secret_key);
            } else {
                snprintf(target_url,
                         sizeof(target_url),
                         "%s/%s.send",
                         cfg->webhook_url,
                         cfg->secret_key);
            }
        } else {
            snprintf(
                target_url, sizeof(target_url), "https://sctapi.ftqq.com/%s.send", cfg->secret_key);
        }

        csilk_json_add_string(payload_json, "title", title);
        csilk_json_add_string(payload_json, "desp", markdown_body ? markdown_body : "");
    } else if (is_pushdeer) {
        if (!cfg->secret_key[0]) {
            csilk_json_free(payload_json);
            set_err(err_buf, err_len, "PushDeer pushkey/secret_key is required");
            return -1;
        }

        if (cfg->webhook_url[0] != '\0') {
            size_t ulen = strlen(cfg->webhook_url);
            if (strstr(cfg->webhook_url, "/push")) {
                snprintf(target_url, sizeof(target_url), "%s", cfg->webhook_url);
            } else if (cfg->webhook_url[ulen - 1] == '/') {
                snprintf(target_url, sizeof(target_url), "%smessage/push", cfg->webhook_url);
            } else {
                snprintf(target_url, sizeof(target_url), "%s/message/push", cfg->webhook_url);
            }
        } else {
            snprintf(target_url, sizeof(target_url), "https://api2.pushdeer.com/message/push");
        }

        csilk_json_add_string(payload_json, "pushkey", cfg->secret_key);
        csilk_json_add_string(payload_json, "text", title);
        csilk_json_add_string(payload_json, "desp", markdown_body ? markdown_body : "");
        csilk_json_add_string(payload_json, "type", "markdown");
    } else if (is_wechat_work) {
        if (!cfg->webhook_url[0] && !cfg->secret_key[0]) {
            csilk_json_free(payload_json);
            set_err(err_buf, err_len, "WeChat Work webhook URL or secret_key is required");
            return -1;
        }

        if (cfg->webhook_url[0] != '\0') {
            if (strstr(cfg->webhook_url, "key=")) {
                snprintf(target_url, sizeof(target_url), "%s", cfg->webhook_url);
            } else if (cfg->secret_key[0] != '\0') {
                char sep = strchr(cfg->webhook_url, '?') ? '&' : '?';
                snprintf(target_url,
                         sizeof(target_url),
                         "%s%ckey=%s",
                         cfg->webhook_url,
                         sep,
                         cfg->secret_key);
            } else {
                snprintf(target_url, sizeof(target_url), "%s", cfg->webhook_url);
            }
        } else {
            snprintf(target_url,
                     sizeof(target_url),
                     "https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=%s",
                     cfg->secret_key);
        }

        size_t b_len = markdown_body ? strlen(markdown_body) : 0;
        size_t t_len = strlen(title);
        char*  combined_md = (char*)malloc(t_len + b_len + 32);
        if (combined_md) {
            snprintf(combined_md,
                     t_len + b_len + 32,
                     "### %s\n\n%s",
                     title,
                     markdown_body ? markdown_body : "");
        }

        csilk_json_add_string(payload_json, "msgtype", "markdown");
        csilk_json_t* md_obj = csilk_json_object();
        csilk_json_add_string(
            md_obj, "content", combined_md ? combined_md : (markdown_body ? markdown_body : ""));
        csilk_json_add_object(payload_json, "markdown", md_obj);

        if (combined_md) {
            free(combined_md);
        }
    } else if (is_webhook) {
        if (!cfg->webhook_url[0]) {
            csilk_json_free(payload_json);
            set_err(err_buf, err_len, "Webhook URL is required");
            return -1;
        }
        snprintf(target_url, sizeof(target_url), "%s", cfg->webhook_url);

        csilk_json_add_string(payload_json, "title", title);
        csilk_json_add_string(payload_json, "content", markdown_body ? markdown_body : "");
        csilk_json_add_string(payload_json, "text", markdown_body ? markdown_body : "");
    } else {
        csilk_json_free(payload_json);
        set_err(err_buf, err_len, "Unknown channel_type");
        return -1;
    }

    size_t plen = 0;
    char*  payload_str = csilk_json_serialize(payload_json, &plen);
    csilk_json_free(payload_json);
    if (!payload_str) {
        set_err(err_buf, err_len, "Failed to serialize payload to string");
        return -1;
    }

    char resolve_entry[320] = {0};
    if (mf_http_validate_and_resolve_url(target_url,
                                         sizeof(target_url),
                                         push_allow_local_env(),
                                         false,
                                         resolve_entry,
                                         sizeof(resolve_entry)) != 0) {
        free(payload_str);
        set_err(err_buf, err_len, "Blocked push webhook URL by SSRF policy");
        return -1;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        free(payload_str);
        set_err(err_buf, err_len, "curl_easy_init failed");
        return -1;
    }

    mf_curl_buf_t      resp = {.data = NULL, .size = 0, .cap = 0, .max_bytes = MF_CURL_MAX_1MB};
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");

    char auth_header[256];
    if (is_webhook && cfg->secret_key[0] != '\0') {
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", cfg->secret_key);
        headers = curl_slist_append(headers, auth_header);
    }

    struct curl_slist* resolve_list = mf_http_curl_apply_security(curl, resolve_entry, NULL, false);
    curl_easy_setopt(curl, CURLOPT_URL, target_url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload_str);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mf_curl_bounded_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&resp);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Minefolio/1.0 NotificationAgent");

    /* Support test environments */
    const char* insecure_env = getenv("MINEFOLIO_ALLOW_INSECURE_SSL");
    if (insecure_env && (strcmp(insecure_env, "1") == 0 || strcasecmp(insecure_env, "true") == 0)) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    push_curl_progress_ctx_t prog_ctx = {.is_alive_fn = is_alive_fn, .alive_ctx = alive_ctx};
    if (is_alive_fn) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, push_curl_xferinfo_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void*)&prog_ctx);
    }

    CURLcode res = curl_easy_perform(curl);
    long     http_code = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    }

    if (resolve_list) {
        curl_slist_free_all(resolve_list);
    }
    curl_slist_free_all(headers);
    free(payload_str);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (resp.data) {
            free(resp.data);
        }
        if (res == CURLE_ABORTED_BY_CALLBACK || (is_alive_fn && !is_alive_fn(alive_ctx))) {
            set_err(err_buf, err_len, "测试请求已取消");
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "cURL request failed: %s", curl_easy_strerror(res));
            set_err(err_buf, err_len, msg);
        }
        return -1;
    }

    if (http_code < 200 || http_code >= 300) {
        char msg[256];
        snprintf(msg,
                 sizeof(msg),
                 "HTTP %ld error from push gateway%s%s",
                 http_code,
                 resp.data ? ": " : "",
                 resp.data ? resp.data : "");
        if (resp.data) {
            free(resp.data);
        }
        set_err(err_buf, err_len, msg);
        return -1;
    }

    /* Check gateway-level JSON errors if returned */
    if (resp.data) {
        csilk_json_t* res_json = csilk_json_parse(resp.data);
        if (res_json) {
            if (is_serverchan) {
                int64_t code = csilk_json_get_int(res_json, "code");
                if (code != 0) {
                    const char* errmsg = csilk_json_get_string(res_json, "message");
                    char        msg[256];
                    snprintf(msg,
                             sizeof(msg),
                             "ServerChan error code %lld: %s",
                             (long long)code,
                             errmsg ? errmsg : "unknown");
                    set_err(err_buf, err_len, msg);
                    csilk_json_free(res_json);
                    free(resp.data);
                    return -1;
                }
            } else if (is_pushdeer) {
                int64_t code = csilk_json_get_int(res_json, "code");
                if (code != 0) {
                    const char* errmsg = csilk_json_get_string(res_json, "error");
                    char        msg[256];
                    snprintf(msg,
                             sizeof(msg),
                             "PushDeer error code %lld: %s",
                             (long long)code,
                             errmsg ? errmsg : "unknown");
                    set_err(err_buf, err_len, msg);
                    csilk_json_free(res_json);
                    free(resp.data);
                    return -1;
                }
            } else if (is_wechat_work) {
                int64_t errcode = csilk_json_get_int(res_json, "errcode");
                if (errcode != 0) {
                    const char* errmsg = csilk_json_get_string(res_json, "errmsg");
                    char        msg[256];
                    snprintf(msg,
                             sizeof(msg),
                             "WeChat Work error code %lld: %s",
                             (long long)errcode,
                             errmsg ? errmsg : "unknown");
                    set_err(err_buf, err_len, msg);
                    csilk_json_free(res_json);
                    free(resp.data);
                    return -1;
                }
            }
            csilk_json_free(res_json);
        }
        free(resp.data);
    }

    return 0;
}
