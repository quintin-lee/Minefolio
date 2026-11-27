#include "services/market/exchange_rate_service.h"
#include "application/transfer/usecases.h"
#include "application/transaction/commands.h"
#include "application/transaction/usecases.h"
#include "domain/auth/repository.h"
#include "common/db.h"
#include "config/secret.h"
#include "csilk/csilk.h"
#include "csilk/drivers/db.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static double
query_asset_balance(csilk_db_pool_t* pool, int64_t asset_id)
{
    char aid_str[32];
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool, "SELECT current_value FROM assets WHERE id=?", (const char*[]){aid_str, NULL});
    assert(res && csilk_json_array_size(res) > 0);
    double val = db_get_num(csilk_json_array_get(res, 0), "current_value");
    csilk_json_free(res);
    return val;
}

static void
test_cross_currency_transfer_fx_and_reversal(void)
{
    printf("--- Running Cross-Currency Transfer FX Persistence & Reversal Tests ---\n");
    const char* test_db = "/tmp/test_fx_transfer_reversal.db";
    unlink(test_db);

    config_secret_set_test_mode(true);
    config_secret_set_test_override("DB_DRIVER", "sqlite");
    config_secret_set_test_override("DB_DSN", test_db);
    config_secret_set_test_override("JWT_SECRET", "test_jwt_secret_fx_semantics_32bytes_long!");

    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);
    assert(db_run_migrations(pool) == 0);

    int64_t user_id = 0;
    assert(mf_auth_repo_create(pool, "fx_transfer_user", "hash_123", &user_id) == 0 &&
           user_id > 0);

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* led_res = csilk_db_query_param_json(
        pool,
        "INSERT INTO ledgers (owner_id, name, currency, is_default) "
        "VALUES (?, 'FX Test Ledger', 'CNY', 1) RETURNING id",
        (const char*[]){uid_str, NULL});
    assert(led_res && csilk_json_array_size(led_res) > 0);
    int64_t ledger_id = db_get_int(csilk_json_array_get(led_res, 0), "id");
    csilk_json_free(led_res);

    char lid_str[32];
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    assert(csilk_db_exec_param(
               pool,
               "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (?, ?, 'owner')",
               (const char*[]){lid_str, uid_str, NULL}) == 0);

    csilk_json_t* cat_res = csilk_db_query_param_json(
        pool,
        "INSERT INTO categories (user_id, name, type, asset_type, currency, ledger_id) "
        "VALUES (?, 'Cash', 'asset', 'cash', 'CNY', ?) RETURNING id",
        (const char*[]){uid_str, lid_str, NULL});
    assert(cat_res && csilk_json_array_size(cat_res) > 0);
    int64_t cat_id = db_get_int(csilk_json_array_get(cat_res, 0), "id");
    csilk_json_free(cat_res);

    char cat_str[32];
    snprintf(cat_str, sizeof(cat_str), "%lld", (long long)cat_id);

    csilk_json_t* usd_res = csilk_db_query_param_json(
        pool,
        "INSERT INTO assets (user_id, category_id, name, current_value, currency, ledger_id) "
        "VALUES (?, ?, 'USD Wallet', 1000.0, 'USD', ?) RETURNING id",
        (const char*[]){uid_str, cat_str, lid_str, NULL});
    assert(usd_res && csilk_json_array_size(usd_res) > 0);
    int64_t usd_asset_id = db_get_int(csilk_json_array_get(usd_res, 0), "id");
    csilk_json_free(usd_res);

    csilk_json_t* cny_res = csilk_db_query_param_json(
        pool,
        "INSERT INTO assets (user_id, category_id, name, current_value, currency, ledger_id) "
        "VALUES (?, ?, 'CNY Wallet', 0.0, 'CNY', ?) RETURNING id",
        (const char*[]){uid_str, cat_str, lid_str, NULL});
    assert(cny_res && csilk_json_array_size(cny_res) > 0);
    int64_t cny_asset_id = db_get_int(csilk_json_array_get(cny_res, 0), "id");
    csilk_json_free(cny_res);

    assert(exchange_rate_set("USD", 7.20) == 0);

    /* Case 1: Normal cross-currency transfer persists fx_rate_to_cny and reverses accurately */
    create_transfer_cmd_t cmd = {
        .user_id = user_id,
        .from_asset_id = usd_asset_id,
        .to_asset_id = cny_asset_id,
        .amount = 100.0,
        .currency = "USD",
        .transfer_date = "2026-10-15",
        .note = "USD to CNY transfer",
        .ledger_id = ledger_id,
    };
    transfer_usecase_result_t tres = {0};
    assert(transfer_usecase_create(pool, &cmd, &tres) == 0);

    assert(fabs(query_asset_balance(pool, usd_asset_id) - 900.0) < 1e-2);
    assert(fabs(query_asset_balance(pool, cny_asset_id) - 720.0) < 1e-2);

    char usd_id_str[32], cny_id_str[32];
    snprintf(usd_id_str, sizeof(usd_id_str), "%lld", (long long)usd_asset_id);
    snprintf(cny_id_str, sizeof(cny_id_str), "%lld", (long long)cny_asset_id);

    csilk_json_t* out_rows = csilk_db_query_param_json(
        pool,
        "SELECT id, fx_rate_to_cny FROM transactions "
        "WHERE user_id=? AND asset_id=? AND transaction_type='transfer_out' "
        "ORDER BY id DESC LIMIT 1",
        (const char*[]){uid_str, usd_id_str, NULL});
    assert(out_rows && csilk_json_array_size(out_rows) > 0);
    int64_t out_tx_id = db_get_int(csilk_json_array_get(out_rows, 0), "id");
    double  out_fx = db_get_num(csilk_json_array_get(out_rows, 0), "fx_rate_to_cny");
    csilk_json_free(out_rows);
    assert(fabs(out_fx - 7.20) < 1e-4);

    csilk_json_t* in_rows = csilk_db_query_param_json(
        pool,
        "SELECT fx_rate_to_cny FROM transactions "
        "WHERE user_id=? AND asset_id=? AND transaction_type='transfer_in' "
        "ORDER BY id DESC LIMIT 1",
        (const char*[]){uid_str, cny_id_str, NULL});
    assert(in_rows && csilk_json_array_size(in_rows) > 0);
    double in_fx = db_get_num(csilk_json_array_get(in_rows, 0), "fx_rate_to_cny");
    csilk_json_free(in_rows);
    assert(fabs(in_fx - 7.20) < 1e-4);

    delete_tx_cmd_t     dcmd = {.user_id = user_id, .tx_id = out_tx_id};
    tx_usecase_result_t dres = {0};
    assert(tx_usecase_delete(pool, &dcmd, &dres) == 0);

    assert(fabs(query_asset_balance(pool, usd_asset_id) - 1000.0) < 1e-2);
    assert(fabs(query_asset_balance(pool, cny_asset_id) - 0.0) < 1e-2);

    /* Case 2: Legacy row with defaulted fx_rate_to_cny=1.0 on non-CNY transfer reverses cleanly */
    assert(transfer_usecase_create(pool, &cmd, &tres) == 0);
    assert(fabs(query_asset_balance(pool, usd_asset_id) - 900.0) < 1e-2);
    assert(fabs(query_asset_balance(pool, cny_asset_id) - 720.0) < 1e-2);

    out_rows = csilk_db_query_param_json(
        pool,
        "SELECT id FROM transactions "
        "WHERE user_id=? AND asset_id=? AND transaction_type='transfer_out' "
        "ORDER BY id DESC LIMIT 1",
        (const char*[]){uid_str, usd_id_str, NULL});
    assert(out_rows && csilk_json_array_size(out_rows) > 0);
    out_tx_id = db_get_int(csilk_json_array_get(out_rows, 0), "id");
    csilk_json_free(out_rows);

    char out_id_str[32];
    snprintf(out_id_str, sizeof(out_id_str), "%lld", (long long)out_tx_id);
    assert(csilk_db_exec_param(
               pool,
               "UPDATE transactions SET fx_rate_to_cny = 1.0 WHERE id = ? AND user_id = ?",
               (const char*[]){out_id_str, uid_str, NULL}) == 0);

    dcmd.tx_id = out_tx_id;
    memset(&dres, 0, sizeof(dres));
    assert(tx_usecase_delete(pool, &dcmd, &dres) == 0);

    assert(fabs(query_asset_balance(pool, usd_asset_id) - 1000.0) < 1e-2);
    assert(fabs(query_asset_balance(pool, cny_asset_id) - 0.0) < 1e-2);

    csilk_db_pool_free(pool);
    config_secret_clear_test_overrides();
    unlink(test_db);
}

