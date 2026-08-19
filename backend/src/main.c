#include "csilk/app/app.h"
#include "common/db.h"
#include "common/response.h"
#include "auth_key.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Handler forward declarations
extern void auth_register(csilk_ctx_t* c);
extern void auth_login(csilk_ctx_t* c);
extern void auth_me(csilk_ctx_t* c);
extern void auth_change_password(csilk_ctx_t* c);
extern void auth_public_key(csilk_ctx_t* c);
extern void categories_list(csilk_ctx_t* c);
extern void categories_create(csilk_ctx_t* c);
extern void categories_update(csilk_ctx_t* c);
extern void categories_delete(csilk_ctx_t* c);
extern void categories_children(csilk_ctx_t* c);
extern void assets_list(csilk_ctx_t* c);
extern void assets_create(csilk_ctx_t* c);
extern void assets_update(csilk_ctx_t* c);
extern void assets_delete(csilk_ctx_t* c);
extern void assets_detail(csilk_ctx_t* c);
extern void transactions_list(csilk_ctx_t* c);
extern void transactions_monthly(csilk_ctx_t* c);
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
extern void report_expense_yearly(csilk_ctx_t* c);
extern void report_expense_category(csilk_ctx_t* c);
extern void report_expense_tag(csilk_ctx_t* c);
extern void report_asset_trend(csilk_ctx_t* c);
extern void report_asset_breakdown(csilk_ctx_t* c);
extern void report_transaction_performance(csilk_ctx_t* c);
extern void report_holdings(csilk_ctx_t* c);
extern void report_asset_summary(csilk_ctx_t* c);
extern void summary_get(csilk_ctx_t* c);
extern void asset_logs_list(csilk_ctx_t* c);
extern void system_status(csilk_ctx_t* c);
extern void system_setup(csilk_ctx_t* c);
extern void transactions_export_csv(csilk_ctx_t* c);
extern void transactions_import_csv(csilk_ctx_t* c);
extern void daily_expenses_export_csv(csilk_ctx_t* c);
extern void daily_expenses_import_csv(csilk_ctx_t* c);


