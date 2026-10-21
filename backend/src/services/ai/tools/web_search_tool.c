#include "services/ai/tools/web_search_tool.h"
#include "services/ai/tools/schema.h"
#include "domain/ai/rules.h"
#include "common/db.h"
#include <curl/curl.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEARCH_USER_AGENT                                                                          \
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "            \
    "Chrome/128.0.0.0 Safari/537.36"
#define DEFAULT_SEARCH_TIMEOUT_MS 8000L
#define DEFAULT_FETCH_TIMEOUT_MS 10000L

/* -------------------------------------------------------------------------- */
/* curl 内存写缓冲与重定向安全处理                                            */
/* -------------------------------------------------------------------------- */

typedef struct {
    char*  data;
    size_t size;
    size_t cap;
} curl_mem_buf_t;

static size_t
curl_write_mem_cb(void* ptr, size_t size, size_t nmemb, void* userdata)
{
    size_t          total = size * nmemb;
    curl_mem_buf_t* buf = (curl_mem_buf_t*)userdata;
    if (!buf) {
        return 0;
    }
    if (buf->size + total + 1 > buf->cap) {
        size_t new_cap = (buf->cap == 0) ? 16384 : buf->cap * 2;
        while (new_cap < buf->size + total + 1) {
            new_cap *= 2;
        }
        /* 限制单次抓取最大 4MB 内存缓冲，防止 OOM */
        if (new_cap > 4 * 1024 * 1024) {
            return 0;
        }
        char* tmp = (char*)realloc(buf->data, new_cap);
        if (!tmp) {
            return 0;
        }
        buf->data = tmp;
        buf->cap = new_cap;
    }
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

typedef struct {
    char location[4096];
} curl_header_state_t;

static size_t
curl_header_location_cb(char* buffer, size_t size, size_t nitems, void* userdata)
{
    size_t               total = size * nitems;
    curl_header_state_t* state = (curl_header_state_t*)userdata;
    if (!state || total < 10) {
        return total;
    }

    if (strncasecmp(buffer, "Location:", 9) == 0) {
        const char* p = buffer + 9;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        size_t len = total - (size_t)(p - buffer);
        while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == '\n' || p[len - 1] == ' ')) {
            len--;
        }
        if (len > 0 && len < sizeof(state->location)) {
            memcpy(state->location, p, len);
            state->location[len] = '\0';
        }
    }
    return total;
}

/**
 * @brief 带多跳 SSRF 安全校验的 HTTP GET 请求
 * @return 0 成功；-1 网络错误/超时；-2 SSRF 拦截；-3 内存错误
 */
