/**
 * @file mcp_client.c
 * @brief MCP 远端工具源客户端实现 — M2
 *
 * 帧收发复用 csilk_mcp_msg_*，传输层：
 *  - HTTP：libcurl 短连接（请求/响应各一次 curl_easy），Mcp-Session-Id 跨调用透传
 *  - STDIO：fork+execv 拉起子进程，stdin/stdout 换行分帧；超时则 SIGTERM→SIGKILL
 */

#include "services/ai/tools/mcp/mcp_client.h"
#include "config/secret.h"

#include <curl/curl.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

/* 远端工具单次 schema 最大长度（与 entity input_schema[4096] 对齐） */
#define MF_MCP_TOOL_SCHEMA_MAX 4096

/* -------------------------------------------------------------------------- */
/* libcurl 内存写缓冲                                                          */
/* -------------------------------------------------------------------------- */

typedef struct {
    char*  data;
    size_t size;
} memory_buf_t;

static size_t
curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    memory_buf_t* mem = (memory_buf_t*)userdata;
    size_t        total = size * nmemb;
    char*         newp = realloc(mem->data ? mem->data : (void*)malloc(1), mem->size + total + 1);
    if (!newp) {
        return 0; /* 触发 OOM，curl 返回 -1 */
    }
    mem->data = newp;
    memcpy(&mem->data[mem->size], ptr, total);
    mem->size += total;
    mem->data[mem->size] = '\0';
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
    csilk_json_add_item(frame, params_copy); /* add_item 接管所有权 */
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
            char hdr[512];
            snprintf(hdr, sizeof(hdr), "%s: %s", key ? key : "", csilk_json_string_value(val));
            *headers = curl_slist_append(*headers, hdr);
        }
    }
    csilk_json_free(obj);
}

