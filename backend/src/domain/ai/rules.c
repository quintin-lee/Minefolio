#include "domain/ai/rules.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

int
mf_ai_rule_validate_session_title(
    const char* title, char* out_title, size_t out_cap, char* err_buf, size_t err_len)
{
    if (!out_title || out_cap == 0) {
        return -1;
    }

    if (!title || !title[0]) {
        snprintf(out_title, out_cap, "新对话");
        return 0;
    }

    size_t len = strlen(title);
    if (len > 128) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "会话标题不能超过 128 字符");
        }
        return -1;
    }

    snprintf(out_title, out_cap, "%s", title);
    return 0;
}

int
mf_ai_rule_clamp_context_size(int requested_size)
{
    if (requested_size < 5) {
        return 5;
    }
    if (requested_size > 100) {
        return 100;
    }
    return requested_size;
}

int
mf_ai_rule_calculate_token_cost(int64_t prompt_tokens,
                                int64_t completion_tokens,
                                double  prompt_unit_price,
                                double  completion_unit_price,
                                double* out_cost)
{
    if (!out_cost) {
        return -1;
    }
    if (prompt_tokens < 0 || completion_tokens < 0 || prompt_unit_price < 0 ||
        completion_unit_price < 0) {
        return -1;
    }

    double cost =
        (prompt_tokens * prompt_unit_price + completion_tokens * completion_unit_price) / 1000000.0;
    *out_cost = cost;
    return 0;
}

bool
mf_ai_rule_is_safe_provider(const char* provider_id)
{
    if (!provider_id || !provider_id[0]) {
        return false;
    }
    size_t len = strlen(provider_id);
    if (len > 32) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        char ch = provider_id[i];
        if (!isalnum((unsigned char)ch) && ch != '_' && ch != '-') {
            return false;
        }
    }
    return true;
}

static bool
url_host_is_internal(const char* host)
{
    if (!host || !host[0]) {
        return true;
    }

    char   buf[256];
    size_t n = strlen(host);
    if (n >= sizeof(buf)) {
        n = sizeof(buf) - 1;
    }
    for (size_t i = 0; i < n; i++) {
        buf[i] = (char)tolower((unsigned char)host[i]);
    }
    buf[n] = '\0';

    /* 去掉可能存在的 user@ 前缀 */
    const char* hostpart = buf;
    const char* at = strchr(buf, '@');
    if (at) {
        hostpart = at + 1;
    }

    /* 处理 IPv6 [::1] 括号与去端口 */
    char hbuf[256];
    if (hostpart[0] == '[') {
        const char* end_bracket = strchr(hostpart, ']');
        if (end_bracket) {
            size_t blen = (size_t)(end_bracket - (hostpart + 1));
            if (blen >= sizeof(hbuf)) {
                blen = sizeof(hbuf) - 1;
            }
            memcpy(hbuf, hostpart + 1, blen);
            hbuf[blen] = '\0';
            hostpart = hbuf;
        }
    } else {
        strncpy(hbuf, hostpart, sizeof(hbuf) - 1);
        hbuf[sizeof(hbuf) - 1] = '\0';
        char* first_colon = strchr(hbuf, ':');
        char* last_colon = strrchr(hbuf, ':');
        /* 仅在单冒号（host:port）时切除端口；多冒号为裸 IPv6 地址（如 ::1） */
        if (first_colon && first_colon == last_colon) {
            *first_colon = '\0';
        }
        hostpart = hbuf;
    }

    if (!hostpart[0] || strcmp(hostpart, "localhost") == 0) {
        return true;
    }

    /* 纯 IPv4 文本 */
    if (hostpart[0] >= '0' && hostpart[0] <= '9') {
        if (strncmp(hostpart, "127.", 4) == 0 || strcmp(hostpart, "0.0.0.0") == 0) {
            return true;
        }
        if (strncmp(hostpart, "10.", 3) == 0) {
            return true;
        }
        if (strncmp(hostpart, "192.168.", 8) == 0) {
            return true;
        }
        if (strncmp(hostpart, "172.", 4) == 0) {
            /* 172.16.0.0/12 */
            int second = 0;
            if (sscanf(hostpart + 4, "%d", &second) == 1) {
                if (second >= 16 && second <= 31) {
                    return true;
                }
            }
        }
        if (strncmp(hostpart, "169.254.", 8) == 0) {
            return true; /* 链路本地 */
        }
        return false;
    }

    /* IPv6 回环与内网 */
    if (strcmp(hostpart, "::1") == 0 || strcmp(hostpart, "::") == 0 ||
        strncmp(hostpart, "fe80:", 5) == 0 || strncmp(hostpart, "fc00:", 5) == 0 ||
        strncmp(hostpart, "fd", 2) == 0) {
        return true;
    }

    return false;
}

bool
mf_ai_rule_validate_url(const char* url, size_t max_len)
{
    if (!url || !url[0]) {
        return false;
    }
    if (max_len == 0 || max_len > 4096) {
        max_len = 4096;
    }
    size_t len = strlen(url);
    if (len > max_len) {
        return false;
    }
    if (!(strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0)) {
        return false;
    }

    /* 解析 host */
    const char* p = url + (url[0] == 'h' && strncmp(url, "https://", 8) == 0 ? 8 : 7);
    const char* host_start = p;
    if (*p == '[') {
        /* IPv6 */
        while (*p && *p != ']' && *p != '/' && *p != '?' && *p != '#') {
            p++;
        }
        if (*p == ']') {
            p++;
        }
    }
    while (*p && *p != '/' && *p != '?' && *p != '#') {
        p++;
    }

    char   host[256];
    size_t hl = (size_t)(p - host_start);
    if (hl == 0 || hl >= sizeof(host)) {
        return false;
    }
    memcpy(host, host_start, hl);
    host[hl] = '\0';

    return !url_host_is_internal(host);
}
