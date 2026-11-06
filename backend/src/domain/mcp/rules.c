#include "domain/mcp/rules.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>

/* 名称：^[a-z0-9_-]{1,64}$ */
bool
mf_mcp_rule_validate_name(const char* name)
{
    if (!name || !name[0]) {
        return false;
    }
    size_t len = strlen(name);
    if (len > 64) {
        return false;
    }
    for (const char* p = name; *p; p++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool
mf_mcp_rule_validate_transport(const char* transport)
{
    if (!transport) {
        return false;
    }
    return strcmp(transport, "http") == 0 || strcmp(transport, "stdio") == 0;
}

/* 判断 URL host 是否指向内网/回环地址 */
static bool
url_host_is_internal(const char* host)
{
    if (!host || !host[0]) {
        return true; /* 无 host 视为非法 */
    }
    const char* lower = host;
    /* 小写化 host 前段用于比较 */
    char   buf[256];
    size_t n = strlen(host);
    if (n >= sizeof(buf)) {
        n = sizeof(buf) - 1;
    }
    memcpy(buf, host, n);
    buf[n] = '\0';
    for (size_t i = 0; i < n; i++) {
        buf[i] = (char)tolower((unsigned char)buf[i]);
    }
    lower = buf;

    /* 去掉可能存在的 user@ 前缀与 :port 后缀 */
    const char* hostpart = lower;
    const char* at = strchr(lower, '@');
    if (at) {
        hostpart = at + 1;
    }
    char hbuf[256];
    strncpy(hbuf, hostpart, sizeof(hbuf) - 1);
    hbuf[sizeof(hbuf) - 1] = '\0';
    char* colon = strchr(hbuf, ':');
    if (colon) {
        *colon = '\0';
    }
    hostpart = hbuf;

    if (strcmp(hostpart, "localhost") == 0) {
        return true;
    }

    size_t hlen = strlen(hostpart);
    if ((hlen > 6 && strcmp(hostpart + hlen - 6, ".local") == 0) ||
        (hlen > 9 && strcmp(hostpart + hlen - 9, ".internal") == 0) ||
        (hlen > 4 && strcmp(hostpart + hlen - 4, ".lan") == 0) ||
        (hlen > 12 && strcmp(hostpart + hlen - 12, ".localdomain") == 0) ||
        (hlen > 10 && strcmp(hostpart + hlen - 10, ".home.arpa") == 0)) {
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
            int second = atoi(hostpart + 4);
            if (second >= 16 && second <= 31) {
                return true;
            }
        }
        if (strncmp(hostpart, "169.254.", 8) == 0) {
            return true; /* 链路本地 */
        }
        return false;
    }
    /* IPv6 回环 */
    if (strcmp(hostpart, "::1") == 0) {
        return true;
    }

    /* 域名深度检测：getaddrinfo 校验背后解析的所有 IP */
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* addr_res = NULL;
    if (getaddrinfo(hostpart, NULL, &hints, &addr_res) == 0 && addr_res) {
        bool resolved_internal = false;
        for (struct addrinfo* it = addr_res; it != NULL; it = it->ai_next) {
            if (it->ai_family == AF_INET) {
                struct sockaddr_in* s4 = (struct sockaddr_in*)it->ai_addr;
                uint32_t            ip = ntohl(s4->sin_addr.s_addr);
                if ((ip >> 24) == 127 || (ip >> 24) == 10 || (ip >> 20) == ((172 << 4) | 1) ||
                    (ip >> 16) == ((192 << 8) | 168) || (ip >> 16) == ((169 << 8) | 254) ||
                    (ip >> 24) == 0 || ip == 0xFFFFFFFF) {
                    resolved_internal = true;
                    break;
                }
            } else if (it->ai_family == AF_INET6) {
                struct sockaddr_in6* s6 = (struct sockaddr_in6*)it->ai_addr;
                if (IN6_IS_ADDR_LOOPBACK(&s6->sin6_addr) || IN6_IS_ADDR_LINKLOCAL(&s6->sin6_addr) ||
                    IN6_IS_ADDR_SITELOCAL(&s6->sin6_addr) ||
                    IN6_IS_ADDR_UNSPECIFIED(&s6->sin6_addr)) {
                    resolved_internal = true;
                    break;
                }
                if (IN6_IS_ADDR_V4MAPPED(&s6->sin6_addr)) {
                    uint32_t v4 = ntohl(*(const uint32_t*)&s6->sin6_addr.s6_addr[12]);
                    if ((v4 >> 24) == 127 || (v4 >> 24) == 10 || (v4 >> 20) == ((172 << 4) | 1) ||
                        (v4 >> 16) == ((192 << 8) | 168) || (v4 >> 16) == ((169 << 8) | 254) ||
                        (v4 >> 24) == 0 || v4 == 0xFFFFFFFF) {
                        resolved_internal = true;
                        break;
                    }
                }
                if ((s6->sin6_addr.s6_addr[0] & 0xfe) == 0xfc) {
                    resolved_internal = true;
                    break;
                }
            }
        }
        freeaddrinfo(addr_res);
        if (resolved_internal) {
            return true;
        }
    }

    return false;
}

bool
mf_mcp_rule_validate_url(const char* url, bool allow_local)
{
    if (!url || !url[0]) {
        return false;
    }
    size_t len = strlen(url);
    if (len > 512) {
        return false;
    }
    if (!(strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0)) {
        return false;
    }

    if (allow_local) {
        return true;
    }

    /* 解析出 host */
    const char* p = url + (url[0] == 'h' && strncmp(url, "https", 5) == 0 ? 8 : 7);
    const char* host_start = p;
    while (*p && *p != '/' && *p != '?' && *p != '#' && *p != ':') {
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

bool
mf_mcp_rule_validate_command(const char* command)
{
    if (!command || !command[0]) {
        return false;
    }
    size_t len = strlen(command);
    if (len > 512) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = command[i];
        /* 禁止 shell 元字符（纵深防御，execvp 不走 shell） */
        if (c == ';' || c == '&' || c == '|' || c == '`' || c == '$' || c == '>' || c == '<') {
            return false;
        }
    }
    return true;
}

bool
mf_mcp_rule_validate_timeout_ms(int timeout_ms)
{
    return timeout_ms >= 1000 && timeout_ms <= 120000;
}
