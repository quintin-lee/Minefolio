#include "services/transaction_service.h"
#include "services/transaction_query.h"
#include "services/transaction_write.h"

void register_transaction_routes(csilk_app_t* app) {
    csilk_app_get_ext(app, "/api/transactions", transactions_list, nullptr, "transaction_resp_t", "List transactions", "Returns paginated transaction list with optional filters");
    csilk_app_get_ext(app, "/api/transactions/monthly", transactions_monthly, nullptr, nullptr, "Monthly transaction summary", "Returns monthly aggregated transaction totals");
    csilk_app_post_ext(app, "/api/transactions", transactions_create, "transaction_req_t", "transaction_resp_t", "Create transaction", "Create a new transaction (expense, income, transfer, investment buy/sell)");
    csilk_app_put_ext(app, "/api/transactions/:id", transactions_update, "transaction_req_t", "transaction_resp_t", "Update transaction", "Update an existing transaction by ID");
    csilk_app_delete_ext(app, "/api/transactions/:id", transactions_delete, nullptr, nullptr, "Delete transaction", "Delete a transaction by ID");
}
