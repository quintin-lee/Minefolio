#include "repositories/portfolio_repository.h"
#include "infrastructure/database/statement.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
portfolio_repo_fetch_positions(mf_db_t*                   db,
                               int64_t                    user_id,
                               portfolio_position_row_t** out_rows,
                               int*                       out_count)
{
    if (!db || !out_rows || !out_count) {
        return -1;
    }
    *out_rows = NULL;
    *out_count = 0;

    mf_stmt_t* stmt = NULL;
    int        rc = mf_stmt_prepare(db,
                                    "SELECT a.id AS asset_id, a.name, "
                                    "COALESCE(a.symbol, '') AS symbol, "
                                    "COALESCE(a.currency, 'CNY') AS currency, "
                                    "a.quantity, a.cost_basis, a.net_value, a.current_value "
                                    "FROM assets a "
                                    "LEFT JOIN categories c ON a.category_id = c.id "
                                    "WHERE a.user_id = ? AND c.asset_type = 'investment' "
                                    "ORDER BY a.name ASC;",
                                    &stmt);
    if (rc != 0 || !stmt) {
        return -1;
    }

    mf_stmt_bind_int64(stmt, 1, user_id);

    mf_result_t* res = NULL;
    rc = mf_stmt_query(stmt, &res);
    if (rc != 0 || !res) {
        mf_stmt_close(stmt);
        return -1;
    }

    int n = mf_result_row_count(res);
    if (n <= 0) {
        mf_result_free(res);
        mf_stmt_close(stmt);
        return 0;
    }

    portfolio_position_row_t* list = calloc((size_t)n, sizeof(*list));
    if (!list) {
        mf_result_free(res);
        mf_stmt_close(stmt);
        return -1;
    }

    int idx = 0;
    while (mf_result_next(res)) {
        list[idx].asset_id = mf_result_get_int64(res, "asset_id");
        snprintf(list[idx].name, sizeof(list[idx].name), "%s", mf_result_get_text(res, "name"));
        snprintf(
            list[idx].symbol, sizeof(list[idx].symbol), "%s", mf_result_get_text(res, "symbol"));
        snprintf(list[idx].currency,
                 sizeof(list[idx].currency),
                 "%s",
                 mf_result_get_text(res, "currency"));
        list[idx].quantity = mf_result_get_double(res, "quantity");
        list[idx].cost_basis = mf_result_get_double(res, "cost_basis");
        list[idx].net_value = mf_result_get_double(res, "net_value");
        list[idx].current_value = mf_result_get_double(res, "current_value");
        idx++;
    }

    *out_rows = list;
    *out_count = idx;

    mf_result_free(res);
    mf_stmt_close(stmt);
    return 0;
}

void
portfolio_repo_free_positions(portfolio_position_row_t* rows)
{
    if (rows) {
        free(rows);
    }
}