static int
curl_http_get_with_redirect_check(const char* initial_url,
                                  long        timeout_ms,
                                  const char* accept_header,
                                  char**      out_body,
                                  size_t*     out_len,
                                  long*       out_http_code)
{
    if (out_body) {
        *out_body = NULL;
    }
    if (out_len) {
        *out_len = 0;
    }
    if (out_http_code) {
        *out_http_code = 0;
    }

    if (!initial_url || !mf_ai_rule_validate_url(initial_url, 4096)) {
        return -2; /* SSRF 拒绝 */
    }

    char current_url[4096];
    snprintf(current_url, sizeof(current_url), "%s", initial_url);

    int max_hops = 3;
    for (int hop = 0; hop <= max_hops; hop++) {
        CURL* curl = curl_easy_init();
        if (!curl) {
            return -3;
        }

        curl_mem_buf_t      body_buf = {.data = NULL, .size = 0, .cap = 0};
        curl_header_state_t hdr_state;
        memset(&hdr_state, 0, sizeof(hdr_state));

        struct curl_slist* headers = NULL;
        if (accept_header && accept_header[0]) {
            char hbuf[256];
            snprintf(hbuf, sizeof(hbuf), "Accept: %s", accept_header);
            headers = curl_slist_append(headers, hbuf);
        }
        headers = curl_slist_append(headers, "Accept-Language: zh-CN,zh;q=0.9,en;q=0.8");

        curl_easy_setopt(curl, CURLOPT_URL, current_url);
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, SEARCH_USER_AGENT);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms > 0 ? timeout_ms : 5000L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L); /* 禁用盲目跳转 */
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_mem_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body_buf);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_location_cb);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hdr_state);

        /* 自动使用环境变量代理（如 https_proxy=http://127.0.0.1:8118） */
        const char* env_proxy = getenv("https_proxy");
        if (!env_proxy) {
            env_proxy = getenv("http_proxy");
        }
        if (env_proxy && env_proxy[0]) {
            curl_easy_setopt(curl, CURLOPT_PROXY, env_proxy);
        }

        CURLcode res = curl_easy_perform(curl);
        long     http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            free(body_buf.data);
            return -1;
        }

        /* 检查 3xx 重定向 */
        if (http_code == 301 || http_code == 302 || http_code == 303 || http_code == 307 ||
            http_code == 308) {
            if (hdr_state.location[0] != '\0') {
                char target_url[4096];
                if (strncmp(hdr_state.location, "http://", 7) == 0 ||
                    strncmp(hdr_state.location, "https://", 8) == 0) {
                    snprintf(target_url, sizeof(target_url), "%s", hdr_state.location);
                } else if (hdr_state.location[0] == '/') {
                    /* 相对路径拼接 scheme + host */
                    char scheme_host[1024];
                    scheme_host[0] = '\0';
                    const char* p = strstr(current_url, "://");
                    if (p) {
                        const char* host_end = strchr(p + 3, '/');
                        size_t      len =
                            host_end ? (size_t)(host_end - current_url) : strlen(current_url);
                        if (len < sizeof(scheme_host)) {
                            memcpy(scheme_host, current_url, len);
                            scheme_host[len] = '\0';
                        }
                    }
                    snprintf(
                        target_url, sizeof(target_url), "%s%s", scheme_host, hdr_state.location);
                } else {
                    /* 不支持的重定向协议 */
                    free(body_buf.data);
                    return -2;
                }

                /* 校验重定向目标 URL 的 SSRF 安全性 */
                if (!mf_ai_rule_validate_url(target_url, 4096)) {
                    free(body_buf.data);
                    return -2; /* 阻止重定向到内网地址 */
                }

                /* Bing 重定向可能丢失 /search 路由（例如跳转到 /?q=...），在此规范化为 /search?q=... */
                const char* bq = strstr(target_url, "bing.com/?q=");
                if (bq) {
                    char fixed_url[4096];
                    size_t pre_len = (size_t)(bq - target_url) + 9; /* 包括 "bing.com/" */
                    snprintf(fixed_url, sizeof(fixed_url), "%.*ssearch?%s", (int)pre_len, target_url, bq + 11);
                    snprintf(target_url, sizeof(target_url), "%s", fixed_url);
                }

                snprintf(current_url, sizeof(current_url), "%s", target_url);
                free(body_buf.data);
                continue; /* 进入下一跳请求 */
            }
        }

        /* 正常响应或终态 */
        if (out_http_code) {
            *out_http_code = http_code;
        }
        if (out_body) {
            *out_body = body_buf.data;
        } else {
            free(body_buf.data);
        }
        if (out_len) {
            *out_len = body_buf.size;
        }
        return 0;
    }

    return -1; /* 超出重定向跳数 */
}

/* -------------------------------------------------------------------------- */
/* 文本清洗、HTML 标签剥离与实体反转义                                         */
/* -------------------------------------------------------------------------- */

