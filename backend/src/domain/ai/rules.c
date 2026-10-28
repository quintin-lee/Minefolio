#include "domain/ai/rules.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

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

int
mf_ai_rule_model_prices(const char*   provider,
                        const char*   model,
                        const double* provider_in,
                        const double* provider_out,
                        double*       out_in,
                        double*       out_out)
{
    if (!out_in || !out_out) {
        return -1;
    }
    if ((provider_in && *provider_in < 0.0) || (provider_out && *provider_out < 0.0)) {
        return -1;
    }
    /* 默认价表（与既有 trace 估算口径一致；4o-mini 必须先于 gpt-4o 判定） */
    double in_p = 0.50;
    double out_p = 1.50;
    if (model && strstr(model, "gpt-4o-mini")) {
        in_p = 0.15;
        out_p = 0.60;
    } else if (model && strstr(model, "4o-mini")) {
        in_p = 0.15;
        out_p = 0.60;
    } else if ((model && strstr(model, "gpt-4o")) || (model && strstr(model, "gpt-4"))) {
        in_p = 2.50;
        out_p = 10.00;
    } else if ((provider && strstr(provider, "deepseek")) || (model && strstr(model, "deepseek"))) {
        in_p = 0.14;
        out_p = 0.28;
    } else if ((provider && strstr(provider, "qwen")) || (model && strstr(model, "qwen"))) {
        in_p = 0.20;
        out_p = 0.60;
    }
    if (provider && strstr(provider, "ollama")) {
        in_p = 0.0;
        out_p = 0.0;
    }
    /* 配置价逐侧覆盖：>0 的字段生效，<=0 的侧保持默认表值 */
    if (provider_in && *provider_in > 0.0) {
        in_p = *provider_in;
    }
    if (provider_out && *provider_out > 0.0) {
        out_p = *provider_out;
    }
    *out_in = in_p;
    *out_out = out_p;
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

    /* 使用 inet_aton 检查 IPv4（支持十进制点分、八进制、十六进制、纯整数与 0） */
    struct in_addr in4;
    if (inet_aton(hostpart, &in4)) {
        uint32_t ip = ntohl(in4.s_addr);
        if ((ip >> 24) == 127 ||                /* 127.0.0.0/8 回环 */
            (ip >> 24) == 10 ||                 /* 10.0.0.0/8 私网 */
            (ip >> 20) == ((172 << 4) | 1) ||   /* 172.16.0.0/12 (172.16-172.31) */
            (ip >> 16) == ((192 << 8) | 168) || /* 192.168.0.0/16 私网 */
            (ip >> 16) == ((169 << 8) | 254) || /* 169.254.0.0/16 链路本地 */
            (ip >> 24) == 0 ||                  /* 0.0.0.0/8 及 0 */
            ip == 0xFFFFFFFF) {                 /* 255.255.255.255 广播 */
            return true;
        }
        return false;
    }

    /* 使用 inet_pton 检查标准 IPv6 及 IPv4-mapped IPv6 */
    struct in6_addr in6;
    if (inet_pton(AF_INET6, hostpart, &in6) == 1) {
        if (IN6_IS_ADDR_LOOPBACK(&in6) || IN6_IS_ADDR_LINKLOCAL(&in6) ||
            IN6_IS_ADDR_SITELOCAL(&in6) || IN6_IS_ADDR_UNSPECIFIED(&in6)) {
            return true;
        }
        if (IN6_IS_ADDR_V4MAPPED(&in6)) {
            uint32_t v4 = ntohl(*(const uint32_t*)&in6.s6_addr[12]);
            if ((v4 >> 24) == 127 || (v4 >> 24) == 10 || (v4 >> 20) == ((172 << 4) | 1) ||
                (v4 >> 16) == ((192 << 8) | 168) || (v4 >> 16) == ((169 << 8) | 254) ||
                (v4 >> 24) == 0 || v4 == 0xFFFFFFFF) {
                return true;
            }
        }
        /* 唯一本地 IPv6 fc00::/7 (包括 fc00:: 与 fd00::) */
        if ((in6.s6_addr[0] & 0xfe) == 0xfc) {
            return true;
        }
        return false;
    }

    /* 兜底文本前缀匹配 */
    if (strncmp(hostpart, "127.", 4) == 0 || strcmp(hostpart, "0.0.0.0") == 0 ||
        strncmp(hostpart, "10.", 3) == 0 || strncmp(hostpart, "192.168.", 8) == 0 ||
        strncmp(hostpart, "169.254.", 8) == 0 || strcmp(hostpart, "::1") == 0 ||
        strcmp(hostpart, "::") == 0) {
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
    if (max_len == 0 || max_len > 4200) {
        max_len = 4200;
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
