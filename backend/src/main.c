#include "csilk/app/app.h"
#include "csilk/app/admin.h"
#include "common/db.h"
#include "config/key_manager.h"
#include "middlewares/jwt_middleware.h"
#include "middlewares/cors_middleware.h"
#include "middlewares/csrf_middleware.h"
#include "middlewares/security_headers_middleware.h"
#include "middlewares/rate_limit.h"
#include "interfaces/http/controllers/auth_controller.h"
#include "interfaces/http/controllers/admin_controller.h"
#include "interfaces/http/controllers/category_controller.h"
#include "interfaces/http/controllers/asset_controller.h"
#include "interfaces/http/controllers/account_controller.h"
#include "interfaces/http/controllers/daily_expense_controller.h"
#include "interfaces/http/controllers/tag_controller.h"
#include "interfaces/http/controllers/transfer_controller.h"
#include "interfaces/http/controllers/transaction_controller.h"
#include "interfaces/http/controllers/report_controller.h"
#include "interfaces/http/controllers/ai_controller.h"
#include "interfaces/http/controllers/file_controller.h"
#include "interfaces/http/controllers/ai_trace_controller.h"
#include "interfaces/http/controllers/import_export_controller.h"
#include "interfaces/http/controllers/market_controller.h"
#include "interfaces/http/controllers/dca_controller.h"
#include "interfaces/http/controllers/cashflow_controller.h"
#include "interfaces/http/controllers/ledger_controller.h"
#include "interfaces/http/controllers/import_rule_controller.h"
#include "interfaces/http/controllers/receipt_controller.h"
#include "interfaces/http/controllers/ai_mcp_controller.h"
#include "interfaces/http/controllers/ai_news_controller.h"
#include "interfaces/http/controllers/im_bot_controller.h"
#include "interfaces/http/controllers/im_bot_instance_controller.h"
#include "interfaces/http/controllers/mcp_server_controller.h"
#include "interfaces/http/controllers/skill_controller.h"
#include "interfaces/http/controllers/scheduler_controller.h"
#include "interfaces/http/controllers/task_controller.h"
#include "services/market/market_scheduler.h"
#include "core/ledger/ledger_engine.h"
#include "core/scheduler/scheduler_engine.h"
#include "application/scheduler/usecases.h"
#include "application/auth/usecases.h"
#include "services/ai_service.h"
#include "dtos/request.h"
#include "config/secret.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <curl/curl.h>

static void
metrics_middleware_wrapper(csilk_ctx_t* c)
{
    csilk_metrics_middleware(c, NULL);
}

static csilk_app_t* g_app = NULL;
static int          g_sig_pipe[2] = {-1, -1};
static atomic_bool  g_watcher_stop = false;

static void
handle_shutdown_signal(int sig)
{
    int           saved_errno = errno;
    unsigned char b = (unsigned char)sig;
    if (g_sig_pipe[1] >= 0) {
        ssize_t n = write(g_sig_pipe[1], &b, 1);
        (void)n;
    }
    errno = saved_errno;
}

static int
create_sig_pipe(void)
{
    if (pipe(g_sig_pipe) != 0) {
        return -1;
    }
    for (int i = 0; i < 2; i++) {
        int flags = fcntl(g_sig_pipe[i], F_GETFL, 0);
        if (flags == -1 || fcntl(g_sig_pipe[i], F_SETFL, flags | O_NONBLOCK) == -1) {
            close(g_sig_pipe[0]);
            close(g_sig_pipe[1]);
            g_sig_pipe[0] = -1;
            g_sig_pipe[1] = -1;
            return -1;
        }
        int fd_flags = fcntl(g_sig_pipe[i], F_GETFD, 0);
        if (fd_flags != -1) {
            (void)fcntl(g_sig_pipe[i], F_SETFD, fd_flags | FD_CLOEXEC);
        }
    }
    return 0;
}

static void*
shutdown_watcher_thread(void* arg)
{
    (void)arg;
    while (!atomic_load(&g_watcher_stop)) {
        struct pollfd pfd = {.fd = g_sig_pipe[0], .events = POLLIN, .revents = 0};
        int           rc = poll(&pfd, 1, -1);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (rc > 0 && (pfd.revents & POLLIN)) {
            char    buf[64];
            ssize_t r;
            bool    got_byte = false;
            while ((r = read(g_sig_pipe[0], buf, sizeof(buf))) > 0) {
                got_byte = true;
            }
            if (atomic_load(&g_watcher_stop)) {
                break;
            }
            if (got_byte && g_app) {
                csilk_server_t* server = csilk_app_server(g_app);
                if (server) {
                    csilk_server_stop(server);
                }
                break;
            }
        }
        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            break;
        }
    }
    return NULL;
}

