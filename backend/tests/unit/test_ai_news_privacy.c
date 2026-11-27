#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "csilk/csilk.h"
#include "csilk/app/app.h"
#include "common/db.h"
#include "common/jwt.h"
#include "common/ledger_utils.h"
#include "common/response.h"
#include "config/secret.h"
#include "domain/auth/repository.h"
#include "interfaces/http/controllers/ai_news_controller.h"
#include "middlewares/jwt_middleware.h"
#include "services/ai/ai_news_service.h"

static const char* TEST_DB_PATH = "/tmp/test_mf_ai_news_privacy.db";

static csilk_db_pool_t*
setup_test_db(void)
{
    unlink(TEST_DB_PATH);
    char shm[256], wal[256];
    snprintf(shm, sizeof(shm), "%s-shm", TEST_DB_PATH);
    snprintf(wal, sizeof(wal), "%s-wal", TEST_DB_PATH);
    unlink(shm);
    unlink(wal);

    config_secret_set_test_mode(true);
    config_secret_set_test_override("DB_DRIVER", "sqlite");
    config_secret_set_test_override("DB_DSN", TEST_DB_PATH);
    config_secret_set_test_override("JWT_SECRET", "test_jwt_secret_ai_news_privacy_32b!");

    csilk_db_pool_t* pool = NULL;
    int              rc = db_init(&pool);
    assert(rc == 0 && pool != NULL);
    rc = db_run_migrations(pool);
    assert(rc == 0);
    return pool;
}

typedef struct {
    csilk_app_t*    app;
    csilk_server_t* srv;
    int             port;
    pthread_t       thread;
    atomic_int      exited;
} news_http_srv_t;

static void
news_healthz_handler(csilk_ctx_t* c)
{
    csilk_string(c, CSILK_STATUS_OK, "ok");
}

static int
pick_loopback_port(void)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    assert(s >= 0);
    int opt = 1;
    (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in sin = {0};
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

static int
connect_loopback(int port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return -1;
    }
    struct sockaddr_in sin = {0};
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = htons((uint16_t)port);
    if (connect(s, (struct sockaddr*)&sin, sizeof(sin)) != 0) {
        close(s);
        return -1;
    }
    return s;
}

static ssize_t
http_exchange(int port, const char* req, char* resp, size_t resp_sz)
{
    int fd = connect_loopback(port);
    if (fd < 0) {
        return -1;
    }
    size_t len = strlen(req);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, req + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            close(fd);
            return -1;
        }
        sent += (size_t)n;
    }
    size_t total = 0;
    while (total < resp_sz - 1) {
        struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
        int           pr = poll(&pfd, 1, 1000);
        if (pr <= 0) {
            break;
        }
        ssize_t n = recv(fd, resp + total, resp_sz - 1 - total, 0);
        if (n <= 0) {
            break;
        }
        total += (size_t)n;
    }
    resp[total] = '\0';
    close(fd);
    return (ssize_t)total;
}

static void*
news_srv_runner(void* arg)
{
    news_http_srv_t* h = (news_http_srv_t*)arg;
    csilk_app_run(h->app, h->port);
    atomic_store_explicit(&h->exited, 1, memory_order_release);
    return NULL;
}

static void
start_news_http_srv(news_http_srv_t* h)
{
    memset(h, 0, sizeof(*h));
    atomic_init(&h->exited, 0);
    h->port = pick_loopback_port();
    h->app = csilk_app_new(NULL);
    assert(h->app != NULL);
    csilk_config_t* cfg = csilk_app_config(h->app);
    assert(cfg != NULL);
    csilk_server_config_t scfg = cfg->server;
    scfg.worker_threads = 1;
    scfg.idle_timeout_ms = 5000;
    csilk_app_set_server_config(h->app, scfg);
    free(cfg);

    csilk_app_get(h->app, "/healthz", news_healthz_handler);
    csilk_app_use_group(h->app, "/api", jwt_middleware_wrapper);
    register_ai_news_routes(h->app);
    h->srv = csilk_app_server(h->app);
    assert(h->srv != NULL);
    assert(pthread_create(&h->thread, NULL, news_srv_runner, h) == 0);

    bool ready = false;
    for (int i = 0; i < 100; ++i) {
        if (atomic_load_explicit(&h->exited, memory_order_acquire) != 0) {
            break;
        }
        char resp[512];
        if (http_exchange(h->port,
                          "GET /healthz HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
                          resp,
                          sizeof(resp)) > 0 &&
            strstr(resp, "HTTP/1.1 200") != NULL) {
            ready = true;
            break;
        }
        struct timespec ts = {0, 15000000L};
        nanosleep(&ts, NULL);
    }
    assert(ready);
}

