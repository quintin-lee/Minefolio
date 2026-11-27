#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/**
 * @file mcp_client.c
 * @brief MCP 远端工具源客户端实现 — M2
 *
 * 帧收发复用 csilk_mcp_msg_*，传输层：
 *  - HTTP：libcurl 短连接（请求/响应各一次 curl_easy），Mcp-Session-Id 跨调用透传
 *  - STDIO：fork+execv 拉起子进程，stdin/stdout 换行分帧；超时则 SIGTERM→SIGKILL
 */

#include "services/ai/tools/mcp/mcp_client.h"
#include "services/ai/tools/mcp/mcp_config.h"
#include "domain/mcp/rules.h"
#include "common/http_safety.h"
#include "config/secret.h"
#include "csilk/core/server/server.h"

#include <curl/curl.h>
#include <openssl/crypto.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>

extern char** environ;

/* 远端工具单次 schema 最大长度（与 entity input_schema[16384] 对齐） */
#define MF_MCP_TOOL_SCHEMA_MAX 16384

/* -------------------------------------------------------------------------- */
/* libcurl 响应头回调                                                          */
/* -------------------------------------------------------------------------- */

static size_t
curl_header_cb(char* buffer, size_t size, size_t nitems, void* userdata)
{
    mf_mcp_client_t* c = (mf_mcp_client_t*)userdata;
    size_t           total = size * nitems;
    if (!c || total < 16) {
        return total;
    }
    const char* prefix = "mcp-session-id:";
    size_t      plen = strlen(prefix);
    if (strncasecmp(buffer, prefix, plen) == 0) {
        const char* val = buffer + plen;
        while (*val == ' ' || *val == '\t') {
            val++;
        }
        char   sid[256] = {0};
        size_t idx = 0;
        while (*val && *val != '\r' && *val != '\n' && idx < sizeof(sid) - 1) {
            sid[idx++] = *val++;
        }
        sid[idx] = '\0';
        if (sid[0]) {
            free(c->session_id);
            c->session_id = strdup(sid);
        }
    }
    return total;
}

/* -------------------------------------------------------------------------- */
/* JSON-RPC 帧构造                                                            */
/* -------------------------------------------------------------------------- */

/* 构造 JSON-RPC 2.0 请求帧 {jsonrpc, id, method, params}，返回堆分配 JSON 串 */
static char*
build_request(const char* method, const csilk_json_t* params)
{
    csilk_json_t* frame = csilk_json_object();
    if (!frame) {
        return NULL;
    }
    csilk_json_add_string(frame, "jsonrpc", "2.0");
    csilk_json_add_int(frame, "id", 1);
    csilk_json_add_string(frame, "method", method ? method : "");
    csilk_json_t* params_copy = params ? csilk_json_copy(params) : csilk_json_object();
    csilk_json_add_object(frame, "params", params_copy); /* add_object 接管所有权 */
    char* out = csilk_json_serialize(frame, NULL);
    csilk_json_free(frame);
    return out;
}

/* 构造 JSON-RPC 2.0 通知帧（无 id） */
static char*
build_notification(const char* method, const csilk_json_t* params)
{
    csilk_json_t* frame = csilk_json_object();
    if (!frame) {
        return NULL;
    }
    csilk_json_add_string(frame, "jsonrpc", "2.0");
    csilk_json_add_string(frame, "method", method ? method : "");
    csilk_json_t* params_copy = params ? csilk_json_copy(params) : csilk_json_object();
    csilk_json_add_object(frame, "params", params_copy);
    char* out = csilk_json_serialize(frame, NULL);
    csilk_json_free(frame);
    return out;
}

/* -------------------------------------------------------------------------- */
/* HTTP 传输：发送一帧并取回响应帧                                              */
/* -------------------------------------------------------------------------- */

/* 解析 headers_json（形如 {"k":"v",...}）追加到 curl slist */
static void
append_header_json(const char* headers_json, struct curl_slist** headers)
{
    if (!headers_json || !headers_json[0]) {
        return;
    }
    csilk_json_t* obj = csilk_json_parse(headers_json);
    if (!obj || !csilk_json_is_object(obj)) {
        if (obj) {
            csilk_json_free(obj);
        }
        return;
    }
    /* 遍历 object 键值对 */
    size_t n = csilk_json_object_size(obj);
    for (size_t i = 0; i < n; i++) {
        const char*         key = csilk_json_object_key(obj, i);
        const csilk_json_t* val = csilk_json_object_val(obj, i);
        if (val && csilk_json_is_string(val)) {
            const char* v = csilk_json_string_value(val);
            if (!key || !key[0] || strpbrk(key, "\r\n:") != NULL || !v ||
                strpbrk(v, "\r\n") != NULL) {
                continue;
            }
            char hdr[600];
            snprintf(hdr, sizeof(hdr), "%s: %s", key, v);
            *headers = curl_slist_append(*headers, hdr);
        }
    }
    csilk_json_free(obj);
}