// JWT middleware wrapper (new API: no extra args)
static void jwt_middleware_wrapper(csilk_ctx_t* c) {
    const char* path = csilk_get_path(c);
    // Public auth endpoints do not require a token
    if (path && (strcmp(path, "/api/auth/login") == 0 ||
                 strcmp(path, "/api/auth/register") == 0 ||
                 strcmp(path, "/api/system/status") == 0 ||
                 strcmp(path, "/api/system/setup") == 0)) {
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

// Explicit OPTIONS handler for CORS preflight requests.
// csilk only runs middleware on matched routes, so we need a wildcard
// OPTIONS route to ensure preflight requests don't get a 404.
static void cors_preflight_handler(csilk_ctx_t* c) {
    csilk_cors_config_t cors = {0};
    cors.allow_origin = "*";
    cors.allow_methods = "GET,POST,PUT,DELETE,OPTIONS";
    cors.allow_headers = "Content-Type,Authorization,X-CSRF-Token";
    cors.allow_credentials = 1;
    csilk_cors_middleware(c, &cors);
}

// CSRF middleware wrapper — stateless double-submit cookie.
// Safe methods (GET/HEAD/OPTIONS) ensure a JS-readable csrf_token cookie exists;
// state-changing methods require the X-CSRF-Token header to match that cookie.
// Env-gated via MINEFOLIO_ENABLE_CSRF so cross-origin dev (5173 -> 8080, no
// withCredentials) is unaffected while prod (nginx same-origin) is protected.
static void csrf_middleware_wrapper(csilk_ctx_t* c) {
    const char* method = csilk_get_method(c);
    if (!method) { csilk_next(c); return; }

    // Public auth bootstrap endpoints are exempt (no cookie exists yet on first use)
    const char* path = csilk_get_path(c);
    if (path && (strcmp(path, "/api/auth/login") == 0 ||
                 strcmp(path, "/api/auth/register") == 0 ||
                 strcmp(path, "/api/system/status") == 0 ||
                 strcmp(path, "/api/system/setup") == 0)) {
        csilk_next(c);
        return;
    }

    if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0 ||
        strcmp(method, "OPTIONS") == 0) {
        // Ensure the SPA has a token it can echo back
        if (!csilk_get_cookie(c, "csrf_token")) {
            char buf[33];
            if (csilk_csrf_generate_token(buf, sizeof(buf)) == 0) {
                // secure=0, http_only=0 so JS can read via document.cookie
                csilk_set_cookie(c, "csrf_token", buf, 86400, "/", NULL, 0, 0);
            }
        }
        csilk_next(c);
        return;
    }

    const char* token = csilk_get_header(c, "X-CSRF-Token");
    const char* cookie = csilk_get_cookie(c, "csrf_token");
    if (!token || !cookie || strcmp(token, cookie) != 0) {
        csilk_json_error(c, CSILK_STATUS_FORBIDDEN, "Forbidden: Invalid CSRF token");
        csilk_abort(c);
        return;
    }
    csilk_next(c);
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

    // Generate RSA key pair for password encryption in transit
    if (auth_key_init() != 0) {
        fprintf(stderr, "Failed to initialize RSA key pair\n");
        csilk_db_pool_free(pool);
        return 1;
    }
    printf("RSA-2048 key pair generated for password encryption.\n");

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

    // CORS preflight: catch-all OPTIONS for /api/* so browsers can
    // complete the preflight handshake before POST/PUT/DELETE requests.
    csilk_app_options(app, "/api/*path", cors_preflight_handler);

    // System setup & Auth (public)
    csilk_app_get_ext(app, "/api/system/status", system_status, nullptr, nullptr,
                      "System status", "Returns initialization status and user count");
    csilk_app_post_ext(app, "/api/system/setup", system_setup,
                       nullptr, nullptr, "Initialize system",
                       "Seed the database with default categories for the first admin user");
    csilk_app_post_ext(app, "/api/auth/register", auth_register,
                       nullptr, nullptr, "Register admin",
                       "Create the first admin user (only allowed before system initialization)");
    csilk_app_post_ext(app, "/api/auth/login", auth_login,
                       nullptr, nullptr, "Login",
                       "Authenticate with username and RSA-encrypted password, returns JWT token");
    csilk_app_get_ext(app, "/api/auth/public-key", auth_public_key,
                      nullptr, nullptr, "Get public key",
                      "Returns the RSA public key PEM for client-side password encryption");

    // API group (requires JWT + CSRF)
    const char* jwt_secret = getenv("MINEFOLIO_JWT_SECRET");
    if (!jwt_secret) jwt_secret = "minefolio-dev-secret-change-in-production";

    csilk_app_use_group(app, "/api", jwt_middleware_wrapper);

    // CSRF (optional, enabled via MINEFOLIO_ENABLE_CSRF) — stateless double-submit
    if (getenv("MINEFOLIO_ENABLE_CSRF")) {
        csilk_app_use_group(app, "/api", csrf_middleware_wrapper);
    }

    // Auth
    csilk_app_get_ext(app, "/api/auth/me", auth_me, nullptr, nullptr,
                      "Get current user profile", "Returns the authenticated user's profile");
    csilk_app_put_ext(app, "/api/auth/password", auth_change_password,
                      nullptr, nullptr, "Change password",
                      "Update the current user's password using encrypted old/new values");

    // Categories
    csilk_app_get_ext(app, "/api/categories", categories_list, nullptr, nullptr,
                      "List categories", "Returns all categories owned by the current user");
    csilk_app_post_ext(app, "/api/categories", categories_create,
                       nullptr, nullptr, "Create category",
                       "Create a new expense/income/asset/transaction category");
    csilk_app_put_ext(app, "/api/categories/:id", categories_update,
                      nullptr, nullptr, "Update category",
                      "Update an existing category by ID");
    csilk_app_delete_ext(app, "/api/categories/:id", categories_delete,
                         nullptr, nullptr, "Delete category",
                         "Delete a category and its children by ID");
    csilk_app_get_ext(app, "/api/categories/:id/children", categories_children,
                      nullptr, nullptr, "List category children",
                      "Returns immediate child categories of the given category");

    // Assets
    csilk_app_get_ext(app, "/api/assets", assets_list, nullptr, nullptr,
                      "List assets", "Returns paginated list of user's assets with optional category filter");
    csilk_app_post_ext(app, "/api/assets", assets_create,
                       nullptr, nullptr, "Create asset",
                       "Create a new asset (cash, investment, liability, etc.)");
    csilk_app_put_ext(app, "/api/assets/:id", assets_update,
                      nullptr, nullptr, "Update asset",
                      "Update an existing asset by ID; investment assets recalculate position on net_value change");
    csilk_app_delete_ext(app, "/api/assets/:id", assets_delete,
                         nullptr, nullptr, "Delete asset",
                         "Delete an asset and its associated transactions by ID");
    csilk_app_get_ext(app, "/api/assets/:id", assets_detail,
                      nullptr, nullptr, "Get asset detail",
                      "Returns full asset details including linked transaction history");

    // Transactions
    csilk_app_get_ext(app, "/api/transactions", transactions_list, nullptr, nullptr,
                      "List transactions", "Returns paginated transaction list with optional filters");
    csilk_app_get_ext(app, "/api/transactions/monthly", transactions_monthly,
                      nullptr, nullptr, "Monthly transaction summary",
                      "Returns monthly aggregated transaction totals");
    csilk_app_post_ext(app, "/api/transactions", transactions_create,
                       nullptr, nullptr, "Create transaction",
                       "Create a new transaction (expense, income, transfer, investment buy/sell)");
    csilk_app_put_ext(app, "/api/transactions/:id", transactions_update,
                      nullptr, nullptr, "Update transaction",
                      "Update an existing transaction by ID");
    csilk_app_delete_ext(app, "/api/transactions/:id", transactions_delete,
                         nullptr, nullptr, "Delete transaction",
                         "Delete a transaction by ID");
    csilk_app_get_ext(app, "/api/export/transactions", transactions_export_csv,
                      nullptr, nullptr, "Export transactions as CSV",
                      "Downloads all user transactions as a CSV file");
    csilk_app_post_ext(app, "/api/import/transactions", transactions_import_csv,
                       nullptr, nullptr, "Import transactions from CSV",
                       "Imports transactions from an uploaded CSV file");

    // Daily expenses
    csilk_app_get_ext(app, "/api/daily-expenses", daily_expenses_list, nullptr, nullptr,
                      "List daily expenses", "Returns paginated list of daily expense records");
    csilk_app_post_ext(app, "/api/daily-expenses", daily_expenses_create,
                       nullptr, nullptr, "Create daily expense",
                       "Create a new daily expense entry with optional tags");
    csilk_app_put_ext(app, "/api/daily-expenses/:id", daily_expenses_update,
                      nullptr, nullptr, "Update daily expense",
                      "Update an existing daily expense record by ID");
    csilk_app_delete_ext(app, "/api/daily-expenses/:id", daily_expenses_delete,
                         nullptr, nullptr, "Delete daily expense",
                         "Delete a daily expense record by ID");
    csilk_app_get_ext(app, "/api/daily-expenses/monthly", daily_expenses_monthly,
                      nullptr, nullptr, "Monthly daily expenses",
                      "Returns monthly aggregated daily expense data");
    csilk_app_get_ext(app, "/api/export/daily-expenses", daily_expenses_export_csv,
                      nullptr, nullptr, "Export daily expenses as CSV",
                      "Downloads all daily expenses as a CSV file");
    csilk_app_post_ext(app, "/api/import/daily-expenses", daily_expenses_import_csv,
                       nullptr, nullptr, "Import daily expenses from CSV",
                       "Imports daily expenses from an uploaded CSV file");

    // Tags
    csilk_app_get_ext(app, "/api/tags", tags_list, nullptr, nullptr,
                      "List tags", "Returns all tags owned by the current user");
    csilk_app_post_ext(app, "/api/tags", tags_create,
                       nullptr, nullptr, "Create tag",
                       "Create a new tag with optional color");
    csilk_app_put_ext(app, "/api/tags/:id", tags_update,
                      nullptr, nullptr, "Update tag",
                      "Update an existing tag by ID");
    csilk_app_delete_ext(app, "/api/tags/:id", tags_delete,
                         nullptr, nullptr, "Delete tag",
                         "Delete a tag by ID");
    csilk_app_get_ext(app, "/api/tags/suggestions", tags_suggestions,
                      nullptr, nullptr, "Tag suggestions",
                      "Returns tag name suggestions for autocomplete (query param: q)");

    // Transfers
    csilk_app_post_ext(app, "/api/transfers", transfers_create,
                       nullptr, nullptr, "Create transfer",
                       "Create a transfer between two assets (debit one, credit other)");

    // Reports
    csilk_app_get_ext(app, "/api/reports/expense/monthly", report_expense_monthly,
                      nullptr, nullptr, "Monthly expense report",
                      "Returns monthly income/expense breakdown by category and tag");
    csilk_app_get_ext(app, "/api/reports/expense/trend", report_expense_trend,
                      nullptr, nullptr, "Expense trend",
                      "Returns expense trend over N months (query param: months)");
    csilk_app_get_ext(app, "/api/reports/expense/yearly", report_expense_yearly,
                      nullptr, nullptr, "Yearly expense report",
                      "Returns yearly expense summary grouped by month");
    csilk_app_get_ext(app, "/api/reports/expense/category", report_expense_category,
                      nullptr, nullptr, "Expense by category",
                      "Returns expense totals grouped by category");
    csilk_app_get_ext(app, "/api/reports/expense/tag", report_expense_tag,
                      nullptr, nullptr, "Expense by tag",
                      "Returns expense totals grouped by tag");
    csilk_app_get_ext(app, "/api/reports/asset/trend", report_asset_trend,
                      nullptr, nullptr, "Asset value trend",
                      "Returns asset value trend over time (query param: months)");
    csilk_app_get_ext(app, "/api/reports/asset/breakdown", report_asset_breakdown,
                      nullptr, nullptr, "Asset breakdown",
                      "Returns asset allocation breakdown by category");
    csilk_app_get_ext(app, "/api/reports/transaction/performance",
                      report_transaction_performance, nullptr, nullptr,
                      "Transaction performance", "Returns investment transaction performance/PnL report");
    csilk_app_get_ext(app, "/api/reports/holdings", report_holdings,
                      nullptr, nullptr, "Portfolio holdings",
                      "Returns current portfolio holdings for investment assets");
    csilk_app_get_ext(app, "/api/reports/asset/summary", report_asset_summary,
                      nullptr, nullptr, "Asset summary",
                      "Returns aggregated asset summary including net worth");

    // Summary (dashboard aggregate)
    csilk_app_get_ext(app, "/api/summary", summary_get,
                      nullptr, nullptr, "Dashboard summary",
                      "Returns dashboard aggregate: net worth, monthly income/expense, recent transactions");

    csilk_app_get_ext(app, "/api/asset-balance-logs", asset_logs_list,
                      nullptr, nullptr, "Asset balance logs",
                      "Returns paginated asset balance change logs with optional asset_id filter");

    // Static files (frontend build output)
    csilk_app_static(app, "/", "./frontend/dist");

    printf("Starting Minefolio server on :8080\n");
    csilk_app_run(app, 8080);

    csilk_app_free(app);
    csilk_db_pool_free(pool);
    return 0;
}
