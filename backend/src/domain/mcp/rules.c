#include "domain/mcp/rules.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
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
ipv4_is_restricted(uint32_t ip)
{
    return ((ip >> 24) == 127U ||                 /* 127.0.0.0/8 */
            (ip >> 24) == 10U ||                  /* 10.0.0.0/8 */
            (ip >> 20) == ((172U << 4) | 1U) ||   /* 172.16.0.0/12 */
            (ip >> 16) == ((192U << 8) | 168U) || /* 192.168.0.0/16 */
            (ip >> 16) == ((169U << 8) | 254U) || /* 169.254.0.0/16 */
            (ip & 0xFFC00000U) == 0x64400000U ||  /* 100.64.0.0/10 */
            (ip >> 24) == 0U ||                   /* 0.0.0.0/8 */
            (ip >> 28) >= 14U ||                  /* 224.0.0.0/4+ */
            ip == 0xFFFFFFFFU);
}

static bool
url_host_is_internal(const char* host)
{
    if (!host || !host[0]) {
        return true; /* 无 host 视为非法 */
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

    const char* hostpart = buf;
    const char* at = strchr(buf, '@');
    if (at) {
        hostpart = at + 1;
    }

    char hbuf[256];
    if (hostpart[0] == '[') {
        const char* endb = strchr(hostpart, ']');
        if (!endb) {
            return true;
        }
        size_t blen = (size_t)(endb - (hostpart + 1));
        if (blen == 0 || blen >= sizeof(hbuf)) {
            return true;
        }
        memcpy(hbuf, hostpart + 1, blen);
        hbuf[blen] = '\0';
        hostpart = hbuf;
    } else {
        snprintf(hbuf, sizeof(hbuf), "%s", hostpart);
        char* first_colon = strchr(hbuf, ':');
        char* last_colon = strrchr(hbuf, ':');
        if (first_colon && first_colon == last_colon) {
            *first_colon = '\0';
        }
        hostpart = hbuf;
    }

    if (!hostpart[0] || strcmp(hostpart, "localhost") == 0 ||
        strcmp(hostpart, "host.docker.internal") == 0) {
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

    struct in_addr in4;
    if (inet_aton(hostpart, &in4)) {
        return ipv4_is_restricted(ntohl(in4.s_addr));
    }

    struct in6_addr in6;
    if (inet_pton(AF_INET6, hostpart, &in6) == 1) {
        if (IN6_IS_ADDR_LOOPBACK(&in6) || IN6_IS_ADDR_LINKLOCAL(&in6) ||
            IN6_IS_ADDR_SITELOCAL(&in6) || IN6_IS_ADDR_UNSPECIFIED(&in6) ||
            IN6_IS_ADDR_MULTICAST(&in6)) {
            return true;
        }
        if (IN6_IS_ADDR_V4MAPPED(&in6)) {
            uint32_t v4 = 0;
            memcpy(&v4, &in6.s6_addr[12], sizeof(v4));
            return ipv4_is_restricted(ntohl(v4));
        }
        if ((in6.s6_addr[0] & 0xfe) == 0xfc) {
            return true;
        }
        return false;
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
                if (ipv4_is_restricted(ntohl(s4->sin_addr.s_addr))) {
                    resolved_internal = true;
                    break;
                }
            } else if (it->ai_family == AF_INET6) {
                struct sockaddr_in6* s6 = (struct sockaddr_in6*)it->ai_addr;
                if (IN6_IS_ADDR_LOOPBACK(&s6->sin6_addr) || IN6_IS_ADDR_LINKLOCAL(&s6->sin6_addr) ||
                    IN6_IS_ADDR_SITELOCAL(&s6->sin6_addr) ||
                    IN6_IS_ADDR_UNSPECIFIED(&s6->sin6_addr) ||
                    IN6_IS_ADDR_MULTICAST(&s6->sin6_addr)) {
                    resolved_internal = true;
                    break;
                }
                if (IN6_IS_ADDR_V4MAPPED(&s6->sin6_addr)) {
                    uint32_t v4 = 0;
                    memcpy(&v4, &s6->sin6_addr.s6_addr[12], sizeof(v4));
                    if (ipv4_is_restricted(ntohl(v4))) {
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

    /* 解析出 host（兼容 IPv6 方括号 [::1]:port） */
    const char* p = url + (strncmp(url, "https://", 8) == 0 ? 8 : 7);
    const char* host_start = p;
    if (*p == '[') {
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
