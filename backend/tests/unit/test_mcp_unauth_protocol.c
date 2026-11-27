#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "csilk/csilk.h"
#include "common/jwt.h"
#include "interfaces/http/controllers/mcp_server_controller.h"
#include "services/ai/tools/registry.h"

typedef struct {
    csilk_app_t*    app;
    csilk_server_t* srv;
    int             port;
    pthread_t       thread;
    atomic_int      exited;
} mcp_test_server_t;

static int pick_loopback_port(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    assert(s >= 0);
    int opt = 1;
    (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = 0;
    assert(bind(s, (struct sockaddr*)&sin, sizeof(sin)) == 0);
    socklen_t len = sizeof(sin);
    assert(getsockname(s, (struct sockaddr*)&sin, &len) == 0);
    int port = ntohs(sin.sin_port);
    close(s);
    assert(port > 0);
    return port;
}

static int connect_loopback(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = htons((uint16_t)port);
    if (connect(s, (struct sockaddr*)&sin, sizeof(sin)) != 0) {
        close(s);
        return -1;
    }
    return s;
}

static int send_all_bytes(int fd, const char* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static ssize_t read_until_eof(int fd, char* buf, size_t cap) {
    size_t total = 0;
    while (total < cap - 1) {
        ssize_t n = recv(fd, buf + total, cap - 1 - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        total += (size_t)n;
    }
    buf[total] = '\0';
    return (ssize_t)total;
}

static void* mcp_server_thread(void* arg) {
    mcp_test_server_t* s = (mcp_test_server_t*)arg;
    (void)csilk_app_run(s->app, s->port);
    atomic_store_explicit(&s->exited, 1, memory_order_release);
    return NULL;
}

static csilk_json_t* post_mcp_json(int port, const char* json_body) {
    char req[1024];
    int  req_len = snprintf(req,
                            sizeof(req),
                            "POST /mcp HTTP/1.1\r\n"
                            "Host: localhost\r\n"
                            "Content-Type: application/json\r\n"
                            "Content-Length: %zu\r\n"
                            "Connection: close\r\n"
                            "\r\n"
                            "%s",
                            strlen(json_body),
                            json_body);
    assert(req_len > 0 && (size_t)req_len < sizeof(req));

    int fd = connect_loopback(port);
    assert(fd >= 0);
    assert(send_all_bytes(fd, req, (size_t)req_len) == 0);

    char    resp_buf[4096];
    ssize_t n = read_until_eof(fd, resp_buf, sizeof(resp_buf));
    close(fd);
    assert(n > 0);

    const char* hdr_end = strstr(resp_buf, "\r\n\r\n");
    assert(hdr_end != NULL);
    const char* body = hdr_end + 4;
    csilk_json_t* parsed = csilk_json_parse(body);
    assert(parsed != NULL);
    return parsed;
}

static void test_mcp_response_id_formatting_and_leaks(void) {
    signal(SIGPIPE, SIG_IGN);
    mcp_test_server_t s;
    memset(&s, 0, sizeof(s));
    atomic_init(&s.exited, 0);
    s.port = pick_loopback_port();
    s.app = csilk_app_new(NULL);
    assert(s.app != NULL);

    csilk_config_t* cfg = csilk_app_config(s.app);
    assert(cfg != NULL);
    csilk_server_config_t scfg = cfg->server;
    scfg.worker_threads = 1;
    scfg.idle_timeout_ms = 5000;
    csilk_app_set_server_config(s.app, scfg);
    free(cfg);

    register_mcp_server_routes(s.app);
    s.srv = csilk_app_server(s.app);
    assert(s.srv != NULL);

    assert(pthread_create(&s.thread, NULL, mcp_server_thread, &s) == 0);

    /* Wait for server socket to listen */
    for (int i = 0; i < 100; i++) {
        if (atomic_load_explicit(&s.exited, memory_order_acquire) != 0) break;
        int fd = connect_loopback(s.port);
        if (fd >= 0) {
            close(fd);
            break;
        }
        struct timespec ts = {0, 10000000L}; /* 10ms */
        nanosleep(&ts, NULL);
    }

    /* 1. Numeric id: must echo id=42 and error=-32001 without leaking csilk_json_null() */
    csilk_json_t* r_num =
        post_mcp_json(s.port, "{\"jsonrpc\":\"2.0\",\"id\":42,\"method\":\"initialize\",\"params\":{}}");
    assert(strcmp(csilk_json_get_string(r_num, "jsonrpc"), "2.0") == 0);
    const csilk_json_t* id_num = csilk_json_get(r_num, "id");
    assert(id_num != NULL && csilk_json_is_number(id_num) && csilk_json_int_value(id_num) == 42);
    const csilk_json_t* err_num = csilk_json_get(r_num, "error");
    assert(err_num != NULL && csilk_json_int_value(csilk_json_get(err_num, "code")) == -32001);
    csilk_json_free(r_num);

    /* 2. JSON null id: must emit "id":null via csilk_json_is_null() without leaking */
    csilk_json_t* r_null =
        post_mcp_json(s.port, "{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"initialize\",\"params\":{}}");
    assert(strcmp(csilk_json_get_string(r_null, "jsonrpc"), "2.0") == 0);
    const csilk_json_t* id_null = csilk_json_get(r_null, "id");
    assert(id_null != NULL && csilk_json_is_null(id_null));
    const csilk_json_t* err_null = csilk_json_get(r_null, "error");
    assert(err_null != NULL && csilk_json_int_value(csilk_json_get(err_null, "code")) == -32001);
    csilk_json_free(r_null);

    csilk_server_stop(s.srv);
    pthread_join(s.thread, NULL);
    csilk_app_free(s.app);
}

int main(void) {
    printf("Running test_mcp_unauth_protocol...\n");

    /* When context has no jwt_payload, jwt_get_user_id returns -1 without side effects */
    void* dummy_ctx = calloc(1, 4096);
    int64_t uid = jwt_get_user_id((csilk_ctx_t*)dummy_ctx);
    assert(uid < 0);

    /* Verify internal_only confirmation tools exist in registry but are excluded from LLM/MCP exposure */
    const ai_tool_t* ce = ai_tool_find("confirm_proposed_expense");
    const ai_tool_t* ct = ai_tool_find("confirm_proposed_transfer");
    assert(ce != NULL && ce->internal_only == true);
    assert(ct != NULL && ct->internal_only == true);

    size_t count = 0;
    const csilk_ai_tool_t* defs = ai_tool_get_csilk_definitions(&count);
    assert(defs != NULL && count > 0);
    for (size_t i = 0; i < count; i++) {
        assert(strcmp(defs[i].function.name, "confirm_proposed_expense") != 0);
        assert(strcmp(defs[i].function.name, "confirm_proposed_transfer") != 0);
    }

    /* Verify mcp_endpoint_handler cleanly produces JSON-RPC -32001 when user_id < 0
       for both numeric and null request IDs without leaking under LSan */
    test_mcp_response_id_formatting_and_leaks();

    free(dummy_ctx);
    printf("PASS: test_mcp_unauth_protocol completed successfully\n");
    return 0;
}
