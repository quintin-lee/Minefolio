#include "repositories/price_history_repository.h"
#include "infrastructure/database/statement.h"
#include <stdio.h>
#include <stdlib.h>

int
price_history_repo_record(
    mf_db_t* db, int64_t asset_id, const char* price_date, double price, const char* currency)
{
    if (!db || !price_date) {
        return -1;
    }

    const char* cur = (currency && currency[0]) ? currency : "CNY";
    mf_stmt_t*  stmt = NULL;
    int         rc =
        mf_stmt_prepare(db,
                        "INSERT INTO asset_price_history (asset_id, price_date, price, currency) "
                        "VALUES (?, ?, ?, ?) "
                        "ON CONFLICT(asset_id, price_date) DO UPDATE SET "
                        "price = EXCLUDED.price, currency = EXCLUDED.currency;",
                        &stmt);
    if (rc != 0 || !stmt) {
        return -1;
    }

    mf_stmt_bind_int64(stmt, 1, asset_id);
    mf_stmt_bind_text(stmt, 2, price_date);
    mf_stmt_bind_double(stmt, 3, price);
    mf_stmt_bind_text(stmt, 4, cur);

    int64_t aff = 0;
    rc = mf_stmt_execute(stmt, &aff);
    mf_stmt_close(stmt);
    return rc;
}