static char*
html_unescape_entities(const char* src)
{
    if (!src) {
        return strdup("");
    }
    size_t len = strlen(src);
    char*  out = (char*)malloc(len + 1);
    if (!out) {
        return strdup("");
    }

    size_t i = 0;
    size_t j = 0;
    while (i < len) {
        if (src[i] == '&') {
            if (strncmp(src + i, "&amp;", 5) == 0) {
                out[j++] = '&';
                i += 5;
            } else if (strncmp(src + i, "&quot;", 6) == 0) {
                out[j++] = '"';
                i += 6;
            } else if (strncmp(src + i, "&#39;", 5) == 0 || strncmp(src + i, "&apos;", 6) == 0) {
                out[j++] = '\'';
                i += (src[i + 1] == 'a' ? 6 : 5);
            } else if (strncmp(src + i, "&lt;", 4) == 0) {
                out[j++] = '<';
                i += 4;
            } else if (strncmp(src + i, "&gt;", 4) == 0) {
                out[j++] = '>';
                i += 4;
            } else if (strncmp(src + i, "&nbsp;", 6) == 0) {
                out[j++] = ' ';
                i += 6;
            } else if (strncmp(src + i, "&#183;", 6) == 0) {
                out[j++] = '-';
                i += 6;
            } else if (strncmp(src + i, "&ensp;", 6) == 0 || strncmp(src + i, "&emsp;", 6) == 0) {
                out[j++] = ' ';
                i += 6;
            } else {
                out[j++] = src[i++];
            }
        } else {
            out[j++] = src[i++];
        }
    }
    out[j] = '\0';
    return out;
}

static char*
html_strip_tags(const char* html)
{
    if (!html) {
        return strdup("");
    }
    size_t len = strlen(html);
    char*  out = (char*)malloc(len + 1);
    if (!out) {
        return strdup("");
    }

    size_t i = 0;
    size_t j = 0;
    bool   in_tag = false;

    while (i < len) {
        /* 跳过 <script>...</script> 和 <style>...</style> */
        if (!in_tag && strncasecmp(html + i, "<script", 7) == 0) {
            const char* end = strstr(html + i, "</script>");
            if (end) {
                i = (size_t)(end - html) + 9;
                continue;
            }
        }
        if (!in_tag && strncasecmp(html + i, "<style", 6) == 0) {
            const char* end = strstr(html + i, "</style>");
            if (end) {
                i = (size_t)(end - html) + 8;
                continue;
            }
        }

        if (html[i] == '<') {
            in_tag = true;
            i++;
        } else if (html[i] == '>') {
            in_tag = false;
            i++;
            /* 标签结束后若前一个字符不是空格或换行，补一个空格 */
            if (j > 0 && out[j - 1] != ' ' && out[j - 1] != '\n') {
                out[j++] = ' ';
            }
        } else if (!in_tag) {
            char c = html[i++];
            if (c == '\r' || c == '\t') {
                c = ' ';
            }
            if (c == ' ' && j > 0 && out[j - 1] == ' ') {
                continue; /* 压缩多余空格 */
            }
            if (c == '\n' && j > 1 && out[j - 1] == '\n' && out[j - 2] == '\n') {
                continue; /* 压缩连续空行 */
            }
            out[j++] = c;
        } else {
            i++;
        }
    }
    out[j] = '\0';

    char* unescaped = html_unescape_entities(out);
    free(out);
    return unescaped;
}

/* -------------------------------------------------------------------------- */
/* Bing 点击跟踪 URL base64 解码还原                                          */
/* -------------------------------------------------------------------------- */

static int
base64_val(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+' || c == '-') {
        return 62;
    }
    if (c == '/' || c == '_') {
        return 63;
    }
    return -1;
}

/**
 * @brief 解码 Bing /ck/a?u=a1<base64> 形式的真实目标 URL
 */
char*
mf_bing_unpack_url(const char* href)
{
    if (!href || !href[0]) {
        return strdup("");
    }
    const char* p = strstr(href, "u=a1");
    if (!p) {
        p = strstr(href, "/ck/a?");
    }
    if (!p) {
        return strdup(href);
    }
    const char* u_start = strstr(href, "u=a1");
    if (!u_start) {
        return strdup(href);
    }
    u_start += 4; /* 跳过 u=a1 */

    const char* u_end = u_start;
    while (*u_end && *u_end != '&' && *u_end != '#') {
        u_end++;
    }
    size_t in_len = (size_t)(u_end - u_start);
    if (in_len == 0 || in_len > 4096) {
        return strdup(href);
    }

    char* decoded = (char*)malloc(in_len + 1);
    if (!decoded) {
        return strdup(href);
    }

    size_t di = 0;
    int    buf = 0;
    int    bits = 0;
    for (size_t i = 0; i < in_len; i++) {
        char c = u_start[i];
        if (c == '=') {
            break;
        }
        int val = base64_val(c);
        if (val < 0) {
            continue;
        }
        buf = (buf << 6) | val;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            decoded[di++] = (char)((buf >> bits) & 0xFF);
        }
    }
    decoded[di] = '\0';

    if (strncmp(decoded, "http://", 7) == 0 || strncmp(decoded, "https://", 8) == 0) {
        return decoded;
    }

    free(decoded);
    return strdup(href);
}