int
main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    signal(SIGPIPE, SIG_IGN);

    /* 容器启动前维护子命令：自愈已应用迁移的 checksum 漂移，不依赖 psql。
       此路径在 JWT / RSA 初始化之前执行，仅需要数据库环境变量。 */
    if (argc >= 2 && strcmp(argv[1], "--heal-migration-checksums") == 0) {
        csilk_db_pool_t* heal_pool = NULL;
        if (db_init(&heal_pool) != 0 || !heal_pool) {
            fprintf(stderr, "Failed to initialize database for checksum heal\n");
            return 1;
        }
        int rc = db_heal_migration_checksums(heal_pool);
        csilk_db_pool_free(heal_pool);
        return rc == 0 ? 0 : 1;
    }

    /* libcurl 全局初始化必须在任何线程创建前显式完成：
       csilk worker/market_scheduler/AI summary/MCP 均调用 curl_easy_*，
       若依赖首次 lazy init 会形成数据竞争。 */
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "FATAL: curl_global_init failed\n");
        return 1;
    }

    if (!config_secret_is_valid("JWT_SECRET")) {
        fprintf(stderr,
                "FATAL: JWT_SECRET is required and must not be empty or a forbidden default "
                "placeholder\n");
        curl_global_cleanup();
        return 1;
    }

    csilk_db_pool_t* pool;
    if (db_init(&pool) != 0) {
        fprintf(stderr, "Failed to initialize database\n");
        curl_global_cleanup();
        return 1;
    }
    if (db_run_migrations(pool) != 0) {
        fprintf(stderr, "Failed to run migrations\n");
        csilk_db_pool_free(pool);
        curl_global_cleanup();
        return 1;
    }
    printf("Database initialized and migrations applied.\n");

    int         workers = 2;
    const char* workers_env = getenv("MINEFOLIO_WORKERS");
    if (workers_env && workers_env[0]) {
        int parsed_w = atoi(workers_env);
        if (parsed_w >= 1 && parsed_w <= 16) {
            workers = parsed_w;
        }
    }

    if (auth_key_init() != 0) {
        fprintf(stderr, "Failed to initialize RSA key pair\n");
        csilk_db_pool_free(pool);
        curl_global_cleanup();
        return 1;
    }
    ledger_register_balance_hooks();
    ai_init(pool);
    if (task_controller_init(pool, 4) != 0) {
        fprintf(stderr, "Failed to initialize async task manager\n");
        ai_shutdown();
        csilk_db_pool_free(pool);
        curl_global_cleanup();
        return 1;
    }
    printf("RSA-2048 key pair generated for password encryption.\n");

    csilk_app_t* app = csilk_app_new(NULL);
    if (!app) {
        fprintf(stderr, "Failed to create app\n");
        task_controller_shutdown();
        ai_shutdown();
        csilk_db_pool_free(pool);
        curl_global_cleanup();
        return 1;
    }
    {
        csilk_config_t* scfg_wrap = csilk_app_config(app);
        if (scfg_wrap) {
            csilk_server_config_t scfg = scfg_wrap->server;
            scfg.worker_threads = workers;
            csilk_app_set_server_config(app, scfg);
            free(scfg_wrap);
        }
    }

    csilk_app_use(app, csilk_recovery_handler);
    csilk_app_use(app, csilk_logger_handler);
    csilk_app_use(app, csilk_request_id_middleware);
    csilk_app_use(app, metrics_middleware_wrapper);
    /* Security headers on every response */
    csilk_app_use(app, security_headers_middleware);
    /* Rate-limit auth-write endpoints (login/register/setup) + IM bind-auth, before JWT check. */
    csilk_app_use_group(app, "/api", rate_limit_auth_middleware);
    /* CORS_ORIGIN 未设时回 "*" 且禁止凭证（规范禁止 * 与 credentials 共存）；
       生产若需带凭证跨域，必须显式配置 CORS_ORIGIN 白名单。 */
    if (!config_env_get("CORS_ORIGIN", NULL, 0, NULL)) {
        CSILK_LOG_W(
            "CORS_ORIGIN not set: serving Access-Control-Allow-Origin: * without credentials. "
            "Set CORS_ORIGIN to an allow-list origin (e.g. https://app.example.com) to "
            "permit credentialed cross-origin requests.");
    }
    csilk_app_use(app, cors_middleware_wrapper);

    csilk_app_get(app, "/healthz", csilk_health_check_handler);
    csilk_app_options(app, "/api/*path", cors_preflight_handler);
    csilk_app_options(app, "/mcp", cors_preflight_handler);

    // JWT middleware (rejects if JWT_SECRET is not set)
    auth_usecase_set_jwt_invalidator(jwt_middleware_invalidate_user);
    csilk_app_use_group(app, "/api", jwt_middleware_wrapper);
    // /mcp is a separate group (no /api prefix) — run JWT middleware on it too so
    // ctx_user_id() works inside the MCP server handler.
    csilk_app_use_group(app, "/mcp", jwt_middleware_wrapper);
    /* /mcp 每用户限频（Task 4）：注册在 JWT 之后，使 ctx_user_id 可用 */
    csilk_app_use_group(app, "/mcp", mcp_rate_limit_middleware);
    char        csrf_buf[16] = {0};
    const char* csrf_val = config_env_get("ENABLE_CSRF", csrf_buf, sizeof(csrf_buf), NULL);
    if (csrf_val && (strcmp(csrf_val, "1") == 0 || strcasecmp(csrf_val, "true") == 0 ||
                     strcasecmp(csrf_val, "yes") == 0 || strcasecmp(csrf_val, "on") == 0)) {
        csilk_app_use_group(app, "/api", csrf_middleware_wrapper);
    }

    // Domain route registration
    register_auth_routes(app);
    register_admin_routes(app);
    register_category_routes(app);
    register_asset_routes(app);
    register_account_routes(app);
    register_transaction_routes(app);
    register_daily_expense_routes(app);
    register_tag_routes(app);
    register_transfer_routes(app);
    register_report_routes(app);
    register_import_export_routes(app);
    register_ai_routes(app);
    register_file_routes(app);
    register_ai_trace_routes(app);
    register_market_routes(app);
    register_dca_routes(app);
    register_cashflow_routes(app);
    register_ledger_routes(app);
    register_import_rule_routes(app);
    register_receipt_routes(app);
    register_ai_mcp_routes(app);
    register_mcp_server_routes(app);
    register_skill_routes(app);
    register_ai_news_routes(app);
    register_im_bot_routes(app);
    register_im_bot_instance_routes(app);
    register_scheduler_routes(app);
    register_task_routes(app);
    csilk_admin_serve_secure(app, "/csilk-admin", jwt_admin_middleware_wrapper);

    const char* dist = "./frontend/dist";
    if (access(dist, F_OK) != 0) {
        if (access("../frontend/dist", F_OK) == 0) {
            dist = "../frontend/dist";
        } else if (access("dist", F_OK) == 0) {
            dist = "dist";
        }
    }
    csilk_app_static(app, "/", dist);

    /* Register iLink push hooks so the scheduler engine can enqueue outbox rows,
       expire stale entries, and seed built-in jobs without including application headers. */
    scheduler_application_register_push_hooks();

    market_scheduler_start(pool);
    scheduler_engine_start(pool);

    g_app = app;
    pthread_t watcher_thread;
    bool      watcher_started = false;
    atomic_store(&g_watcher_stop, false);
    if (create_sig_pipe() == 0) {
        if (pthread_create(&watcher_thread, NULL, shutdown_watcher_thread, NULL) == 0) {
            watcher_started = true;
        } else {
            CSILK_LOG_W("Failed to start shutdown watcher thread");
            close(g_sig_pipe[0]);
            close(g_sig_pipe[1]);
            g_sig_pipe[0] = -1;
            g_sig_pipe[1] = -1;
        }
    } else {
        CSILK_LOG_W("Failed to create shutdown signal pipe");
    }

    signal(SIGTERM, handle_shutdown_signal);
    signal(SIGINT, handle_shutdown_signal);

    const char* port_str =
        config_env_get("PORT", NULL, 0, config_env_get("MINEFOLIO_PORT", NULL, 0, "8080"));
    int port = port_str ? atoi(port_str) : 8080;
    CSILK_LOG_I("Server starting: %d worker(s) on port %d", workers, port);
    printf("Starting Minefolio server on :%d\n", port);
    csilk_app_run(app, port);

    atomic_store(&g_watcher_stop, true);
    market_scheduler_stop();
    scheduler_engine_stop();
    task_controller_shutdown();
    {
        extern void ai_news_shutdown(void);
        ai_news_shutdown();
    }

    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    if (g_sig_pipe[1] >= 0) {
        unsigned char wake = 0;
        ssize_t       n = write(g_sig_pipe[1], &wake, 1);
        (void)n;
    }
    if (watcher_started) {
        pthread_join(watcher_thread, NULL);
    }
    if (g_sig_pipe[0] >= 0) {
        close(g_sig_pipe[0]);
        g_sig_pipe[0] = -1;
    }
    if (g_sig_pipe[1] >= 0) {
        close(g_sig_pipe[1]);
        g_sig_pipe[1] = -1;
    }
    g_app = NULL;

    csilk_app_free(app);
    ai_shutdown();
    db_wal_checkpoint(pool);
    csilk_db_pool_free(pool);
    curl_global_cleanup();
    return 0;
}
