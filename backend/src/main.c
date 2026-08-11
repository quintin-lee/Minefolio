#include "csilk/app/app.h"
#include "common/db.h"
#include "common/response.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Handler forward declarations
extern void auth_register(csilk_ctx_t* c);
extern void auth_login(csilk_ctx_t* c);
extern void auth_me(csilk_ctx_t* c);
extern void categories_list(csilk_ctx_t* c);
extern void categories_create(csilk_ctx_t* c);
extern void categories_update(csilk_ctx_t* c);
extern void categories_delete(csilk_ctx_t* c);
extern void assets_list(csilk_ctx_t* c);
extern void assets_create(csilk_ctx_t* c);
extern void assets_update(csilk_ctx_t* c);
extern void assets_delete(csilk_ctx_t* c);
extern void assets_detail(csilk_ctx_t* c);
extern void transactions_list(csilk_ctx_t* c);
extern void transactions_create(csilk_ctx_t* c);
extern void transactions_update(csilk_ctx_t* c);
extern void transactions_delete(csilk_ctx_t* c);
extern void daily_expenses_list(csilk_ctx_t* c);
extern void daily_expenses_create(csilk_ctx_t* c);
extern void daily_expenses_update(csilk_ctx_t* c);
extern void daily_expenses_delete(csilk_ctx_t* c);
extern void daily_expenses_monthly(csilk_ctx_t* c);
extern void tags_list(csilk_ctx_t* c);
extern void tags_create(csilk_ctx_t* c);
extern void tags_update(csilk_ctx_t* c);
extern void tags_delete(csilk_ctx_t* c);
extern void tags_suggestions(csilk_ctx_t* c);
extern void transfers_create(csilk_ctx_t* c);
extern void report_expense_monthly(csilk_ctx_t* c);
extern void report_expense_trend(csilk_ctx_t* c);
extern void report_expense_category(csilk_ctx_t* c);
extern void report_expense_tag(csilk_ctx_t* c);
extern void report_asset_trend(csilk_ctx_t* c);
extern void report_asset_breakdown(csilk_ctx_t* c);
extern void report_transaction_performance(csilk_ctx_t* c);
extern void report_asset_summary(csilk_ctx_t* c);


// JWT middleware wrapper (new API: no extra args)
static void jwt_middleware_wrapper(csilk_ctx_t* c) {
    const char* path = csilk_get_path(c);
    // Public auth endpoints do not require a token
    if (path && (strcmp(path, "/api/auth/login") == 0 ||
                 strcmp(path, "/api/auth/register") == 0)) {
        return;
    }
    const char* secret = getenv("MINEFOLIO_JWT_SECRET");
    if (!secret) secret = "minefolio-dev-secret-change-in-production";
    csilk_jwt_middleware(c, secret);
}

// CORS middleware wrapper (captures config)
static void cors_middleware_wrapper(csilk_ctx_t* c) {
    csilk_cors_config_t cors = {0};
    cors.allow_origin = "*";
    cors.allow_methods = "GET,POST,PUT,DELETE,OPTIONS";
    cors.allow_headers = "Content-Type,Authorization,X-CSRF-Token";
    cors.allow_credentials = 1;
    csilk_cors_middleware(c, &cors);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    // Initialize database
    csilk_db_pool_t* pool;
    if (db_init(&pool) != 0) {
        fprintf(stderr, "Failed to initialize database\n");
        return 1;
    }

    // Run migrations
    if (db_run_migrations(pool) != 0) {
        fprintf(stderr, "Failed to run migrations\n");
        csilk_db_pool_free(pool);
        return 1;
    }
    printf("Database initialized and migrations applied.\n");

    // Create app
    csilk_app_t* app = csilk_app_new(NULL);
    if (!app) {
        fprintf(stderr, "Failed to create app\n");
        csilk_db_pool_free(pool);
        return 1;
    }

    // Server-level middleware
    csilk_app_use(app, csilk_recovery_handler);
    csilk_app_use(app, csilk_logger_handler);
    csilk_app_use(app, csilk_request_id_middleware);

    csilk_app_use(app, cors_middleware_wrapper);

    // Health check (public)
    csilk_app_get(app, "/healthz", csilk_health_check_handler);

    // Auth routes (public)
    csilk_app_post(app, "/api/auth/register", auth_register);
    csilk_app_post(app, "/api/auth/login", auth_login);

    // API group (requires JWT + CSRF)
    const char* jwt_secret = getenv("MINEFOLIO_JWT_SECRET");
    if (!jwt_secret) jwt_secret = "minefolio-dev-secret-change-in-production";

    csilk_app_use_group(app, "/api", jwt_middleware_wrapper);

    // Auth
    csilk_app_get(app, "/api/auth/me", auth_me);

    // Categories
    csilk_app_get(app, "/api/categories", categories_list);
    csilk_app_post(app, "/api/categories", categories_create);
    csilk_app_put(app, "/api/categories/:id", categories_update);
    csilk_app_delete(app, "/api/categories/:id", categories_delete);

    // Assets
    csilk_app_get(app, "/api/assets", assets_list);
    csilk_app_post(app, "/api/assets", assets_create);
    csilk_app_put(app, "/api/assets/:id", assets_update);
    csilk_app_delete(app, "/api/assets/:id", assets_delete);
    csilk_app_get(app, "/api/assets/:id", assets_detail);

    // Transactions
    csilk_app_get(app, "/api/transactions", transactions_list);
    csilk_app_post(app, "/api/transactions", transactions_create);
    csilk_app_put(app, "/api/transactions/:id", transactions_update);
    csilk_app_delete(app, "/api/transactions/:id", transactions_delete);

    // Daily expenses
    csilk_app_get(app, "/api/daily-expenses", daily_expenses_list);
    csilk_app_post(app, "/api/daily-expenses", daily_expenses_create);
    csilk_app_put(app, "/api/daily-expenses/:id", daily_expenses_update);
    csilk_app_delete(app, "/api/daily-expenses/:id", daily_expenses_delete);
    csilk_app_get(app, "/api/daily-expenses/monthly", daily_expenses_monthly);

    // Tags
    csilk_app_get(app, "/api/tags", tags_list);
    csilk_app_post(app, "/api/tags", tags_create);
    csilk_app_put(app, "/api/tags/:id", tags_update);
    csilk_app_delete(app, "/api/tags/:id", tags_delete);
    csilk_app_get(app, "/api/tags/suggestions", tags_suggestions);

    // Transfers
    csilk_app_post(app, "/api/transfers", transfers_create);

    // Reports
    csilk_app_get(app, "/api/reports/expense/monthly", report_expense_monthly);
    csilk_app_get(app, "/api/reports/expense/trend", report_expense_trend);
    csilk_app_get(app, "/api/reports/expense/category", report_expense_category);
    csilk_app_get(app, "/api/reports/expense/tag", report_expense_tag);
    csilk_app_get(app, "/api/reports/asset/trend", report_asset_trend);
    csilk_app_get(app, "/api/reports/asset/breakdown", report_asset_breakdown);
    csilk_app_get(app, "/api/reports/transaction/performance", report_transaction_performance);
    csilk_app_get(app, "/api/reports/asset/summary", report_asset_summary);

    // Static files (frontend build output)
    csilk_app_static(app, "/", "./frontend/dist");

    printf("Starting Minefolio server on :8080\n");
    csilk_app_run(app, 8080);

    csilk_app_free(app);
    csilk_db_pool_free(pool);
    return 0;
}