/* -------------------------------------------------------------------------- */
/* Bing 搜索结果 HTML 解析器                                                   */
/* -------------------------------------------------------------------------- */

csilk_json_t*
mf_bing_html_parse(const char* html, size_t max_results)
{
    csilk_json_t* list = csilk_json_array();
    if (!html || !html[0]) {
        return list;
    }

    const char* cur = html;
    size_t      count = 0;

    while (count < max_results) {
        const char* li = strstr(cur, "<li class=\"b_algo\"");
        if (!li) {
            break;
        }
        const char* next_li = strstr(li + 18, "<li class=\"b_algo\"");
        cur = next_li ? next_li : (li + strlen(li));

        /* 提取 <h2><a ... href="URL">Title</a></h2> */
        const char* h2 = strstr(li, "<h2");
        if (!h2 || (next_li && h2 > next_li)) {
            continue;
        }
        const char* a = strstr(h2, "<a ");
        if (!a || (next_li && a > next_li)) {
            continue;
        }
        const char* href_attr = strstr(a, "href=\"");
        if (!href_attr || (next_li && href_attr > next_li)) {
            continue;
        }
        href_attr += 6;
        const char* href_end = strchr(href_attr, '"');
        if (!href_end) {
            continue;
        }

        char   raw_href[2048];
        size_t hlen = (size_t)(href_end - href_attr);
        if (hlen >= sizeof(raw_href)) {
            hlen = sizeof(raw_href) - 1;
        }
        memcpy(raw_href, href_attr, hlen);
        raw_href[hlen] = '\0';

        char* canonical_url = mf_bing_unpack_url(raw_href);

        /* 提取标题 */
        const char* a_content_start = strchr(a, '>');
        if (!a_content_start) {
            free(canonical_url);
            continue;
        }
        a_content_start++;
        const char* a_content_end = strstr(a_content_start, "</a>");
        if (!a_content_end) {
            free(canonical_url);
            continue;
        }

        char   raw_title[1024];
        size_t tlen = (size_t)(a_content_end - a_content_start);
        if (tlen >= sizeof(raw_title)) {
            tlen = sizeof(raw_title) - 1;
        }
        memcpy(raw_title, a_content_start, tlen);
        raw_title[tlen] = '\0';

        char* clean_title = html_strip_tags(raw_title);

        /* 提取摘要：匹配 <div class="b_caption"><p ...> 或 <p class="b_lineclamp */
        char raw_snippet[2048];
        raw_snippet[0] = '\0';
        const char* cap = strstr(h2, "class=\"b_caption\"");
        if (!cap || (next_li && cap > next_li)) {
            cap = strstr(h2, "class=\"b_lineclamp");
        }
        if (cap && (!next_li || cap < next_li)) {
            const char* p_start = strchr(cap, '>');
            if (p_start) {
                p_start++;
                /* 若有内层 <p>，寻内层 */
                const char* inner_p = strstr(cap, "<p");
                if (inner_p && (!next_li || inner_p < next_li)) {
                    const char* inner_p_start = strchr(inner_p, '>');
                    if (inner_p_start) {
                        p_start = inner_p_start + 1;
                    }
                }
                const char* p_end = strstr(p_start, "</p>");
                if (p_end && (!next_li || p_end < next_li)) {
                    size_t slen = (size_t)(p_end - p_start);
                    if (slen >= sizeof(raw_snippet)) {
                        slen = sizeof(raw_snippet) - 1;
                    }
                    memcpy(raw_snippet, p_start, slen);
                    raw_snippet[slen] = '\0';
                }
            }
        }

        char* clean_snippet = html_strip_tags(raw_snippet);

        /* 过滤无效项 */
        if (clean_title[0] && canonical_url[0]) {
            csilk_json_t* item = csilk_json_object();
            csilk_json_add_string(item, "title", clean_title);
            csilk_json_add_string(item, "url", canonical_url);
            csilk_json_add_string(item, "snippet", clean_snippet);
            csilk_json_array_append(list, item);
            count++;
        }

        free(canonical_url);
        free(clean_title);
        free(clean_snippet);
    }

    return list;
}

