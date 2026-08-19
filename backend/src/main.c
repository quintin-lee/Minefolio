#include "csilk/app/app.h"
#include "common/db.h"
#include "config/key_manager.h"
#include "middlewares/jwt_middleware.h"
#include "middlewares/cors_middleware.h"
#include "middlewares/csrf_middleware.h"
#include "controllers/auth_controller.h"
#include "controllers/category_controller.h"
#include "controllers/asset_controller.h"
#include "controllers/transaction_controller.h"
#include "controllers/daily_expense_controller.h"
#include "controllers/tag_controller.h"
#include "controllers/transfer_controller.h"
#include "controllers/report_controller.h"
#include "controllers/import_export_controller.h"
#include "repositories/asset_repo.h"
#include "dtos/request.h"
#include "dtos/response.h"
#include <stdio.h>

int main(int argc, char** argv) {
    (void)argc; (void)argv;

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
    csilk_app_use(app, cors_middleware_wrapper);

    csilk_app_get(app, "/healthz", csilk_health_check_handler);
    csilk_app_options(app, "/api/*path", cors_preflight_handler);

    // System (public)
    csilk_app_get_ext(app, "/api/system/status", system_status, nullptr, nullptr,
                      "System status", "Returns initialization status and user count");
    csilk_app_post_ext(app, "/api/system/setup", system_setup,
                       "setup_req_t", "token_resp_t",
                       "Initialize system",
                       "Seed the database with default categories for the first admin user");
    csilk_app_post_ext(app, "/api/auth/register", auth_register,
                       "register_req_t", "token_resp_t",
                       "Register admin",
                       "Create the first admin user (only allowed before system initialization)");
    csilk_app_post_ext(app, "/api/auth/login", auth_login,
                       "login_req_t", "token_resp_t",
                       "Login",
                       "Authenticate with username and RSA-encrypted password, returns JWT token");
    csilk_app_get_ext(app, "/api/auth/public-key", auth_public_key,
                      nullptr, nullptr, "Get public key",
                      "Returns the RSA public key PEM for client-side password encryption");

    // JWT + CSRF group
    const char* jwt_secret = getenv("MINEFOLIO_JWT_SECRET");
    if (!jwt_secret) jwt_secret = "minefolio-dev-secret-change-in-production";
    csilk_app_use_group(app, "/api", jwt_middleware_wrapper);
    if (getenv("MINEFOLIO_ENABLE_CSRF"))
        csilk_app_use_group(app, "/api", csrf_middleware_wrapper);

    // Auth
    csilk_app_get_ext(app, "/api/auth/me", auth_me, nullptr,
                      "user_resp_t", "Get current user profile",
                      "Returns the authenticated user's profile");
    csilk_app_put_ext(app, "/api/auth/password", auth_change_password,
                      "change_pwd_req_t", nullptr,
                      "Change password",
                      "Update the current user's password using encrypted old/new values");

    // Categories
    csilk_app_get_ext(app, "/api/categories", categories_list, nullptr,
                      "category_resp_t", "List categories",
                      "Returns all categories owned by the current user");
    csilk_app_post_ext(app, "/api/categories", categories_create,
                       "category_req_t", "category_resp_t",
                       "Create category",
                       "Create a new expense/income/asset/transaction category");
    csilk_app_put_ext(app, "/api/categories/:id", categories_update,
                      "category_req_t", "category_resp_t",
                      "Update category",
                      "Update an existing category by ID");
    csilk_app_delete_ext(app, "/api/categories/:id", categories_delete,
                         nullptr, nullptr, "Delete category",
                         "Delete a category and its children by ID");
    csilk_app_get_ext(app, "/api/categories/:id/children", categories_children,
                      nullptr, "category_resp_t",
                      "List category children",
                      "Returns immediate child categories of the given category");

    // Assets
    csilk_app_get_ext(app, "/api/assets", assets_list, nullptr,
                      "asset_resp_t", "List assets",
                      "Returns paginated list of user's assets with optional category filter");
    csilk_app_post_ext(app, "/api/assets", assets_create,
                       "asset_req_t", "asset_resp_t",
                       "Create asset",
                       "Create a new asset (cash, investment, liability, etc.)");
    csilk_app_put_ext(app, "/api/assets/:id", assets_update,
                      "asset_req_t", "asset_resp_t",
                      "Update asset",
                      "Update an existing asset by ID; investment assets recalculate position on net_value change");
    csilk_app_delete_ext(app, "/api/assets/:id", assets_delete,
                         nullptr, nullptr, "Delete asset",
                         "Delete an asset and its associated transactions by ID");
    csilk_app_get_ext(app, "/api/assets/:id", assets_detail,
                      nullptr, "asset_resp_t",
                      "Get asset detail",
                      "Returns full asset details including linked transaction history");

    // Transactions
    csilk_app_get_ext(app, "/api/transactions", transactions_list, nullptr,
                      "transaction_resp_t", "List transactions",
                      "Returns paginated transaction list with optional filters");
    csilk_app_get_ext(app, "/api/transactions/monthly", transactions_monthly,
                      nullptr, nullptr, "Monthly transaction summary",
                      "Returns monthly aggregated transaction totals");
    csilk_app_post_ext(app, "/api/transactions", transactions_create,
                       "transaction_req_t", "transaction_resp_t",
                       "Create transaction",
                       "Create a new transaction (expense, income, transfer, investment buy/sell)");
    csilk_app_put_ext(app, "/api/transactions/:id", transactions_update,
                      "transaction_req_t", "transaction_resp_t",
                      "Update transaction",
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
    csilk_app_get_ext(app, "/api/daily-expenses", daily_expenses_list, nullptr,
                      "daily_expense_resp_t", "List daily expenses",
                      "Returns paginated list of daily expense records");
    csilk_app_post_ext(app, "/api/daily-expenses", daily_expenses_create,
                       "daily_expense_req_t", "daily_expense_resp_t",
                       "Create daily expense",
                       "Create a new daily expense entry with optional tags");
    csilk_app_put_ext(app, "/api/daily-expenses/:id", daily_expenses_update,
                      "daily_expense_req_t", "daily_expense_resp_t",
                      "Update daily expense",
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
    csilk_app_get_ext(app, "/api/tags", tags_list, nullptr,
                      "tag_resp_t", "List tags",
                      "Returns all tags owned by the current user");
    csilk_app_post_ext(app, "/api/tags", tags_create,
                       "tag_req_t", "tag_resp_t",
                       "Create tag",
                       "Create a new tag with optional color");
    csilk_app_put_ext(app, "/api/tags/:id", tags_update,
                      "tag_req_t", "tag_resp_t",
                      "Update tag",
                      "Update an existing tag by ID");
    csilk_app_delete_ext(app, "/api/tags/:id", tags_delete,
                         nullptr, nullptr, "Delete tag",
                         "Delete a tag by ID");
    csilk_app_get_ext(app, "/api/tags/suggestions", tags_suggestions,
                      nullptr, "tag_resp_t",
                      "Tag suggestions",
                      "Returns tag name suggestions for autocomplete (query param: q)");

    // Transfers
    csilk_app_post_ext(app, "/api/transfers", transfers_create,
                       "transfer_req_t", nullptr,
                       "Create transfer",
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

    // Summary
    csilk_app_get_ext(app, "/api/summary", summary_get,
                      nullptr, nullptr, "Dashboard summary",
                      "Returns dashboard aggregate: net worth, monthly income/expense, recent transactions");

    csilk_app_get_ext(app, "/api/asset-balance-logs", asset_logs_list,
                      nullptr, nullptr, "Asset balance logs",
                      "Returns paginated asset balance change logs with optional asset_id filter");

    csilk_app_static(app, "/", "./frontend/dist");

    printf("Starting Minefolio server on :8080\n");
    csilk_app_run(app, 8080);

    csilk_app_free(app);
    csilk_db_pool_free(pool);
    return 0;
}