/* 发送 JSON-RPC 请求到 MCP HTTP 端点，返回响应体（堆分配） */
static char*
http_call(mf_mcp_client_t*    c,
          const char*         request_json,
          long                timeout_ms,
          struct curl_slist** out_headers,
          char*               out_err,
          size_t              err_sz,
          int*                out_http_code)
{
    const mf_mcp_server_t* s = c->server;
    char                   resolve_entry[320] = {0};
    if (!s || mf_http_validate_and_resolve_url(s->url,
                                               512,
                                               mf_mcp_config_allow_local(),
                                               false,
                                               resolve_entry,
                                               sizeof(resolve_entry)) != 0) {
        snprintf(out_err, err_sz, "blocked MCP server URL by SSRF policy");
        return NULL;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        snprintf(out_err, err_sz, "curl init failed");
        return NULL;
    }

    mf_curl_buf_t chunk = {.data = malloc(1), .size = 0, .cap = 1, .max_bytes = MF_CURL_MAX_8MB};
    if (chunk.data) {
        chunk.data[0] = '\0';
    }

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json, text/event-stream");
    if (c->session_id && c->session_id[0] && !strpbrk(c->session_id, "\r\n")) {
        char hdr[320];
        snprintf(hdr, sizeof(hdr), "Mcp-Session-Id: %s", c->session_id);
        headers = curl_slist_append(headers, hdr);
    }
    /* 用户自定义 headers */
    append_header_json(s->headers_json, &headers);
    /* 凭证：secret_ref 非空时附 Authorization（白名单复核 + CRLF 拦截 + 内存擦除） */
    if (s->secret_ref[0] && mf_mcp_secret_ref_valid(s->secret_ref)) {
        char        tok[512];
        const char* t = config_env_get(s->secret_ref, tok, sizeof(tok), "");
        if (t && t[0] && !strpbrk(t, "\r\n")) {
            char hdr[600];
            snprintf(hdr, sizeof(hdr), "Authorization: Bearer %s", t);
            headers = curl_slist_append(headers, hdr);
            OPENSSL_cleanse(hdr, sizeof(hdr));
        }
        OPENSSL_cleanse(tok, sizeof(tok));
    }

    struct curl_slist* resolve_list = mf_http_curl_apply_security(curl, resolve_entry, NULL, false);
    curl_easy_setopt(curl, CURLOPT_URL, s->url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mf_curl_bounded_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void*)c);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 4000L);

    CURLcode res = curl_easy_perform(curl);
    if (resolve_list) {
        curl_slist_free_all(resolve_list);
    }
    if (out_http_code) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        *out_http_code = (int)code;
    }

    if (out_headers) {
        *out_headers = headers;
    } else {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        snprintf(out_err, err_sz, "http transport error: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        free(chunk.data);
        return NULL;
    }
    curl_easy_cleanup(curl);

    /* 处理 SSE 数据帧（"data: {...}" 行）：MCP streamable-HTTP 可能返回 text/event-stream */
    csilk_json_t* parsed = csilk_json_parse(chunk.data);
    if (!parsed) {
        /* 尝试抽取 SSE data 行 */
        char*         sse = strstr(chunk.data, "data:");
        csilk_json_t* sse_json = NULL;
        if (sse) {
            sse += 5;
            while (*sse == ' ' || *sse == '\t') {
                sse++;
            }
            /* 取到行尾 */
            char* eol = strchr(sse, '\n');
            if (eol) {
                size_t n = (size_t)(eol - sse);
                char*  tmp = malloc(n + 1);
                if (tmp) {
                    memcpy(tmp, sse, n);
                    tmp[n] = '\0';
                    sse_json = csilk_json_parse(tmp);
                    free(tmp);
                }
            }
        }
        if (sse_json) {
            free(chunk.data);
            char* out = csilk_json_serialize(sse_json, NULL);
            csilk_json_free(sse_json);
            return out;
        }
        free(chunk.data);
        snprintf(out_err, err_sz, "http response not valid JSON");
        return NULL;
    }
    free(chunk.data);
    char* out = csilk_json_serialize(parsed, NULL);
    csilk_json_free(parsed);
    return out;
}

/* -------------------------------------------------------------------------- */
/* STDIO 传输：拉起子进程 + 帧收发                                              */
/* -------------------------------------------------------------------------- */

/* PATH 查找：把裸命令名（不含 '/'）解析为可执行绝对路径。成功 0 / 未找到 1 */
static int
resolve_exec_on_path(const char* prog, char* out, size_t out_sz)
{
    const char* path = getenv("PATH");
    if (!path) {
        path = "/usr/local/bin:/usr/bin:/bin";
    }
    char path_copy[1024];
    snprintf(path_copy, sizeof(path_copy), "%s", path);
    char* saveptr = NULL;
    for (char* tok = strtok_r(path_copy, ":", &saveptr); tok; tok = strtok_r(NULL, ":", &saveptr)) {
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", tok, prog);
        if (access(full, X_OK) == 0) {
            if (strlen(full) >= out_sz) {
                continue;
            }
            snprintf(out, out_sz, "%s", full);
            return 0;
        }
    }
    return 1;
}

