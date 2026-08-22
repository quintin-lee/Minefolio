#include "services/daily_expense_query.h"
#include "services/daily_expense_write.h"

void register_daily_expense_routes(csilk_app_t* app) {
    csilk_app_get_ext(app, "/api/daily-expenses", daily_expenses_list, nullptr, "daily_expense_resp_t", "List daily expenses", "Returns paginated list of daily expense records");
    csilk_app_post_ext(app, "/api/daily-expenses", daily_expenses_create, "daily_expense_req_t", "daily_expense_resp_t", "Create daily expense", "Create a new daily expense entry with optional tags");
    csilk_app_put_ext(app, "/api/daily-expenses/:id", daily_expenses_update, "daily_expense_req_t", "daily_expense_resp_t", "Update daily expense", "Update an existing daily expense record by ID");
    csilk_app_delete_ext(app, "/api/daily-expenses/:id", daily_expenses_delete, nullptr, nullptr, "Delete daily expense", "Delete a daily expense record by ID");
    csilk_app_get_ext(app, "/api/daily-expenses/monthly", daily_expenses_monthly, nullptr, nullptr, "Monthly daily expenses", "Returns monthly aggregated daily expense data");
}