/* -------------------------------------------------------------------------- */
/* Wikipedia OpenSearch 兜底                                                 */
/* -------------------------------------------------------------------------- */

static csilk_json_t*
wikipedia_opensearch(const char* query, size_t max_results)
{
    csilk_json_t* list = csilk_json_array();
    if (!query || !query[0]) {
        return list;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        return list;
    }
    char* encoded_q = curl_easy_escape(curl, query, (int)strlen(query));
    curl_easy_cleanup(curl);
    if (!encoded_q) {
        return list;
    }

    char url[1024];
    snprintf(url,
             sizeof(url),
             "https://zh.wikipedia.org/w/api.php?action=opensearch&search=%s&limit=%zu&namespace="
             "0&format=json",
             encoded_q,
             max_results);
    curl_free(encoded_q);

    char*  body = NULL;
    size_t body_len = 0;
    long   http_code = 0;
    int    rc = curl_http_get_with_redirect_check(
        url, DEFAULT_SEARCH_TIMEOUT_MS, "application/json", &body, &body_len, &http_code);
    if (rc != 0 || !body || http_code != 200) {
        free(body);
        return list;
    }

    csilk_json_t* root = csilk_json_parse(body);
    free(body);
    if (!root || !csilk_json_is_array(root)) {
        if (root) {
            csilk_json_free(root);
        }
        return list;
    }

    /* 格式：[query, [titles], [snippets], [urls]] */
    if (csilk_json_array_size(root) >= 4) {
        const csilk_json_t* titles = csilk_json_array_get(root, 1);
        const csilk_json_t* snippets = csilk_json_array_get(root, 2);
        const csilk_json_t* urls = csilk_json_array_get(root, 3);

        if (titles && urls) {
            size_t n = csilk_json_array_size(titles);
            for (size_t i = 0; i < n && i < max_results; i++) {
                const char* t = csilk_json_get_string(csilk_json_array_get(titles, i), NULL);
                const char* s =
                    snippets ? csilk_json_get_string(csilk_json_array_get(snippets, i), NULL) : "";
                const char* u = csilk_json_get_string(csilk_json_array_get(urls, i), NULL);
                if (t && u) {
                    csilk_json_t* item = csilk_json_object();
                    csilk_json_add_string(item, "title", t);
                    csilk_json_add_string(item, "url", u);
                    csilk_json_add_string(item, "snippet", s ? s : "");
                    csilk_json_array_append(list, item);
                }
            }
        }
    }

    csilk_json_free(root);
    return list;
}

/* -------------------------------------------------------------------------- */
/* 工具执行入口：web_search                                                    */
/* -------------------------------------------------------------------------- */