/* 解析 command + args_json → argv 数组（NULL 终止） */
static char**
build_argv(const mf_mcp_server_t* s, size_t* out_argc)
{
    /* command 形如 "node /path/script.js"（含空格、无 shell 元字符，rules 已校验）。
     * 按空白 token 化：argv[0]=可执行文件，余为参数；再追加 args_json。
     * execve 不做 PATH 查找，故 argv[0] 须解析为绝对路径（resolve_exec）。 */
    char* stack_argv[64];
    int   argc = 0;
    char  cmd_copy[512];
    snprintf(cmd_copy, sizeof(cmd_copy), "%s", s->command);

    bool  truncated = false;
    char* saveptr = NULL;
    char* tok = strtok_r(cmd_copy, " \t", &saveptr);
    while (tok) {
        if (argc >= 40) {
            truncated = true;
            break;
        }
        stack_argv[argc++] = strdup(tok);
        if (!stack_argv[argc - 1]) {
            for (int k = 0; k < argc; k++) {
                free(stack_argv[k]);
            }
            return NULL;
        }
        tok = strtok_r(NULL, " \t", &saveptr);
    }
    if (argc == 0) {
        return NULL;
    }

    if (s->args_json[0]) {
        csilk_json_t* arr = csilk_json_parse(s->args_json);
        if (arr && csilk_json_is_array(arr)) {
            size_t n = csilk_json_array_size(arr);
            for (size_t i = 0; i < n; i++) {
                if (argc >= 40) {
                    truncated = true;
                    break;
                }
                const csilk_json_t* item = csilk_json_array_get(arr, i);
                if (item && csilk_json_is_string(item)) {
                    stack_argv[argc++] = strdup(csilk_json_string_value(item));
                    if (!stack_argv[argc - 1]) {
                        for (int k = 0; k < argc; k++) {
                            free(stack_argv[k]);
                        }
                        csilk_json_free(arr);
                        return NULL;
                    }
                }
            }
        }
        if (arr) {
            csilk_json_free(arr);
        }
    }
    stack_argv[argc] = NULL;

    if (truncated) {
        CSILK_LOG_W("MCP server '%s' command/args truncated to 40 arguments limit",
                    s->name[0] ? s->name : "unknown");
    }

    /* execve 对 argv[0] 不做 PATH 查找，须解析为绝对路径，否则 ENOENT。
       仅当 argv[0] 不含 '/'（裸命令名）时走 PATH 解析。 */
    if (argc > 0 && strchr(stack_argv[0], '/') == NULL) {
        char resolved[512];
        if (resolve_exec_on_path(stack_argv[0], resolved, sizeof(resolved)) == 0) {
            free(stack_argv[0]);
            stack_argv[0] = strdup(resolved);
            if (!stack_argv[0]) {
                for (int k = 0; k < argc; k++) {
                    free(stack_argv[k]);
                }
                return NULL;
            }
        }
    }

    /* 脚本文件（.sh/.js/.py）直接 execve 走 kernel shebang，stdin dup2 后 bash
       内部 read 会报 "Bad file descriptor" 读不到响应；须显式以解释器前缀拉起。
       命中后把解释器插入 argv[0]，原 argv[0] 顺移到 argv[1]（仅 STDIO transport
       会带脚本，HTTP transport 不使用 argv 数组，故此检测无副作用）。 */
    if (argc > 0) {
        const char* exec0 = stack_argv[0];
        size_t      ln = strlen(exec0);
        const char* interp = NULL;
        if (ln >= 3 && strcmp(exec0 + ln - 3, ".sh") == 0) {
            interp = "bash";
        } else if (ln >= 3 && strcmp(exec0 + ln - 3, ".js") == 0) {
            interp = "node";
        } else if (ln >= 3 && strcmp(exec0 + ln - 3, ".py") == 0) {
            interp = "python3";
        }
        if (interp) {
            /* execve 不做 PATH 查找，解释器须解析为绝对路径，否则 ENOENT。 */
            char resolved[512];
            if (strchr(interp, '/') == NULL &&
                resolve_exec_on_path(interp, resolved, sizeof(resolved)) == 0) {
                interp = resolved;
            }
            char** shifted = malloc((argc + 2) * sizeof(char*));
            if (shifted) {
                shifted[0] = strdup(interp);
                for (int k = 0; k < argc; k++) {
                    shifted[k + 1] = stack_argv[k];
                }
                shifted[argc + 1] = NULL;
                /* 解释器须为 argv[0]（execve 执行目标），原 argv 顺移到 1..argc。
                   shifted 已排好 [interp, 原0, ...]，整体回填 stack_argv；
                   堆串所有权随之转移到 result 数组，由尾部统一 free。 */
                for (int k = 0; k < argc + 2; k++) {
                    stack_argv[k] = shifted[k];
                }
                argc++;
                stack_argv[argc] = NULL;
                free(shifted);
            }
        }
    }

    char** result = malloc((argc + 1) * sizeof(char*));
    if (!result) {
        for (int i = 0; i < argc; i++) {
            free(stack_argv[i]);
        }
        return NULL;
    }
    for (int i = 0; i <= argc; i++) {
        result[i] = stack_argv[i];
    }
    *out_argc = argc;
    return result;
}

/* 检查环境变量名是否包含敏感关键词（JWT、SECRET、KEY、PASSWORD、DSN、MINEFOLIO 等） */
static bool
is_sensitive_env(const char* entry)
{
    if (!entry) {
        return true;
    }
    const char* eq = strchr(entry, '=');
    size_t      key_len = eq ? (size_t)(eq - entry) : strlen(entry);
    char        key[128];
    if (key_len >= sizeof(key)) {
        key_len = sizeof(key) - 1;
    }
    for (size_t i = 0; i < key_len; i++) {
        char c = entry[i];
        if (c >= 'a' && c <= 'z') {
            c -= 32;
        }
        key[i] = c;
    }
    key[key_len] = '\0';

    if (strstr(key, "SECRET") || strstr(key, "KEY") || strstr(key, "PASSWORD") ||
        strstr(key, "PASSWD") || strstr(key, "TOKEN") || strstr(key, "DSN") ||
        strstr(key, "MINEFOLIO") || strstr(key, "DATABASE") || strstr(key, "AUTH") ||
        strstr(key, "CREDENTIAL")) {
        return true;
    }
    return false;
}

/* 黑名单拦截 env_json 中的高危注入环境变量（LD_* / PATH / DYLD_* / 解释器预加载选项 /
 * 含控制字符或敏感键），防止通过数据库 env_json 配置对子进程实施动态库劫持或命令注入。 */
static bool
is_blocked_custom_env_key(const char* key)
{
    if (!key || !key[0]) {
        return true;
    }
    char   upper[128];
    size_t len = strlen(key);
    if (len >= sizeof(upper)) {
        return true;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)key[i];
        if (ch == '=' || ch == '\r' || ch == '\n' || ch < 0x20 || ch == 0x7f) {
            return true;
        }
        if (ch >= 'a' && ch <= 'z') {
            ch = (unsigned char)(ch - 32);
        }
        upper[i] = (char)ch;
    }
    upper[len] = '\0';

    if (strncmp(upper, "LD_", 3) == 0 || strncmp(upper, "DYLD_", 5) == 0) {
        return true;
    }
    if (strcmp(upper, "PATH") == 0 || strcmp(upper, "IFS") == 0 || strcmp(upper, "BASH_ENV") == 0 ||
        strcmp(upper, "ENV") == 0 || strcmp(upper, "CDPATH") == 0 || strcmp(upper, "SHELL") == 0 ||
        strcmp(upper, "SHELLOPTS") == 0 || strcmp(upper, "BASHOPTS") == 0 ||
        strcmp(upper, "PYTHONHOME") == 0 || strcmp(upper, "PYTHONPATH") == 0 ||
        strcmp(upper, "NODE_OPTIONS") == 0 || strcmp(upper, "PERL5OPT") == 0 ||
        strcmp(upper, "RUBYOPT") == 0 || strcmp(upper, "OPENSSL_CONF") == 0) {
        return true;
    }
    return is_sensitive_env(upper);
}

bool
mf_mcp_env_key_is_blocked(const char* key)
{
    return is_blocked_custom_env_key(key);
}