int main(void) {
    // 1. Known currencies must succeed with code 0
    double rate = 0.0;
    int rc = exchange_rate_get_to_cny_safe("CNY", &rate);
    assert(rc == 0 && rate == 1.0);

    rc = exchange_rate_get_to_cny_safe("USD", &rate);
    assert(rc == 0 && rate > 0.0);

    rc = exchange_rate_get_to_cny_safe("HKD", &rate);
    assert(rc == 0 && rate > 0.0);

    // 2. Unknown currency must return -1 in safe mode
    rc = exchange_rate_get_to_cny_safe("XYZ_UNKNOWN", &rate);
    assert(rc == -1);

    // 3. NULL or empty currency must return -1 in safe mode
    rc = exchange_rate_get_to_cny_safe(NULL, &rate);
    assert(rc == -1);
    rc = exchange_rate_get_to_cny_safe("", &rate);
    assert(rc == -1);

    // 4. Legacy fallback function still returns 1.0 for backwards compatibility
    double fallback_rate = exchange_rate_get_to_cny("XYZ_UNKNOWN");
    assert(fallback_rate == 1.0);

    // 5. Historical or latest point-in-time FX lookup tests
    printf("--- Running Historical FX Lookup Tests ---\n");
    csilk_db_init();
    const char* test_db = "/tmp/test_fx_history.db";
    unlink(test_db);
    csilk_db_pool_t* pool = csilk_db_pool_new("sqlite", test_db);
    assert(pool != NULL);

    csilk_db_exec(pool,
        "CREATE TABLE IF NOT EXISTS exchange_rate_history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "rate_date TEXT NOT NULL, "
        "base_currency TEXT NOT NULL DEFAULT 'CNY', "
        "target_currency TEXT NOT NULL, "
        "rate DECIMAL(18,6) NOT NULL, "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "UNIQUE (rate_date, base_currency, target_currency)"
        ");");

    currency_t cny = currency_from_str("CNY");
    currency_t rmb = currency_from_str("RMB");
    currency_t usd = currency_from_str("USD");
    currency_t xyz = currency_from_str("XYZ");

    // 5.1 CNY / RMB must return 1.0 regardless of pool or date
    assert(exchange_rate_get_historical_or_latest(pool, cny, "2026-05-10") == 1.0);
    assert(exchange_rate_get_historical_or_latest(pool, rmb, "2026-05-10") == 1.0);
    assert(exchange_rate_get_historical_or_latest(NULL, cny, "2026-05-10") == 1.0);

    // 5.2 Date before any history smoothly falls back to latest rate (RF-2) without 0 or NaN
    double usd_latest = exchange_rate_get_to_cny("USD");
    double pre_hist_rate = exchange_rate_get_historical_or_latest(pool, usd, "2026-01-01");
    assert(!isnan(pre_hist_rate) && !isinf(pre_hist_rate) && pre_hist_rate > 0.0);
    assert(fabs(pre_hist_rate - usd_latest) < 1e-4);

    // 5.3 Insert historical snapshots
    csilk_db_exec(pool,
        "INSERT INTO exchange_rate_history (rate_date, base_currency, target_currency, rate) "
        "VALUES ('2026-05-10', 'CNY', 'USD', 7.150000);");
    csilk_db_exec(pool,
        "INSERT INTO exchange_rate_history (rate_date, base_currency, target_currency, rate) "
        "VALUES ('2026-05-15', 'CNY', 'USD', 7.250000);");

    // Before first record date -> fallback to latest rate
    double r_before = exchange_rate_get_historical_or_latest(pool, usd, "2026-05-09");
    assert(fabs(r_before - usd_latest) < 1e-4);

    // Exactly first record date -> 7.15
    double r_10 = exchange_rate_get_historical_or_latest(pool, usd, "2026-05-10");
    assert(fabs(r_10 - 7.15) < 1e-4);

    // Between records -> latest on or before target date -> 7.15
    double r_12 = exchange_rate_get_historical_or_latest(pool, usd, "2026-05-12");
    assert(fabs(r_12 - 7.15) < 1e-4);

    // Second record date -> 7.25
    double r_15 = exchange_rate_get_historical_or_latest(pool, usd, "2026-05-15");
    assert(fabs(r_15 - 7.25) < 1e-4);

    // After second record date -> 7.25
    double r_20 = exchange_rate_get_historical_or_latest(pool, usd, "2026-05-20");
    assert(fabs(r_20 - 7.25) < 1e-4);

    // Timestamp format prefix "2026-05-10 18:30:00" -> matches 2026-05-10 -> 7.15
    double r_time = exchange_rate_get_historical_or_latest(pool, usd, "2026-05-10 18:30:00");
    assert(fabs(r_time - 7.15) < 1e-4);

    // Missing/NULL date -> latest rate
    double r_null = exchange_rate_get_historical_or_latest(pool, usd, NULL);
    assert(fabs(r_null - usd_latest) < 1e-4);

    // Unknown currency -> fallback 1.0 (no 0 or NaN)
    double r_xyz = exchange_rate_get_historical_or_latest(pool, xyz, "2026-05-10");
    assert(!isnan(r_xyz) && !isinf(r_xyz) && r_xyz == 1.0);

    csilk_db_pool_free(pool);
    unlink(test_db);

    test_cross_currency_transfer_fx_and_reversal();

    printf("PASS: test_fx_semantics\n");
    return 0;
}