static char*
exec_web_search(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    (void)ctx;
    if (!args) {
        return strdup("{\"error\":\"invalid_parameters\",\"message\":\"missing query\"}");
    }

    const char* query = csilk_json_get_string(args, "query");
    if (!query || !query[0]) {
        return strdup("{\"error\":\"invalid_parameters\",\"message\":\"query must be non-empty\"}");
    }

    size_t num_results = 5;
    double nr = db_get_num(args, "num_results");
    if (nr >= 1.0 && nr <= 10.0) {
        num_results = (size_t)nr;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        return strdup("{\"error\":\"internal_error\",\"message\":\"curl init failed\"}");
    }
    char* encoded_q = curl_easy_escape(curl, query, (int)strlen(query));
    curl_easy_cleanup(curl);
    if (!encoded_q) {
        return strdup("{\"error\":\"internal_error\",\"message\":\"query encoding failed\"}");
    }

    char search_url[2048];
    snprintf(
        search_url, sizeof(search_url), "https://www.bing.com/search?q=%s", encoded_q);
    curl_free(encoded_q);

    char*  body = NULL;
    size_t body_len = 0;
    long   http_code = 0;
    int    rc = curl_http_get_with_redirect_check(search_url,
                                                  DEFAULT_SEARCH_TIMEOUT_MS,
                                                  "text/html,application/xhtml+xml",
                                                  &body,
                                                  &body_len,
                                                  &http_code);

    csilk_json_t* results_arr = NULL;
    if (rc == 0 && body && http_code == 200) {
        results_arr = mf_bing_html_parse(body, num_results);
    }
    free(body);

    /* 若 Bing 未获取到结果，执行 Wikipedia OpenSearch 兜底 */
    if (!results_arr || csilk_json_array_size(results_arr) == 0) {
        if (results_arr) {
            csilk_json_free(results_arr);
        }
        results_arr = wikipedia_opensearch(query, num_results);
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "query", query);
    size_t count = results_arr ? csilk_json_array_size(results_arr) : 0;
    csilk_json_add_number(res, "count", (double)count);
    if (results_arr) {
        csilk_json_add_array(res, "results", results_arr);
    } else {
        csilk_json_add_array(res, "results", csilk_json_array());
    }

    size_t out_len = 0;
    char*  serialized = csilk_json_serialize(res, &out_len);
    csilk_json_free(res);
    return serialized;
}

/* -------------------------------------------------------------------------- */
/* 工具执行入口：fetch_webpage                                                 */
/* -------------------------------------------------------------------------- */

