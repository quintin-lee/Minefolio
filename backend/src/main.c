#include "csilk/app/app.h"
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
#include "controllers/asset_controller.h"
#include "controllers/transaction_controller.h"
#include "controllers/daily_expense_controller.h"
#include "controllers/tag_controller.h"
#include "controllers/transfer_controller.h"
#include "controllers/report_controller.h"
#include "controllers/import_export_controller.h"
#include "dtos/request.h"
#include "dtos/response.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    if (!getenv("MINEFOLIO_JWT_SECRET")) {
        fprintf(stderr, "FATAL: MINEFOLIO_JWT_SECRET environment variable is required\n");
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

    if (auth_key_init() != 0) {
        fprintf(stderr, "Failed to initialize RSA key pair\n");
        csilk_db_pool_free(pool);
        return 1;
    }
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
    /* Security headers on every response */
    csilk_app_use(app, security_headers_middleware);
    /* Rate-limit auth-write endpoints (login/register/setup) before JWT check */
    csilk_app_use(app, rate_limit_auth_middleware);
    csilk_app_use(app, cors_middleware_wrapper);

    csilk_app_get(app, "/healthz", csilk_health_check_handler);
    csilk_app_options(app, "/api/*path", cors_preflight_handler);

    // JWT middleware (rejects if MINEFOLIO_JWT_SECRET is not set)
    csilk_app_use_group(app, "/api", jwt_middleware_wrapper);
    if (getenv("MINEFOLIO_ENABLE_CSRF"))
        csilk_app_use_group(app, "/api", csrf_middleware_wrapper);

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

    csilk_app_static(app, "/", "./frontend/dist");

    printf("Starting Minefolio server on :8080\n");
    csilk_app_run(app, 8080);

    csilk_app_free(app);
    csilk_db_pool_free(pool);
    return 0;
}
