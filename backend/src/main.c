#include "csilk/app/app.h"
#include "csilk/app/admin.h"
#include "common/db.h"
#include "config/key_manager.h"
#include "middlewares/jwt_middleware.h"
#include "middlewares/cors_middleware.h"
#include "middlewares/csrf_middleware.h"
#include "middlewares/security_headers_middleware.h"
#include "middlewares/rate_limit.h"
#include "controllers/auth_controller.h"
#include "controllers/admin_controller.h"
#include "controllers/category_controller.h"
#include "interfaces/http/controllers/asset_controller.h"
#include "interfaces/http/controllers/transaction_controller.h"
#include "controllers/daily_expense_controller.h"
#include "controllers/tag_controller.h"
#include "controllers/transfer_controller.h"
#include "controllers/report_controller.h"
#include "controllers/ai_controller.h"
#include "controllers/file_controller.h"
#include "controllers/ai_trace_controller.h"
#include "controllers/import_export_controller.h"
#include "interfaces/http/controllers/market_controller.h"
#include "controllers/dca_controller.h"
#include "controllers/cashflow_controller.h"
#include "controllers/ledger_controller.h"
#include "controllers/import_rule_controller.h"
#include "controllers/receipt_controller.h"
#include "services/market/market_scheduler.h"
#include "services/ai_service.h"
#include "dtos/request.h"
#include "config/secret.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

static void
metrics_middleware_wrapper(csilk_ctx_t* c)
{
    csilk_metrics_middleware(c, NULL);
}

int
main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    if (!config_secret_is_valid("JWT_SECRET")) {
        fprintf(stderr,
                "FATAL: JWT_SECRET is required and must not be empty or a forbidden default "
                "placeholder\n");
        return 1;
    }

    csilk_db_pool_t* pool;
    if (db_init(&pool) != 0) {
        fprintf(stderr, "Failed to initialize database\n");
        return 1;
    }
    if (db_run_migrations(pool) != 0) {
        fprintf(stderr, "Failed to run migrations\n");
        csilk_db_pool_free(pool);
        return 1;
    }
    printf("Database initialized and migrations applied.\n");
    CSILK_LOG_I("Server starting: %d worker(s) on port 8080", 2);

    if (auth_key_init() != 0) {
        fprintf(stderr, "Failed to initialize RSA key pair\n");
        csilk_db_pool_free(pool);
        return 1;
    }
    ai_init(pool);
    printf("RSA-2048 key pair generated for password encryption.\n");

    csilk_app_t* app = csilk_app_new(NULL);
    if (!app) {
        fprintf(stderr, "Failed to create app\n");
        csilk_db_pool_free(pool);
        return 1;
    }

    csilk_app_use(app, csilk_recovery_handler);
    csilk_app_use(app, csilk_logger_handler);
    csilk_app_use(app, csilk_request_id_middleware);
    csilk_app_use(app, metrics_middleware_wrapper);
    /* Security headers on every response */
    csilk_app_use(app, security_headers_middleware);
    /* Rate-limit auth-write endpoints (login/register/setup) before JWT check */
    csilk_app_use(app, rate_limit_auth_middleware);
    csilk_app_use(app, cors_middleware_wrapper);

    csilk_app_get(app, "/healthz", csilk_health_check_handler);
    csilk_app_options(app, "/api/*path", cors_preflight_handler);

    // JWT middleware (rejects if JWT_SECRET is not set)
    csilk_app_use_group(app, "/api", jwt_middleware_wrapper);
    if (config_env_get("ENABLE_CSRF", NULL, 0, NULL)) {
        csilk_app_use_group(app, "/api", csrf_middleware_wrapper);
    }

    // Domain route registration
    register_auth_routes(app);
    register_admin_routes(app);
    register_category_routes(app);
    register_asset_routes(app);
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
    csilk_admin_serve_secure(app, "/csilk-admin", NULL);

    const char* dist = "./frontend/dist";
    if (access(dist, F_OK) != 0) {
        if (access("../frontend/dist", F_OK) == 0) {
            dist = "../frontend/dist";
        } else if (access("dist", F_OK) == 0) {
            dist = "dist";
        }
    }
    csilk_app_static(app, "/", dist);

    market_scheduler_start(pool);

    const char* port_str =
        config_env_get("PORT", NULL, 0, config_env_get("MINEFOLIO_PORT", NULL, 0, "8080"));
    int port = port_str ? atoi(port_str) : 8080;
    printf("Starting Minefolio server on :%d\n", port);
    csilk_app_run(app, port);

    market_scheduler_stop();
    csilk_app_free(app);
    ai_shutdown();
    csilk_db_pool_free(pool);
    return 0;
}