static void
stop_news_http_srv(news_http_srv_t* h)
{
    csilk_server_stop(h->srv);
    pthread_join(h->thread, NULL);
    csilk_app_free(h->app);
}

int main(void) {
    printf("Running test_ai_news_privacy...\n");

    /* 1. 测试正常多资产的脱敏与比例计算 */
    csilk_json_t* assets = csilk_json_array();
    
    csilk_json_t* a1 = csilk_json_object();
    csilk_json_add_string(a1, "asset_name", "贵州茅台");
    csilk_json_add_string(a1, "asset_type", "stock");
    csilk_json_add_string(a1, "current_value", "60000.00");
    csilk_json_add_string(a1, "cost_basis", "50000.00");
    csilk_json_add_string(a1, "quantity", "30");
    csilk_json_array_append(assets, a1);

    csilk_json_t* a2 = csilk_json_object();
    csilk_json_add_string(a2, "asset_name", "纳指ETF");
    csilk_json_add_string(a2, "asset_type", "fund");
    csilk_json_add_string(a2, "current_value", "40000.00");
    csilk_json_add_string(a2, "cost_basis", "38000.00");
    csilk_json_add_string(a2, "quantity", "20000");
    csilk_json_array_append(assets, a2);

    csilk_json_t* masked = NULL;
    double total_val = 0.0;
    int rc = ai_news_mask_portfolio(assets, &masked, &total_val);
    assert(rc == 0);
    assert(masked != NULL);
    assert(csilk_json_array_size(masked) == 2);
    assert(total_val == 100000.0);

    /* 验证第一项 */
    csilk_json_t* m1 = csilk_json_array_get(masked, 0);
    const char* n1 = csilk_json_get_string(m1, "name");
    const char* c1 = csilk_json_get_string(m1, "category");
    const char* w1 = csilk_json_get_string(m1, "weight");
    assert(strcmp(n1, "贵州茅台") == 0);
    assert(strcmp(c1, "stock") == 0);
    assert(strcmp(w1, "60.0%") == 0);
    /* 关键安全断言：绝对不能泄露金额与数量 */
    assert(csilk_json_get(m1, "current_value") == NULL);
    assert(csilk_json_get(m1, "cost_basis") == NULL);
    assert(csilk_json_get(m1, "quantity") == NULL);

    /* 验证第二项 */
    csilk_json_t* m2 = csilk_json_array_get(masked, 1);
    const char* n2 = csilk_json_get_string(m2, "name");
    const char* c2 = csilk_json_get_string(m2, "category");
    const char* w2 = csilk_json_get_string(m2, "weight");
    assert(strcmp(n2, "纳指ETF") == 0);
    assert(strcmp(c2, "fund") == 0);
    assert(strcmp(w2, "40.0%") == 0);
    assert(csilk_json_get(m2, "current_value") == NULL);

    csilk_json_free(assets);
    csilk_json_free(masked);

    /* 2. 测试单资产场景 (占比 100.0%) */
    csilk_json_t* single_arr = csilk_json_array();
    csilk_json_t* sa = csilk_json_object();
    csilk_json_add_string(sa, "asset_name", "比特币");
    csilk_json_add_string(sa, "asset_type", "crypto");
    csilk_json_add_string(sa, "current_value", "12345.67");
    csilk_json_array_append(single_arr, sa);

    masked = NULL;
    rc = ai_news_mask_portfolio(single_arr, &masked, &total_val);
    assert(rc == 0);
    assert(masked != NULL);
    assert(csilk_json_array_size(masked) == 1);
    csilk_json_t* sm = csilk_json_array_get(masked, 0);
    assert(strcmp(csilk_json_get_string(sm, "weight"), "100.0%") == 0);
    csilk_json_free(single_arr);
    csilk_json_free(masked);

    /* 3. 测试空持仓场景 */
    csilk_json_t* empty_arr = csilk_json_array();
    masked = NULL;
    rc = ai_news_mask_portfolio(empty_arr, &masked, &total_val);
    assert(rc == 0);
    assert(masked != NULL);
    assert(csilk_json_array_size(masked) == 0);
    assert(total_val == 0.0);
    csilk_json_free(empty_arr);
    csilk_json_free(masked);

    /* 4. 测试零市值场景 (防止除以零) */
    csilk_json_t* zero_arr = csilk_json_array();
    csilk_json_t* za = csilk_json_object();
    csilk_json_add_string(za, "asset_name", "已清仓股票");
    csilk_json_add_string(za, "asset_type", "stock");
    csilk_json_add_string(za, "current_value", "0.00");
    csilk_json_array_append(zero_arr, za);

    masked = NULL;
    rc = ai_news_mask_portfolio(zero_arr, &masked, &total_val);
    assert(rc == 0);
    assert(masked != NULL);
    assert(csilk_json_array_size(masked) == 1);
    csilk_json_t* zm = csilk_json_array_get(masked, 0);
    assert(strcmp(csilk_json_get_string(zm, "weight"), "0.0%") == 0);
    csilk_json_free(zero_arr);
    csilk_json_free(masked);

    /* 5. 回归：ai_news_get_by_date_ledger + csilk_json_copy + api_ai_news_get_date 无 double-free */
    csilk_db_pool_t* pool = setup_test_db();
    int64_t          uid = 0;
    assert(mf_auth_repo_create(pool, "news_tester", "hash_pw_123456", &uid) == 0 && uid > 0);
    char uid_s[32];
    snprintf(uid_s, sizeof(uid_s), "%lld", (long long)uid);
    int64_t lid = ledger_get_default(pool, uid);
    assert(lid > 0);
    char lid_s[32];
    snprintf(lid_s, sizeof(lid_s), "%lld", (long long)lid);

    assert(csilk_db_exec_param(
               pool,
               "INSERT INTO ai_news_reports (user_id, ledger_id, report_date, title, summary, "
               "status) VALUES (?, ?, '2026-11-27', '每日财经简报', '市场平稳', 'done')",
               (const char*[]){uid_s, lid_s, NULL}) == 0);

    /* 5a. Service + deep-copy ownership transfer check */
    csilk_json_t* result = ai_news_get_by_date_ledger(pool, uid, lid, "2026-11-27");
    assert(result != NULL && csilk_json_array_size(result) == 1);
    csilk_json_t* copy = csilk_json_copy(csilk_json_array_get(result, 0));
    csilk_json_free(result);
    assert(copy != NULL);
    assert(strcmp(csilk_json_get_string(copy, "title"), "每日财经简报") == 0);
    respond_ok(NULL, copy);

    /* 5b. Direct controller path: GET /api/ai/news/:date -> api_ai_news_get_date */
    signal(SIGPIPE, SIG_IGN);
    csilk_json_t* urows = csilk_db_query_param_json(
        pool, "SELECT token_version FROM users WHERE id=?", (const char*[]){uid_s, NULL});
    assert(urows && csilk_json_array_size(urows) == 1);
    int tv = (int)db_get_int(csilk_json_array_get(urows, 0), "token_version");
    csilk_json_free(urows);

    char* jwt = jwt_generate_token(NULL, uid, tv);
    assert(jwt != NULL);

    news_http_srv_t h;
    start_news_http_srv(&h);

    char req[1024];
    snprintf(req,
             sizeof(req),
             "GET /api/ai/news/2026-11-27 HTTP/1.1\r\n"
             "Host: localhost\r\n"
             "Authorization: Bearer %s\r\n"
             "Connection: close\r\n\r\n",
             jwt);
    char    resp[4096] = {0};
    ssize_t rlen = http_exchange(h.port, req, resp, sizeof(resp));
    assert(rlen > 0);
    assert(strstr(resp, "HTTP/1.1 200") != NULL);
    assert(strstr(resp, "\"code\":0") != NULL);
    assert(strstr(resp, "每日财经简报") != NULL);

    stop_news_http_srv(&h);
    free(jwt);

    unlink(TEST_DB_PATH);
    {
        char shm[256], wal[256];
        snprintf(shm, sizeof(shm), "%s-shm", TEST_DB_PATH);
        snprintf(wal, sizeof(wal), "%s-wal", TEST_DB_PATH);
        unlink(shm);
        unlink(wal);
    }

    printf("PASS: test_ai_news_privacy completed successfully\n");
    return 0;
}