/* 构造受限脱敏 envp（过滤敏感键，env_json 非空时追加经白名单/黑名单校验的自定义 KEY=VALUE） */
static char**
build_envp(const mf_mcp_server_t* s, size_t* out_envc, size_t* out_basec)
{
    size_t env_total = 0;
    for (char** p = environ; *p; p++) {
        env_total++;
    }

    char** envp = malloc((env_total + 64 + 1) * sizeof(char*));
    if (!envp) {
        *out_envc = 0;
        if (out_basec) {
            *out_basec = 0;
        }
        return NULL;
    }

    size_t idx = 0;
    bool   has_npm_cache = false;
    for (char** p = environ; *p; p++) {
        if (!is_sensitive_env(*p)) {
            if (strncmp(*p, "npm_config_cache=", 17) == 0 ||
                strncmp(*p, "NPM_CONFIG_CACHE=", 17) == 0) {
                has_npm_cache = true;
            }
            envp[idx++] = *p;
        }
    }
    if (!has_npm_cache) {
        /* 自动注入安全可写的 npm 缓存目录，防止容器内无 /home 导致 npx 报 EACCES */
        if (access("/app/data", W_OK) == 0) {
            envp[idx++] = "npm_config_cache=/app/data/.npm";
        } else {
            envp[idx++] = "npm_config_cache=/tmp/.npm";
        }
    }
    size_t base_c = idx;
    if (out_basec) {
        *out_basec = base_c;
    }

    if (s && s->env_json[0]) {
        csilk_json_t* obj = csilk_json_parse(s->env_json);
        if (obj && csilk_json_is_object(obj)) {
            size_t obj_n = csilk_json_object_size(obj);
            for (size_t i = 0; i < obj_n && idx < env_total + 64; i++) {
                const char*         key = csilk_json_object_key(obj, i);
                const csilk_json_t* val = csilk_json_object_val(obj, i);
                if (key && val && csilk_json_is_string(val)) {
                    const char* val_str = csilk_json_string_value(val);
                    if (is_blocked_custom_env_key(key)) {
                        CSILK_LOG_W("mcp_client: blocked dangerous env_json key '%s'", key);
                        continue;
                    }
                    if (!val_str || strpbrk(val_str, "\r\n") != NULL) {
                        CSILK_LOG_W(
                            "mcp_client: blocked env_json key '%s' due to control characters", key);
                        continue;
                    }
                    char buf[512];
                    snprintf(buf, sizeof(buf), "%s=%s", key, val_str);
                    envp[idx++] = strdup(buf);
                }
            }
        }
        if (obj) {
            csilk_json_free(obj);
        }
    }

    envp[idx] = NULL;
    *out_envc = idx;
    return envp;
}

/* 释放 build_envp 返回的 malloc 数组及自定义分配的字符串 */
static void
free_envp(char** envp, size_t base_count, size_t total_count)
{
    if (!envp) {
        return;
    }
    for (size_t i = base_count; i < total_count; i++) {
        free(envp[i]);
    }
    free(envp);
}

/* 向 stdio 子进程写一帧并读回响应帧（换行分帧） */
static bool
drain_stderr_nonblocking(mf_mcp_client_t* c)
{
    if (!c || c->err_fd < 0) {
        return false;
    }
    char    buf[512];
    ssize_t n;
    while ((n = read(c->err_fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        size_t cur_len = strlen(c->last_error);
        if (cur_len + (size_t)n < sizeof(c->last_error) - 1) {
            strncat(c->last_error, buf, sizeof(c->last_error) - cur_len - 1);
        }
    }
    if (n == 0) {
        /* EOF reached on stderr: close descriptor to eliminate POLLHUP busy-loop */
        close(c->err_fd);
        c->err_fd = -1;
        return false;
    }
    return true;
}

static int64_t
monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000LL);
}