/* 发送 JSON-RPC 请求到 MCP HTTP 端点，返回响应体（堆分配） */
static char*
http_call(const mf_mcp_client_t* c,
          const char*            request_json,
          long                   timeout_ms,
          struct curl_slist**    out_headers,
          char*                  out_err,
          size_t                 err_sz,
          int*                   out_http_code)
{
    const mf_mcp_server_t* s = c->server;
    CURL*                  curl = curl_easy_init();
    if (!curl) {
        snprintf(out_err, err_sz, "curl init failed");
        return NULL;
    }

    memory_buf_t chunk = {.data = malloc(1), .size = 0};

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json, text/event-stream");
    if (c->session_id && c->session_id[0]) {
        char hdr[256];
        snprintf(hdr, sizeof(hdr), "Mcp-Session-Id: %s", c->session_id);
        headers = curl_slist_append(headers, hdr);
    }
    /* 用户自定义 headers */
    append_header_json(s->headers_json, &headers);
    /* 凭证：secret_ref 非空时附 Authorization */
    if (s->secret_ref[0]) {
        char        tok[512];
        const char* t = config_env_get(s->secret_ref, tok, sizeof(tok), "");
        if (t && t[0]) {
            char hdr[512];
            snprintf(hdr, sizeof(hdr), "Authorization: Bearer %s", t);
            headers = curl_slist_append(headers, hdr);
        }
    }

    curl_easy_setopt(curl, CURLOPT_URL, s->url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);

    CURLcode res = curl_easy_perform(curl);
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
    for (char* tok = strtok(path_copy, ":"); tok; tok = strtok(NULL, ":")) {
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", tok, prog);
        if (access(full, X_OK) == 0) {
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
    char* stack_argv[41];
    int   argc = 0;
    char  cmd_copy[512];
    snprintf(cmd_copy, sizeof(cmd_copy), "%s", s->command);

    char* tok = strtok(cmd_copy, " \t");
    while (tok && argc < 40) {
        stack_argv[argc++] = strdup(tok);
        if (!stack_argv[argc - 1]) {
            for (int k = 0; k < argc; k++) {
                free(stack_argv[k]);
            }
            return NULL;
        }
        tok = strtok(NULL, " \t");
    }
    if (argc == 0) {
        return NULL;
    }

    if (s->args_json[0]) {
        csilk_json_t* arr = csilk_json_parse(s->args_json);
        if (arr && csilk_json_is_array(arr)) {
            size_t n = csilk_json_array_size(arr);
            for (size_t i = 0; i < n && argc < 40; i++) {
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

/* 构造 envp（env_json 非空时追加 KEY=VALUE，基底 environ） */
static char**
build_envp(const mf_mcp_server_t* s, size_t* out_envc)
{
    if (!s->env_json[0]) {
        *out_envc = 0;
        return environ; /* 继承当前环境 */
    }
    csilk_json_t* obj = csilk_json_parse(s->env_json);
    if (!obj || !csilk_json_is_object(obj)) {
        if (obj) {
            csilk_json_free(obj);
        }
        *out_envc = 0;
        return environ;
    }

    /* 基底 environ + 自定义键值 */
    size_t base_count = 0;
    for (char** p = environ; *p; p++) {
        base_count++;
    }
    char** envp = malloc((base_count + 64 + 1) * sizeof(char*));
    if (!envp) {
        csilk_json_free(obj);
        *out_envc = 0;
        return environ;
    }
    size_t idx = 0;
    for (char** p = environ; *p; p++) {
        envp[idx++] = *p;
    }
    size_t i = 0;
    size_t obj_n = csilk_json_object_size(obj);
    while (idx < base_count + 64 && i < obj_n) {
        const char*         key = csilk_json_object_key(obj, i);
        const csilk_json_t* val = csilk_json_object_val(obj, i);
        if (val && csilk_json_is_string(val)) {
            char buf[512];
            snprintf(buf, sizeof(buf), "%s=%s", key ? key : "", csilk_json_string_value(val));
            envp[idx++] = strdup(buf);
        }
        i++;
    }
    envp[idx] = NULL;
    *out_envc = idx;
    csilk_json_free(obj);
    return envp;
}

/* 释放 build_envp 返回的 malloc 数组（仅在 envp != environ 时调用）。
   environ 基底串不可 free，自定义 strdup 串子进程短命故不单独追踪，
   仅释放数组本身避免大对象泄漏。 */
static void
free_envp(char** envp, size_t envc)
{
    (void)envc;
    if (envp == environ) {
        return;
    }
    free(envp);
}

/* 向 stdio 子进程写一帧并读回响应帧（换行分帧） */
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
        char** envp = build_envp(s, &envc);

        int in_pipe[2], out_pipe[2];
        if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
            free(argv);
            if (envp) {
                free_envp(envp, envc);
            }
            snprintf(out_err, err_sz, "pipe() failed: %s", strerror(errno));
            return NULL;
        }

        pid_t pid = fork();
        if (pid < 0) {
            close(in_pipe[0]);
            close(in_pipe[1]);
            close(out_pipe[0]);
            close(out_pipe[1]);
            free(argv);
            if (envp) {
                free_envp(envp, envc);
            }
            snprintf(out_err, err_sz, "fork failed: %s", strerror(errno));
            return NULL;
        }
        if (pid == 0) {
            close(in_pipe[1]);
            close(out_pipe[0]);
            if (dup2(in_pipe[0], STDIN_FILENO) != 0) {
                _exit(127);
            }
            if (dup2(out_pipe[1], STDOUT_FILENO) != 0) {
                _exit(127);
            }
            close(in_pipe[0]);
            close(out_pipe[1]);
            execve(argv[0], argv, envp ? (char* const*)envp : environ);
            _exit(127);
        }
        c->child_pid = pid;
        c->read_fd = out_pipe[0]; /* 父读子 stdout */
        c->write_fd = in_pipe[1]; /* 父写子 stdin */
        close(in_pipe[0]);
        close(out_pipe[1]);
        free(argv);
        if (envp) {
            free_envp(envp, envc);
        }

        /* 超时由调用层（http/curl 的 CURL timeout + bridge 的整体预算）兜底；
           pipe fd 是普通文件描述符而非 socket，无法设 SO_RCVTIMEO。 */
    }

    /* 写请求帧（带换行） */
    char* frame = malloc(strlen(request_json) + 2);
    if (!frame) {
        snprintf(out_err, err_sz, "oom");
        return NULL;
    }
    sprintf(frame, "%s\n", request_json);
    size_t off = 0;
    while (off < strlen(frame)) {
        ssize_t w = write(c->write_fd, frame + off, strlen(frame) - off);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(frame);
            snprintf(out_err, err_sz, "stdio write failed: %s", strerror(errno));
            return NULL;
        }
        off += (size_t)w;
    }
    free(frame);

    /* 读响应帧（到换行为止，上限 1MB） */
    char   resp[1 << 20];
    char*  cur = resp;
    size_t n = 0;
    while (n < sizeof(resp) - 1) {
        ssize_t r = read(c->read_fd, cur, 1);
        if (r == 0) {
            break; /* EOF */
        }
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            snprintf(out_err, err_sz, "stdio read failed: %s", strerror(errno));
            return NULL;
        }
        cur++;
        n++;
        if (cur[-1] == '\n') {
            break;
        }
    }
    *cur = '\0';
    if (n == 0) {
        snprintf(out_err, err_sz, "stdio child produced no response");
        return NULL;
    }
    /* 截断尾部换行 */
    while (n > 0 && (resp[n - 1] == '\n' || resp[n - 1] == '\r')) {
        resp[--n] = '\0';
    }
    return strdup(resp);
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
    c->server = server;
    c->session_id = NULL;
    c->read_fd = -1;
    c->write_fd = -1;
    c->child_pid = 0;
    c->last_error[0] = '\0';
    return c;
}

void
mf_mcp_client_free(mf_mcp_client_t* c)
{
    if (!c) {
        return;
    }
    /* stdio 子进程清理：SIGTERM → 等 2s → SIGKILL（设计 §10） */
    if (c->child_pid > 0) {
        kill(c->child_pid, SIGTERM);
        usleep(2000000); /* 2s */
        int st = 0;
        if (waitpid(c->child_pid, &st, WNOHANG) == 0) {
            kill(c->child_pid, SIGKILL);
            waitpid(c->child_pid, &st, 0);
        }
        c->child_pid = 0;
    }
    if (c->read_fd >= 0) {
        close(c->read_fd);
    }
    if (c->write_fd >= 0) {
        close(c->write_fd);
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
    csilk_json_add_item(params, client_info);
    csilk_json_add_string(params, "protocolVersion", "2024-11-05");

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
    char* req = build_request("tools/list", csilk_json_object());
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

    for (size_t i = 0; i < count; i++) {
        const csilk_json_t*   t = csilk_json_array_get(tools_arr, i);
        mf_mcp_server_tool_t* e = &arr[i];
        memset(e, 0, sizeof(*e));
        e->server_id = server_id;
        e->user_id = user_id;

        const csilk_json_t* name_j = csilk_json_get(t, "name");
        if (name_j && csilk_json_is_string(name_j)) {
            snprintf(e->tool_name, sizeof(e->tool_name), "%s", csilk_json_string_value(name_j));
        }
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
            if (slen >= cap) {
                slen = cap;
            }
            if (sstr) {
                memcpy(e->input_schema, sstr, slen);
                e->input_schema[slen] = '\0';
                free(sstr);
            }
        }

        /* is_mutation：inputSchema 内出现 "write"/"mutation"/"delete"/"update" 等启发式判定 */
        if (input_j) {
            size_t slen = 0;
            char*  sstr = csilk_json_serialize(input_j, &slen);
            if (sstr) {
                char   lower[1024];
                size_t len = slen < sizeof(lower) - 1 ? slen : sizeof(lower) - 1;
                for (size_t k = 0; k < len; k++) {
                    lower[k] = (char)tolower((unsigned char)sstr[k]);
                }
                lower[len] = '\0';
                e->is_mutation = (strstr(lower, "write") || strstr(lower, "delete") ||
                                  strstr(lower, "update") || strstr(lower, "mutation"))
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
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm);
        snprintf(e->fetched_at, sizeof(e->fetched_at), "%s", ts);
    }

    csilk_json_free(rj);
    *out_tools = arr;
    *out_count = count;
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
            csilk_json_add_item(params, args); /* 移入 ownership */
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
        csilk_json_free(rj);
        if (estr) {
            return estr;
        }
        return strdup("{\"error\":\"remote\"}");
    }

    /* result.content[0].text */
    csilk_json_t* content = csilk_json_get(result, "content");
    if (content && csilk_json_is_array(content) && csilk_json_array_size(content) > 0) {
        const csilk_json_t* first = csilk_json_array_get(content, 0);
        const csilk_json_t* text = first ? csilk_json_get(first, "text") : NULL;
        if (text && csilk_json_is_string(text)) {
            /* 尝试把 text 再 parse 成 JSON；失败则原样返回 */
            char* out = csilk_json_serialize(text, NULL);
            csilk_json_free(rj);
            return out ? out : strdup(csilk_json_string_value(text));
        }
    }
    /* 否则直接序列化整个 result */
    char* out = csilk_json_serialize(result, NULL);
    csilk_json_free(rj);
    return out ? out : strdup("{\"error\":\"empty tools/call result\"}");
}