static char*
exec_fetch_webpage(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    (void)ctx;
    if (!args) {
        return strdup("{\"error\":\"invalid_parameters\",\"message\":\"missing url\"}");
    }

    const char* url = csilk_json_get_string(args, "url");
    if (!url || !url[0]) {
        return strdup("{\"error\":\"invalid_parameters\",\"message\":\"url must be non-empty\"}");
    }

    /* 1. SSRF 拦截检查 */
    if (!mf_ai_rule_validate_url(url, 4096)) {
        return strdup("{\"error\":\"forbidden_url\",\"message\":\"Access to local or private "
                      "network addresses is prohibited for security.\"}");
    }

    size_t max_chars = 4000;
    double mc = db_get_num(args, "max_chars");
    if (mc >= 500.0 && mc <= 10000.0) {
        max_chars = (size_t)mc;
    }

    char* content_text = NULL;
    char  extracted_title[512];
    extracted_title[0] = '\0';

    /* 2. 主通道：通过 Jina Reader Proxy (https://r.jina.ai/<url>) 提取结构化 Markdown */
    char jina_url[4096 + 32];
    snprintf(jina_url, sizeof(jina_url), "https://r.jina.ai/%s", url);

    char*  jina_body = NULL;
    size_t jina_len = 0;
    long   jina_code = 0;
    int    rc = curl_http_get_with_redirect_check(
        jina_url, DEFAULT_FETCH_TIMEOUT_MS, "text/plain", &jina_body, &jina_len, &jina_code);

    if (rc == 0 && jina_body && jina_code == 200 && jina_len > 0) {
        /* 解析 Jina 返回的前置 Title 标头（形如 "Title: ...\n"） */
        if (strncmp(jina_body, "Title: ", 7) == 0) {
            const char* nl = strchr(jina_body, '\n');
            if (nl) {
                size_t tlen = (size_t)(nl - (jina_body + 7));
                if (tlen >= sizeof(extracted_title)) {
                    tlen = sizeof(extracted_title) - 1;
                }
                memcpy(extracted_title, jina_body + 7, tlen);
                extracted_title[tlen] = '\0';
            }
        }
        content_text = jina_body; /* 移交所有权 */
    } else {
        free(jina_body);
        /* 3. 兜底通道：直接 HTTP GET 抓取目标 URL + 本地 HTML 清洗流水线 */
        char*  direct_body = NULL;
        size_t direct_len = 0;
        long   direct_code = 0;
        int    direct_rc = curl_http_get_with_redirect_check(url,
                                                             DEFAULT_FETCH_TIMEOUT_MS,
                                                             "text/html,*/*",
                                                             &direct_body,
                                                             &direct_len,
                                                             &direct_code);

        if (direct_rc == -2) {
            return strdup("{\"error\":\"forbidden_redirect_url\",\"message\":\"Redirect to "
                          "internal subnet was blocked.\"}");
        }
        if (direct_rc != 0 || !direct_body || direct_code >= 400) {
            free(direct_body);
            csilk_json_t* err = csilk_json_object();
            csilk_json_add_string(err, "error", "fetch_failed");
            csilk_json_add_number(err, "status_code", (double)direct_code);
            csilk_json_add_string(err, "url", url);
            size_t elen = 0;
            char*  estr = csilk_json_serialize(err, &elen);
            csilk_json_free(err);
            return estr;
        }

        /* 提取 <title>...</title> */
        const char* t_start = strstr(direct_body, "<title");
        if (t_start) {
            const char* inner = strchr(t_start, '>');
            if (inner) {
                inner++;
                const char* t_end = strstr(inner, "</title>");
                if (t_end) {
                    size_t tlen = (size_t)(t_end - inner);
                    if (tlen >= sizeof(extracted_title)) {
                        tlen = sizeof(extracted_title) - 1;
                    }
                    memcpy(extracted_title, inner, tlen);
                    extracted_title[tlen] = '\0';
                }
            }
        }

        content_text = html_strip_tags(direct_body);
        free(direct_body);
    }

    if (!content_text) {
        content_text = strdup("");
    }

    /* 4. Token 预算安全截断 */
    bool   truncated = false;
    size_t clen = strlen(content_text);
    if (clen > max_chars) {
        content_text[max_chars] = '\0';
        truncated = true;
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "url", url);
    csilk_json_add_string(res, "title", extracted_title);
    csilk_json_add_string(res, "content", content_text);
    csilk_json_add_bool(res, "truncated", truncated);

    free(content_text);
    size_t out_len = 0;
    char*  serialized = csilk_json_serialize(res, &out_len);
    csilk_json_free(res);
    return serialized;
}

/* -------------------------------------------------------------------------- */
/* 工具注册                                                                   */
/* -------------------------------------------------------------------------- */

void
ai_tool_web_search_register_all(void)
{
    /* 1. web_search */
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(s,
                           "query",
                           "string",
                           "搜索关键词或短语，例如：'美联储 利率决议 2026' 或 '英伟达 财报 营收'");
        ai_schema_add_prop(s, "num_results", "integer", "期望返回的结果条数，默认 5，范围 1-10");
        ai_schema_add_required(s, "query");

        ai_tool_t t = {
            .name = "web_search",
            .description = "搜索互联网获取最新的财经市场资讯、宏观经济数据及通用知识。返回相关网页"
                           "的标题、链接和摘要。",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_web_search,
        };
        ai_tool_register(&t);
    }

    /* 2. fetch_webpage */
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(
            s, "url", "string", "要深入阅读的目标网页完整 URL（必须以 http:// 或 https:// 开头）");
        ai_schema_add_prop(s,
                           "max_chars",
                           "integer",
                           "提取的最大正文字符数，默认 4000，上限 10000，防止上下文溢出");
        ai_schema_add_required(s, "url");

        ai_tool_t t = {
            .name = "fetch_webpage",
            .description = "抓取并深入阅读指定网页的正文详情，提取清洗后的纯文本/"
                           "Markdown内容。当搜索结果摘要不足以回答用户问题时使用。",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_fetch_webpage,
        };
        ai_tool_register(&t);
    }
}