static int
stdio_write_all(
    int fd, const char* data, size_t len, int64_t deadline_ms, char* out_err, size_t err_sz)
{
    size_t off = 0;
    while (off < len) {
        int64_t rem = deadline_ms - monotonic_ms();
        if (rem <= 0) {
            if (out_err && err_sz) {
                snprintf(out_err, err_sz, "stdio write timed out");
            }
            return -1;
        }
        int           poll_ms = (rem > INT_MAX) ? INT_MAX : (int)rem;
        struct pollfd pfd = {.fd = fd, .events = POLLOUT, .revents = 0};
        int           prc = poll(&pfd, 1, poll_ms);
        if (prc < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (out_err && err_sz) {
                snprintf(out_err, err_sz, "stdio write poll failed: %s", strerror(errno));
            }
            return -1;
        }
        if (prc == 0) {
            if (out_err && err_sz) {
                snprintf(out_err, err_sz, "stdio write timed out");
            }
            return -1;
        }
        if (pfd.revents & (POLLERR | POLLNVAL)) {
            if (out_err && err_sz) {
                snprintf(
                    out_err, err_sz, "stdio write failed: broken pipe (child process terminated)");
            }
            return -1;
        }
        size_t chunk = len - off;
        if (chunk > 4096) {
            chunk = 4096; /* <= PIPE_BUF so blocking pipe write never stalls after POLLOUT */
        }
        ssize_t w = write(fd, data + off, chunk);
        if (w < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            if (out_err && err_sz) {
                if (errno == EPIPE) {
                    snprintf(out_err,
                             err_sz,
                             "stdio write failed: broken pipe (child process terminated)");
                } else {
                    snprintf(out_err, err_sz, "stdio write failed: %s", strerror(errno));
                }
            }
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

static char*
stdio_call(mf_mcp_client_t* c, const char* request_json, char* out_err, size_t err_sz)
{
    const mf_mcp_server_t* s = c->server;
    if (c->read_fd < 0 || c->write_fd < 0) {
        /* 首次调用：spawn 子进程 */
        size_t argc = 0;
        char** argv = build_argv(s, &argc);
        if (!argv) {
            snprintf(out_err, err_sz, "stdio argv build failed");
            return NULL;
        }
        size_t envc = 0;
        size_t env_basec = 0;
        char** envp = build_envp(s, &envc, &env_basec);

        int in_pipe[2] = {-1, -1};
        int out_pipe[2] = {-1, -1};
        int err_pipe[2] = {-1, -1};
        if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
            if (in_pipe[0] >= 0) {
                close(in_pipe[0]);
                close(in_pipe[1]);
            }
            if (out_pipe[0] >= 0) {
                close(out_pipe[0]);
                close(out_pipe[1]);
            }
            if (err_pipe[0] >= 0) {
                close(err_pipe[0]);
                close(err_pipe[1]);
            }
            for (size_t i = 0; i < argc; i++) {
                free(argv[i]);
            }
            free(argv);
            if (envp) {
                free_envp(envp, env_basec, envc);
            }
            snprintf(out_err, err_sz, "pipe() failed: %s", strerror(errno));
            return NULL;
        }
        fcntl(err_pipe[0], F_SETFL, O_NONBLOCK);
        fcntl(in_pipe[1], F_SETFL, O_NONBLOCK);
        fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);

        pid_t pid = fork();
        if (pid < 0) {
            close(in_pipe[0]);
            close(in_pipe[1]);
            close(out_pipe[0]);
            close(out_pipe[1]);
            close(err_pipe[0]);
            close(err_pipe[1]);
            for (size_t i = 0; i < argc; i++) {
                free(argv[i]);
            }
            free(argv);
            if (envp) {
                free_envp(envp, env_basec, envc);
            }
            snprintf(out_err, err_sz, "fork failed: %s", strerror(errno));
            return NULL;
        }
        if (pid == 0) {
            /* 子进程分支：
               0. 独立进程组与资源限制隔离（纵深防御，避免转储核心与泄漏或残留孙进程） */
            (void)setpgid(0, 0);

            struct rlimit rl_core = {0, 0};
            (void)setrlimit(RLIMIT_CORE, &rl_core);

            struct rlimit rl_nofile;
            if (getrlimit(RLIMIT_NOFILE, &rl_nofile) == 0 && rl_nofile.rlim_cur > 1024) {
                rl_nofile.rlim_cur = 1024;
                if (rl_nofile.rlim_max > 1024) {
                    rl_nofile.rlim_max = 1024;
                }
                (void)setrlimit(RLIMIT_NOFILE, &rl_nofile);
            }

            struct rlimit rl_fsize = {64ULL * 1024ULL * 1024ULL, 64ULL * 1024ULL * 1024ULL};
            const char*   fsize_mb_str = getenv("MINEFOLIO_MCP_RLIMIT_FSIZE_MB");
            if (fsize_mb_str && fsize_mb_str[0]) {
                long fsize_mb = strtol(fsize_mb_str, NULL, 10);
                if (fsize_mb > 0 && fsize_mb <= 16384) {
                    rlim_t fbytes = (rlim_t)fsize_mb * 1024ULL * 1024ULL;
                    rl_fsize.rlim_cur = fbytes;
                    rl_fsize.rlim_max = fbytes;
                }
            }
            (void)setrlimit(RLIMIT_FSIZE, &rl_fsize);

            const char* as_mb_str = getenv("MINEFOLIO_MCP_RLIMIT_AS_MB");
            if (as_mb_str && as_mb_str[0]) {
                long as_mb = strtol(as_mb_str, NULL, 10);
                if (as_mb > 0 && as_mb <= 65536) {
                    rlim_t        bytes = (rlim_t)as_mb * 1024ULL * 1024ULL;
                    struct rlimit rl_as = {bytes, bytes};
                    (void)setrlimit(RLIMIT_AS, &rl_as);
                }
            }

            /* 1. 先重定向标准输入/输出/错误描述符 */
            if (dup2(in_pipe[0], STDIN_FILENO) < 0 || dup2(out_pipe[1], STDOUT_FILENO) < 0 ||
                dup2(err_pipe[1], STDERR_FILENO) < 0) {
                _exit(127);
            }
            /* 2. 关闭原始管道端 */
            close(in_pipe[0]);
            close(in_pipe[1]);
            close(out_pipe[0]);
            close(out_pipe[1]);
            close(err_pipe[0]);
            close(err_pipe[1]);

            /* 3. 清理所有大于 2 的继承描述符，实现严格隔离 */
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
            closefrom(3);
#else
            int max_fd = (int)sysconf(_SC_OPEN_MAX);
            if (max_fd < 0 || max_fd > 4096) {
                max_fd = 4096;
            }
            for (int fd = 3; fd < max_fd; fd++) {
                close(fd);
            }
#endif
            static char* const safe_fallback_env[] = {
                "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
                "TERM=dumb",
                NULL};
            execve(argv[0], argv, envp ? (char* const*)envp : safe_fallback_env);
            _exit(127);
        }
        (void)setpgid(pid, pid);
        c->child_pid = pid;
        c->read_fd = out_pipe[0]; /* 父读子 stdout */
        c->write_fd = in_pipe[1]; /* 父写子 stdin */
        c->err_fd = err_pipe[0];  /* 父读子 stderr */
        close(in_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[1]);
        for (size_t i = 0; i < argc; i++) {
            free(argv[i]);
        }
        free(argv);
        if (envp) {
            free_envp(envp, env_basec, envc);
        }

        /* 超时由调用层（http/curl 的 CURL timeout + bridge 的整体预算）兜底；
           pipe fd 是普通文件描述符而非 socket，无法设 SO_RCVTIMEO。 */
    }

    int     timeout_ms = (c->server && c->server->timeout_ms > 0) ? c->server->timeout_ms : 30000;
    int64_t deadline_ms = monotonic_ms() + (int64_t)timeout_ms;

    /* 写请求帧（带换行），使用预计算长度和单调时钟预算 */
    size_t req_len = strlen(request_json);
    size_t frame_len = req_len + 1;
    char*  frame = malloc(frame_len + 1);
    if (!frame) {
        snprintf(out_err, err_sz, "oom");
        return NULL;
    }
    memcpy(frame, request_json, req_len);
    frame[req_len] = '\n';
    frame[frame_len] = '\0';
    if (stdio_write_all(c->write_fd, frame, frame_len, deadline_ms, out_err, err_sz) != 0) {
        free(frame);
        return NULL;
    }
    free(frame);

    /* 读响应帧（4KB 块缓冲读取，memchr 快速内存扫描分帧，双 FD poll + 单调截止时间控制） */
    c->last_error[0] = '\0';
    char* resp = (char*)malloc(MCP_MAX_FRAME_SIZE + MCP_IO_BUFFER_SIZE + 1);
    if (!resp) {
        snprintf(out_err, err_sz, "oom allocating stdio response buffer");
        return NULL;
    }
    size_t n = 0;

    while (n < MCP_MAX_FRAME_SIZE) {
        /* 1. 若缓冲区中有未消费数据，在内存中扫描换行符 */
        if (c->framer.head < c->framer.tail) {
            char* p = (char*)memchr(
                c->framer.buf + c->framer.head, '\n', c->framer.tail - c->framer.head);
            if (p) {
                size_t len = (size_t)(p - (c->framer.buf + c->framer.head));
                memcpy(resp + n, c->framer.buf + c->framer.head, len);
                n += len;
                c->framer.head = (size_t)(p - c->framer.buf) + 1;
                resp[n] = '\0';
                /* 截断尾部换行并返回 */
                while (n > 0 && (resp[n - 1] == '\n' || resp[n - 1] == '\r')) {
                    resp[--n] = '\0';
                }
                return resp;
            } else {
                /* 缓冲区内无换行，全部移入 resp */
                size_t len = c->framer.tail - c->framer.head;
                memcpy(resp + n, c->framer.buf + c->framer.head, len);
                n += len;
                c->framer.head = c->framer.tail = 0;
            }
        }

        /* 2. 缓冲区已空，执行双路 poll 并块读取 */
        if (c->framer.head == c->framer.tail) {
            c->framer.head = c->framer.tail = 0;
            int64_t rem = deadline_ms - monotonic_ms();
            if (rem <= 0) {
                goto read_timeout;
            }
            int           remaining_ms = (rem > INT_MAX) ? INT_MAX : (int)rem;
            struct pollfd pfds[2] = {
                {.fd = c->read_fd, .events = POLLIN, .revents = 0},
                {.fd = c->err_fd,  .events = POLLIN, .revents = 0}
            };
            int nfds = (c->err_fd >= 0) ? 2 : 1;
            int prc = poll(pfds, nfds, remaining_ms);
            if (prc < 0) {
                if (errno == EINTR) {
                    continue;
                }
                snprintf(out_err, err_sz, "stdio poll failed: %s", strerror(errno));
                c->framer.head = c->framer.tail = 0;
                free(resp);
                return NULL;
            }
            if (prc == 0) {
            read_timeout:
                drain_stderr_nonblocking(c);
                if (c->last_error[0] != '\0') {
                    /* 显式精度截断：last_error(1024B) 可能超出调用方 256B 的 out_err，
                       保留前 100 字节以容纳前缀文案与结尾 NUL，消除 -Wformat-truncation */
                    snprintf(out_err,
                             err_sz,
                             "stdio read timed out after %d ms. %.100s",
                             timeout_ms,
                             c->last_error);
                } else {
                    snprintf(out_err, err_sz, "stdio read timed out after %d ms", timeout_ms);
                }
                c->framer.head = c->framer.tail = 0;
                free(resp);
                return NULL;
            }
            if (c->err_fd >= 0 && (pfds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
                drain_stderr_nonblocking(c);
            }
            if (pfds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
                ssize_t r = read(c->read_fd, c->framer.buf, MCP_IO_BUFFER_SIZE);
                if (r == 0) {
                    /* EOF */
                    drain_stderr_nonblocking(c);
                    if (n > 0) {
                        resp[n] = '\0';
                        while (n > 0 && (resp[n - 1] == '\n' || resp[n - 1] == '\r')) {
                            resp[--n] = '\0';
                        }
                        return resp;
                    }
                    if (c->last_error[0] != '\0') {
                        /* 显式精度截断：last_error(1024B) 可能超出调用方 256B 的 out_err，
                           最多保留 250 字节以容纳结尾 NUL，消除 -Wformat-truncation */
                        snprintf(out_err, err_sz, "%.250s", c->last_error);
                    } else {
                        snprintf(out_err, err_sz, "stdio child produced no response");
                    }
                    c->framer.head = c->framer.tail = 0;
                    free(resp);
                    return NULL;
                }
                if (r < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                        continue;
                    }
                    snprintf(out_err, err_sz, "stdio read failed: %s", strerror(errno));
                    c->framer.head = c->framer.tail = 0;
                    free(resp);
                    return NULL;
                }
                c->framer.tail = (size_t)r;
            }
        }
    }

    /* 超过 1MB 阈值 */
    c->framer.head = c->framer.tail = 0;
    snprintf(out_err, err_sz, "stdio response frame exceeded 1MB limit");
    free(resp);
    return NULL;
}

/* -------------------------------------------------------------------------- */
/* 句柄生命周期                                                                */
/* -------------------------------------------------------------------------- */

mf_mcp_client_t*
mf_mcp_client_new(const mf_mcp_server_t* server)
{
    if (!server) {
        return NULL;
    }
    mf_mcp_client_t* c = calloc(1, sizeof(mf_mcp_client_t));
    if (!c) {
        return NULL;
    }
    memcpy(&c->server_storage, server, sizeof(c->server_storage));
    c->server = &c->server_storage;
    c->session_id = NULL;
    c->read_fd = -1;
    c->write_fd = -1;
    c->err_fd = -1;
    c->child_pid = 0;
    c->framer.head = 0;
    c->framer.tail = 0;
    c->last_error[0] = '\0';
    return c;
}

void
mf_mcp_client_free(mf_mcp_client_t* c)
{
    if (!c) {
        return;
    }
    /* stdio 子进程清理：优先终止整条进程组 (-pid)，SIGTERM → 轮询至多 500ms → SIGKILL */
    if (c->child_pid > 0) {
        if (kill(-c->child_pid, SIGTERM) != 0) {
            (void)kill(c->child_pid, SIGTERM);
        }
        int st = 0;
        int exited = 0;
        for (int w = 0; w < 50; w++) {
            pid_t r = waitpid(c->child_pid, &st, WNOHANG);
            if (r > 0 || (r == -1 && errno == ECHILD)) {
                exited = 1;
                break;
            }
            usleep(10000); /* 10ms */
        }
        if (!exited) {
            if (kill(-c->child_pid, SIGKILL) != 0) {
                (void)kill(c->child_pid, SIGKILL);
            }
            pid_t wr;
            do {
                wr = waitpid(c->child_pid, &st, 0);
            } while (wr == -1 && errno == EINTR);
        }
        c->child_pid = 0;
    }
    if (c->read_fd >= 0) {
        close(c->read_fd);
    }
    if (c->write_fd >= 0) {
        close(c->write_fd);
    }
    if (c->err_fd >= 0) {
        close(c->err_fd);
    }
    free(c->session_id);
    free(c);
}

/* -------------------------------------------------------------------------- */
/* 高层 API                                                                    */
/* -------------------------------------------------------------------------- */

int
mf_mcp_client_initialize(mf_mcp_client_t* c, char* out_err, size_t err_sz)
{
    if (!c) {
        return -1;
    }
    /* initialize 请求 */
    csilk_json_t* params = csilk_json_object();
    csilk_json_t* client_info = csilk_json_object();
    csilk_json_add_string(client_info, "name", "minefolio");
    csilk_json_add_string(client_info, "version", "1.0");
    csilk_json_add_object(params, "clientInfo", client_info);
    csilk_json_add_string(params, "protocolVersion", "2024-11-05");
    /* capabilities 是 MCP 2024-11-05 规范 initialize.params 的必需字段：客户端不声明
       任何能力时也须传空对象 {}。基于 @modelcontextprotocol/sdk 的服务器 (如 a-share-mcp)
       用 zod 严格校验，缺此字段即回 JSON-RPC error -32603 (无 result)，客户端会误报
       "initialize: no result"。add_object 把 caps 所有权转移给 params，随 params 级联释放。 */
    csilk_json_t* caps = csilk_json_object();
    csilk_json_add_object(params, "capabilities", caps);

    char* req = build_request("initialize", params);
    csilk_json_free(params);
    if (!req) {
        snprintf(out_err, err_sz, "build initialize frame failed");
        return -1;
    }

    int ok = -1;
    if (c->server->transport == MCP_TRANSPORT_HTTP) {
        char  err[256] = {0};
        int   http_code = 0;
        char* resp = http_call(c, req, c->server->timeout_ms, NULL, err, sizeof(err), &http_code);
        if (!resp) {
            snprintf(out_err, err_sz, "%s", err);
            free(req);
            return -1;
        }
        csilk_json_t* rj = csilk_json_parse(resp);
        if (rj && csilk_json_get(rj, "result")) {
            ok = 0;
        } else {
            snprintf(out_err, err_sz, "initialize: no result (http=%d)", http_code);
        }
        if (rj) {
            csilk_json_free(rj);
        }
        free(resp);
    } else {
        char  err[256] = {0};
        char* resp = stdio_call(c, req, err, sizeof(err));
        if (!resp) {
            snprintf(out_err, err_sz, "%s", err);
        } else {
            csilk_json_t* rj = csilk_json_parse(resp);
            if (rj && csilk_json_get(rj, "result")) {
                ok = 0;
            } else {
                snprintf(out_err, err_sz, "initialize: no result");
            }
            if (rj) {
                csilk_json_free(rj);
            }
            free(resp);
        }
    }
    free(req);

    if (ok == 0) {
        /* MCP 握手完成：发送 notifications/initialized */
        char* notif = build_notification("notifications/initialized", NULL);
        if (notif) {
            if (c->server->transport == MCP_TRANSPORT_HTTP) {
                char  nerr[256] = {0};
                int   ncode = 0;
                char* nresp =
                    http_call(c, notif, c->server->timeout_ms, NULL, nerr, sizeof(nerr), &ncode);
                if (nresp) {
                    free(nresp);
                }
            } else if (c->write_fd >= 0) {
                int timeout_ms =
                    (c->server && c->server->timeout_ms > 0) ? c->server->timeout_ms : 30000;
                int64_t deadline_ms = monotonic_ms() + (int64_t)timeout_ms;
                size_t  notif_len = strlen(notif);
                size_t  nframe_len = notif_len + 1;
                char*   nframe = malloc(nframe_len + 1);
                if (nframe) {
                    memcpy(nframe, notif, notif_len);
                    nframe[notif_len] = '\n';
                    nframe[nframe_len] = '\0';
                    (void)stdio_write_all(c->write_fd, nframe, nframe_len, deadline_ms, NULL, 0);
                    free(nframe);
                }
            }
            free(notif);
        }
    }

    return ok;
}

int
mf_mcp_client_list_tools(mf_mcp_client_t*       c,
                         int64_t                server_id,
                         int64_t                user_id,
                         mf_mcp_server_tool_t** out_tools,
                         size_t*                out_count)
{
    if (!c) {
        return -1;
    }
    char* req = build_request("tools/list", NULL);
    if (!req) {
        return -1;
    }

    char  err[256] = {0};
    char* resp = NULL;
    if (c->server->transport == MCP_TRANSPORT_HTTP) {
        int http_code = 0;
        resp = http_call(c, req, c->server->timeout_ms, NULL, err, sizeof(err), &http_code);
    } else {
        resp = stdio_call(c, req, err, sizeof(err));
    }
    free(req);
    if (!resp) {
        return -1;
    }

    csilk_json_t* rj = csilk_json_parse(resp);
    free(resp);
    if (!rj) {
        return -1;
    }
    csilk_json_t* result = csilk_json_get(rj, "result");
    csilk_json_t* tools_arr = result ? csilk_json_get(result, "tools") : NULL;

    size_t count = 0;
    if (tools_arr && csilk_json_is_array(tools_arr)) {
        count = csilk_json_array_size(tools_arr);
    }

    mf_mcp_server_tool_t* arr = calloc(count + 1, sizeof(mf_mcp_server_tool_t));
    if (!arr && count > 0) {
        csilk_json_free(rj);
        return -1;
    }

    size_t kept = 0;
    for (size_t i = 0; i < count; i++) {
        const csilk_json_t*   t = csilk_json_array_get(tools_arr, i);
        mf_mcp_server_tool_t* e = &arr[kept];
        memset(e, 0, sizeof(*e));
        e->server_id = server_id;
        e->user_id = user_id;

        const csilk_json_t* name_j = csilk_json_get(t, "name");
        if (name_j && csilk_json_is_string(name_j)) {
            snprintf(e->tool_name, sizeof(e->tool_name), "%s", csilk_json_string_value(name_j));
        }
        /* 缺 name 的畸形条目直接丢弃：空 tool_name 会生成 "mcp:<id>:" 残缺路由名 */
        if (!e->tool_name[0]) {
            continue;
        }
        kept++;
        /* qualified_name = mcp:<serverId>:<tool_name> */
        snprintf(e->qualified_name,
                 sizeof(e->qualified_name),
                 "mcp:%lld:%s",
                 (long long)server_id,
                 e->tool_name);

        const csilk_json_t* desc_j = csilk_json_get(t, "description");
        if (desc_j && csilk_json_is_string(desc_j)) {
            const char* dv = csilk_json_string_value(desc_j);
            size_t      dl = strlen(dv);
            if (dl >= sizeof(e->description)) {
                dl = sizeof(e->description) - 1;
            }
            memcpy(e->description, dv, dl);
        }

        const csilk_json_t* input_j = csilk_json_get(t, "inputSchema");
        if (input_j) {
            size_t slen = 0;
            char*  sstr = csilk_json_serialize(input_j, &slen);
            size_t cap = sizeof(e->input_schema) - 1;
            if (sstr) {
                if (slen <= cap) {
                    memcpy(e->input_schema, sstr, slen);
                    e->input_schema[slen] = '\0';
                } else {
                    /* 超大 Schema：安全降级为空 Schema 对象，防止产生非法截断破坏 JSON */
                    snprintf(e->input_schema, sizeof(e->input_schema), "{}");
                }

                /* is_mutation：inputSchema 内或工具名出现动账/提交类关键词的启发式判定 */
                char   lower[1024];
                size_t len = slen < sizeof(lower) - 1 ? slen : sizeof(lower) - 1;
                for (size_t k = 0; k < len; k++) {
                    lower[k] = (char)tolower((unsigned char)sstr[k]);
                }
                lower[len] = '\0';
                e->is_mutation =
                    (strstr(lower, "write") || strstr(lower, "delete") || strstr(lower, "update") ||
                     strstr(lower, "mutation") || strstr(lower, "submit") ||
                     strstr(e->tool_name, "delete") || strstr(e->tool_name, "update") ||
                     strstr(e->tool_name, "create") || strstr(e->tool_name, "insert") ||
                     strstr(e->tool_name, "send") || strstr(e->tool_name, "submit") ||
                     strstr(e->tool_name, "publish") || strstr(e->tool_name, "pay") ||
                     strstr(e->tool_name, "transfer"))
                        ? 1
                        : 0;
                free(sstr);
            }
        }

        /* risk_level：按 is_mutation + 名称启发式（设计 §4） */
        if (e->is_mutation) {
            snprintf(e->risk_level, sizeof(e->risk_level), "medium");
        } else {
            snprintf(e->risk_level, sizeof(e->risk_level), "low");
        }
        time_t    now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        char ts[32] = {0};
        /* 时间戳格式必须与 mcp_bridge.c 的 TTL 解析器 strptime("%Y-%m-%d %H:%M:%S") 一致：
           用空格分隔（非 ISO-8601 的 'T'）。若写 'T' 会导致 strptime 解析失败，
           mcp_tool_is_stale 保守保留（TTL 形同虚设，工具缓存永不过期）。 */
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
        snprintf(e->fetched_at, sizeof(e->fetched_at), "%s", ts);
    }

    csilk_json_free(rj);
    *out_tools = arr;
    *out_count = kept;
    return 0;
}

char*
mf_mcp_client_call_tool(
    mf_mcp_client_t* c, const char* tool_name, const char* args_json, char* out_err, size_t err_sz)
{
    if (!c) {
        return NULL;
    }
    csilk_json_t* params = csilk_json_object();
    csilk_json_add_string(params, "name", tool_name ? tool_name : "");
    if (args_json && args_json[0]) {
        csilk_json_t* args = csilk_json_parse(args_json);
        if (args) {
            csilk_json_add_object(params, "arguments", args); /* 移入 ownership */
        } else {
            csilk_json_add_string(params, "arguments", args_json);
        }
    }
    char* req = build_request("tools/call", params);
    csilk_json_free(params);
    if (!req) {
        return NULL;
    }

    char  err[256] = {0};
    char* resp = NULL;
    if (c->server->transport == MCP_TRANSPORT_HTTP) {
        int http_code = 0;
        resp = http_call(c, req, c->server->timeout_ms, NULL, err, sizeof(err), &http_code);
    } else {
        resp = stdio_call(c, req, err, sizeof(err));
    }
    free(req);
    if (!resp) {
        snprintf(out_err, err_sz, "%s", err);
        return NULL;
    }

    /* 从响应 result 中抽取 content[0].text */
    csilk_json_t* rj = csilk_json_parse(resp);
    free(resp);
    if (!rj) {
        snprintf(out_err, err_sz, "tools/call response invalid JSON");
        return NULL;
    }
    csilk_json_t* result = csilk_json_get(rj, "result");
    if (!result) {
        csilk_json_t* err_node = csilk_json_get(rj, "error");
        size_t        elen = 0;
        char*         estr =
            err_node ? csilk_json_serialize(err_node, &elen) : strdup("{\"error\":\"remote\"}");
        const char* emsg = NULL;
        if (err_node && csilk_json_is_object(err_node)) {
            emsg = csilk_json_get_string(err_node, "message");
        }
        snprintf(out_err, err_sz, "%s", (emsg && emsg[0]) ? emsg : (estr ? estr : "remote error"));
        csilk_json_free(rj);
        if (estr) {
            return estr;
        }
        return strdup("{\"error\":\"remote\"}");
    }

    /* 检查 CallToolResult.isError (MCP 规范) */
    const csilk_json_t* is_err_node = csilk_json_get(result, "isError");
    bool                is_tool_err = false;
    if (is_err_node) {
        if (csilk_json_is_bool(is_err_node)) {
            is_tool_err = csilk_json_bool_value(is_err_node);
        } else if (csilk_json_is_number(is_err_node)) {
            is_tool_err = (csilk_json_number_value(is_err_node) != 0);
        }
    }

    /* result.content[0].text */
    csilk_json_t* content = csilk_json_get(result, "content");
    if (content && csilk_json_is_array(content) && csilk_json_array_size(content) > 0) {
        const csilk_json_t* first = csilk_json_array_get(content, 0);
        const csilk_json_t* text = first ? csilk_json_get(first, "text") : NULL;
        if (text && csilk_json_is_string(text)) {
            const char* s = csilk_json_string_value(text);
            char*       out = s ? strdup(s) : strdup("{}");
            if (is_tool_err && !out_err[0]) {
                snprintf(out_err, err_sz, "tool reported error: %.200s", out ? out : "");
            }
            csilk_json_free(rj);
            return out;
        }
    }
    /* 否则直接序列化整个 result */
    char* out = csilk_json_serialize(result, NULL);
    if (is_tool_err && !out_err[0]) {
        snprintf(out_err, err_sz, "tool reported error");
    }
    csilk_json_free(rj);
    return out ? out : strdup("{\"error\":\"empty tools/call result\"}");
}
