#include "common/http_safety.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static bool
ipv4_is_restricted(uint32_t ip, bool allow_local)
{
    /* 云元数据链路本地 169.254.0.0/16：任何模式均拦截 */
    if ((ip >> 16) == ((169U << 8) | 254U)) {
        return true;
    }
    if (allow_local) {
        return false;
    }
    if ((ip >> 24) == 127U ||                 /* 127.0.0.0/8 回环 */
        (ip >> 24) == 10U ||                  /* 10.0.0.0/8 私网 */
        (ip >> 20) == ((172U << 4) | 1U) ||   /* 172.16.0.0/12 */
        (ip >> 16) == ((192U << 8) | 168U) || /* 192.168.0.0/16 */
        (ip & 0xFFC00000U) == 0x64400000U ||  /* 100.64.0.0/10 CGNAT */
        (ip >> 24) == 0U ||                   /* 0.0.0.0/8 */
        (ip >> 28) >= 14U ||                  /* 224.0.0.0/4 多播及 240.0.0.0/4 保留/广播 */
        ip == 0xFFFFFFFFU) {
        return true;
    }
    return false;
}

static bool
ipv6_is_restricted(const struct in6_addr* addr, bool allow_local)
{
    if (IN6_IS_ADDR_LINKLOCAL(addr)) {
        return true;
    }
    if (IN6_IS_ADDR_V4MAPPED(addr)) {
        uint32_t v4 = 0;
        memcpy(&v4, &addr->s6_addr[12], sizeof(v4));
        return ipv4_is_restricted(ntohl(v4), allow_local);
    }
    if (allow_local) {
        return false;
    }
    if (IN6_IS_ADDR_LOOPBACK(addr) || IN6_IS_ADDR_SITELOCAL(addr) ||
        IN6_IS_ADDR_UNSPECIFIED(addr) || IN6_IS_ADDR_MULTICAST(addr)) {
        return true;
    }
    /* ULA fc00::/7 */
    if ((addr->s6_addr[0] & 0xfe) == 0xfc) {
        return true;
    }
    return false;
}

static bool
extract_host_and_port(
    const char* url, char* host_out, size_t host_cap, char* port_out, size_t port_cap)
{
    if (!url || !host_out || host_cap < 2 || !port_out || port_cap < 6) {
        return false;
    }
    bool        is_https = false;
    const char* p = NULL;
    if (strncmp(url, "https://", 8) == 0) {
        is_https = true;
        p = url + 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        is_https = false;
        p = url + 7;
    } else {
        return false;
    }

    const char* authority_start = p;
    while (*p && *p != '/' && *p != '?' && *p != '#') {
        p++;
    }
    size_t auth_len = (size_t)(p - authority_start);
    if (auth_len == 0 || auth_len >= host_cap + 16) {
        return false;
    }

    char auth[512];
    if (auth_len >= sizeof(auth)) {
        return false;
    }
    memcpy(auth, authority_start, auth_len);
    auth[auth_len] = '\0';

    /* 拒绝 URL 内嵌 user:pass@ 凭证（绕过混淆与凭据泄漏风险） */
    if (strchr(auth, '@') != NULL) {
        return false;
    }

    const char* default_port = is_https ? "443" : "80";
    snprintf(port_out, port_cap, "%s", default_port);

    if (auth[0] == '[') {
        char* endb = strchr(auth, ']');
        if (!endb) {
            return false;
        }
        size_t hlen = (size_t)(endb - (auth + 1));
        if (hlen == 0 || hlen >= host_cap) {
            return false;
        }
        memcpy(host_out, auth + 1, hlen);
        host_out[hlen] = '\0';
        if (endb[1] == ':') {
            const char* pstr = endb + 2;
            if (!pstr[0]) {
                return false;
            }
            for (const char* q = pstr; *q; q++) {
                if (!isdigit((unsigned char)*q)) {
                    return false;
                }
            }
            long pv = strtol(pstr, NULL, 10);
            if (pv <= 0 || pv > 65535) {
                return false;
            }
            snprintf(port_out, port_cap, "%ld", pv);
        } else if (endb[1] != '\0') {
            return false;
        }
    } else {
        char* first_colon = strchr(auth, ':');
        char* last_colon = strrchr(auth, ':');
        if (first_colon && first_colon != last_colon) {
            /* 多冒号裸 IPv6 */
            size_t hlen = strlen(auth);
            if (hlen == 0 || hlen >= host_cap) {
                return false;
            }
            memcpy(host_out, auth, hlen + 1);
        } else if (first_colon) {
            *first_colon = '\0';
            const char* pstr = first_colon + 1;
            if (!pstr[0]) {
                return false;
            }
            for (const char* q = pstr; *q; q++) {
                if (!isdigit((unsigned char)*q)) {
                    return false;
                }
            }
            long pv = strtol(pstr, NULL, 10);
            if (pv <= 0 || pv > 65535) {
                return false;
            }
            snprintf(port_out, port_cap, "%ld", pv);
            size_t hlen = strlen(auth);
            if (hlen == 0 || hlen >= host_cap) {
                return false;
            }
            memcpy(host_out, auth, hlen + 1);
        } else {
            size_t hlen = strlen(auth);
            if (hlen == 0 || hlen >= host_cap) {
                return false;
            }
            memcpy(host_out, auth, hlen + 1);
        }
    }

    for (size_t i = 0; host_out[i]; i++) {
        host_out[i] = (char)tolower((unsigned char)host_out[i]);
    }
    return host_out[0] != '\0';
}

