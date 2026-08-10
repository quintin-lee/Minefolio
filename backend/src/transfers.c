#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void transfers_create(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    int64_t from_id = (int64_t)csilk_json_get_number(body, "from_asset_id");
    int64_t to_id = (int64_t)csilk_json_get_number(body, "to_asset_id");
    double amount = csilk_json_get_number(body, "amount");
    const char* date = csilk_json_get_string(body, "transfer_date");
    const char* note = csilk_json_get_string(body, "note");
    const char* currency = csilk_json_get_string(body, "currency");
    if (!currency) currency = "CNY";

    if (from_id <= 0 || to_id <= 0 || amount <= 0 || !date) {
        csilk_json_free(body);
        respond_bad_request(c, "from_asset_id、to_asset_id、amount、transfer_date 为必填");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();

    // Verify both assets belong to user
    char check_sql[512];
    snprintf(check_sql, sizeof(check_sql),
        "SELECT COUNT(*) as cnt FROM assets WHERE id IN (%lld, %lld) AND user_id=%lld",
        (long long)from_id, (long long)to_id, (long long)user_id);
    csilk_json_t* chk = csilk_db_query_json(pool, check_sql);
    if (!chk || csilk_json_array_size(chk) == 0 ||
        csilk_json_get_number(csilk_json_array_get(chk, 0), "cnt") != 2) {
        csilk_json_free(body);
        if (chk) csilk_json_free(chk);
        respond_not_found(c);
        return;
    }
    csilk_json_free(chk);

    if (from_id == to_id) {
        csilk_json_free(body);
        respond_bad_request(c, "转出和转入资产不能相同");
        return;
    }

    // Start transaction
    csilk_db_exec(pool, "BEGIN");

    // Transfer out
    char out_sql[512];
    snprintf(out_sql, sizeof(out_sql),
        "INSERT INTO transactions (user_id, asset_id, transaction_type, amount, currency, transaction_date, note) "
        "VALUES (%lld, %lld, 'transfer_out', %.2f, '%s', '%s', '%s')",
        (long long)user_id, (long long)from_id, amount, currency, date, note ? note : "");
    csilk_db_exec(pool, out_sql);

    // Transfer in
    char in_sql[512];
    snprintf(in_sql, sizeof(in_sql),
        "INSERT INTO transactions (user_id, asset_id, transaction_type, amount, currency, transaction_date, note) "
        "VALUES (%lld, %lld, 'transfer_in', %.2f, '%s', '%s', '%s')",
        (long long)user_id, (long long)to_id, amount, currency, date, note ? note : "");
    csilk_db_exec(pool, in_sql);

    // Update asset values
    char update_sql[512];
    snprintf(update_sql, sizeof(update_sql),
        "UPDATE assets SET current_value=current_value-%.2f, updated_at=CURRENT_TIMESTAMP "
        "WHERE id=%lld AND user_id=%lld",
        amount, (long long)from_id, (long long)user_id);
    csilk_db_exec(pool, update_sql);

    snprintf(update_sql, sizeof(update_sql),
        "UPDATE assets SET current_value=current_value+%.2f, updated_at=CURRENT_TIMESTAMP "
        "WHERE id=%lld AND user_id=%lld",
        amount, (long long)to_id, (long long)user_id);
    csilk_db_exec(pool, update_sql);

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(body);
    respond_ok_null(c);
}