bool
mf_http_host_is_private_literal(const char* host)
{
    if (!host || !host[0]) {
        return true;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", host);
    for (size_t i = 0; buf[i]; i++) {
        buf[i] = (char)tolower((unsigned char)buf[i]);
    }
    size_t hlen = strlen(buf);
    while (hlen > 0 && buf[hlen - 1] == '.') {
        buf[--hlen] = '\0';
    }
    if (hlen == 0) {
        return true;
    }
    if (strcmp(buf, "localhost") == 0 || strcmp(buf, "host.docker.internal") == 0) {
        return true;
    }
    if ((hlen > 6 && strcmp(buf + hlen - 6, ".local") == 0) ||
        (hlen > 9 && strcmp(buf + hlen - 9, ".internal") == 0) ||
        (hlen > 4 && strcmp(buf + hlen - 4, ".lan") == 0) ||
        (hlen > 12 && strcmp(buf + hlen - 12, ".localdomain") == 0) ||
        (hlen > 10 && strcmp(buf + hlen - 10, ".home.arpa") == 0)) {
        return true;
    }

    struct in_addr in4;
    if (inet_aton(buf, &in4)) {
        return ipv4_is_restricted(ntohl(in4.s_addr), false);
    }
    struct in6_addr in6;
    if (inet_pton(AF_INET6, buf, &in6) == 1) {
        return ipv6_is_restricted(&in6, false);
    }
    return false;
}

int
mf_http_validate_and_resolve_url(const char* url,
                                 size_t      max_len,
                                 bool        allow_local,
                                 bool        has_proxy,
                                 char*       resolve_entry,
                                 size_t      resolve_cap)
{
    if (resolve_entry && resolve_cap > 0) {
        resolve_entry[0] = '\0';
    }
    if (!url || !url[0]) {
        return -1;
    }
    if (max_len == 0) {
        max_len = 2048;
    }
    if (strlen(url) > max_len) {
        return -1;
    }

    char host[256] = {0};
    char port[16] = {0};
    if (!extract_host_and_port(url, host, sizeof(host), port, sizeof(port))) {
        return -1;
    }
    size_t host_len = strlen(host);
    while (host_len > 0 && host[host_len - 1] == '.') {
        host[--host_len] = '\0';
    }
    if (host_len == 0) {
        return -1;
    }

    if (!allow_local && mf_http_host_is_private_literal(host)) {
        return -1;
    }

    struct in_addr in4_lit;
    if (inet_aton(host, &in4_lit)) {
        if (ipv4_is_restricted(ntohl(in4_lit.s_addr), allow_local)) {
            return -1;
        }
        return 0;
    }
    struct in6_addr in6_lit;
    if (inet_pton(AF_INET6, host, &in6_lit) == 1) {
        if (ipv6_is_restricted(&in6_lit, allow_local)) {
            return -1;
        }
        return 0;
    }

    if (has_proxy) {
        return 0;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = NULL;
    int              gai = getaddrinfo(host, port, &hints, &res);
    if (gai != 0 || !res) {
        return -1;
    }

    char first_ip[INET6_ADDRSTRLEN] = {0};
    bool blocked = false;
    for (struct addrinfo* it = res; it != NULL; it = it->ai_next) {
        if (it->ai_family == AF_INET) {
            struct sockaddr_in* s4 = (struct sockaddr_in*)it->ai_addr;
            if (ipv4_is_restricted(ntohl(s4->sin_addr.s_addr), allow_local)) {
                blocked = true;
                break;
            }
            if (!first_ip[0]) {
                inet_ntop(AF_INET, &s4->sin_addr, first_ip, sizeof(first_ip));
            }
        } else if (it->ai_family == AF_INET6) {
            struct sockaddr_in6* s6 = (struct sockaddr_in6*)it->ai_addr;
            if (ipv6_is_restricted(&s6->sin6_addr, allow_local)) {
                blocked = true;
                break;
            }
            if (!first_ip[0]) {
                char tmp[INET6_ADDRSTRLEN];
                if (inet_ntop(AF_INET6, &s6->sin6_addr, tmp, sizeof(tmp))) {
                    snprintf(first_ip, sizeof(first_ip), "%s", tmp);
                }
            }
        }
    }
    freeaddrinfo(res);

    if (blocked || !first_ip[0]) {
        return -1;
    }

    if (resolve_entry && resolve_cap > 0) {
        if (strchr(first_ip, ':')) {
            snprintf(resolve_entry, resolve_cap, "%s:%s:[%s]", host, port, first_ip);
        } else {
            snprintf(resolve_entry, resolve_cap, "%s:%s:%s", host, port, first_ip);
        }
    }
    return 0;
}

struct curl_slist*
mf_http_curl_apply_security(CURL*       curl,
                            const char* resolve_entry,
                            const char* proxy,
                            bool        allow_redirects)
{
    if (!curl) {
        return NULL;
    }
    (void)allow_redirects;
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    /* Never enable automatic libcurl redirects: callers needing 3xx support
     * must follow Location hops manually and re-run mf_http_validate_and_resolve_url. */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
#if defined(CURLOPT_PROTOCOLS_STR)
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#elif defined(CURLOPT_PROTOCOLS)
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    if ((!proxy || !proxy[0]) && resolve_entry && resolve_entry[0]) {
        struct curl_slist* resolve_list = curl_slist_append(NULL, resolve_entry);
        if (resolve_list) {
            curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve_list);
        }
        return resolve_list;
    }
    return NULL;
}

size_t
mf_curl_bounded_write_cb(void* contents, size_t size, size_t nmemb, void* userdata)
{
    mf_curl_buf_t* mem = (mf_curl_buf_t*)userdata;
    if (!mem || !contents) {
        return 0;
    }
    if (size != 0 && nmemb > SIZE_MAX / size) {
        mem->overflow = true;
        return 0;
    }
    size_t realsize = size * nmemb;
    size_t limit = mem->max_bytes > 0 ? mem->max_bytes : MF_CURL_MAX_4MB;
    if (realsize > limit || mem->size > limit - realsize) {
        mem->overflow = true;
        return 0;
    }
    size_t needed = mem->size + realsize + 1;
    if (needed > mem->cap) {
        size_t new_cap = mem->cap ? mem->cap : 4096;
        while (new_cap < needed) {
            if (new_cap > (limit + 1) / 2) {
                new_cap = limit + 1;
                break;
            }
            new_cap *= 2;
        }
        if (new_cap < needed) {
            new_cap = needed;
        }
        char* ptr = (char*)realloc(mem->data, new_cap);
        if (!ptr) {
            return 0;
        }
        mem->data = ptr;
        mem->cap = new_cap;
    }
    memcpy(mem->data + mem->size, contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = '\0';
    return realsize;
}
