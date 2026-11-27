#include "domain/portfolio/rules.h"
#include "application/market/usecases.h"
#include "domain/auth/repository.h"
#include "infrastructure/repositories/ledger_snapshot_repo_impl.h"
#include "infrastructure/repositories/market_repo_impl.h"
#include "services/market/exchange_rate_service.h"
#include "common/db.h"
#include "config/secret.h"
#include "csilk/csilk.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void
test_market_sync_currency_alignment(void)
{
    const char* test_db = "/tmp/test_mf_portfolio_pnl_sync.db";
    unlink(test_db);
    char shm[256], wal[256];
    snprintf(shm, sizeof(shm), "%s-shm", test_db);
    snprintf(wal, sizeof(wal), "%s-wal", test_db);
    unlink(shm);
    unlink(wal);

    setenv("MINEFOLIO_MARKET_MOCK", "1", 1);
    config_secret_set_test_mode(true);
    config_secret_set_test_override("DB_DRIVER", "sqlite");
    config_secret_set_test_override("DB_DSN", test_db);
    config_secret_set_test_override("JWT_SECRET", "test_jwt_secret_portfolio_pnl_32b!!");

    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);
    assert(db_run_migrations(pool) == 0);

    int64_t user_id = 0;
    assert(mf_auth_repo_create(pool, "pnl_sync_user", "hash_sync_123", &user_id) == 0 &&
           user_id > 0);

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* led_res = csilk_db_query_param_json(
        pool,
        "INSERT INTO ledgers (owner_id, name, currency, is_default) "
        "VALUES (?, 'Sync Ledger', 'CNY', 1) RETURNING id",
        (const char*[]){uid_str, NULL});
    assert(led_res && csilk_json_array_size(led_res) == 1);
    int64_t ledger_id = db_get_int(csilk_json_array_get(led_res, 0), "id");
    csilk_json_free(led_res);

    char lid_str[32];
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    csilk_json_t* cat_res = csilk_db_query_param_json(
        pool,
        "INSERT INTO categories (user_id, name, type, asset_type, currency, ledger_id) "
        "VALUES (?, 'US Stocks', 'asset', 'stock', 'USD', ?) RETURNING id",
        (const char*[]){uid_str, lid_str, NULL});
    assert(cat_res && csilk_json_array_size(cat_res) == 1);
    int64_t cat_id = db_get_int(csilk_json_array_get(cat_res, 0), "id");
    csilk_json_free(cat_res);

    char cat_str[32];
    snprintf(cat_str, sizeof(cat_str), "%lld", (long long)cat_id);

    /* Asset in USD; mock quote returns 100.0 CNY -> at USD->CNY=5.0, price must convert to 20.0 USD */
    csilk_json_t* asset_res = csilk_db_query_param_json(
        pool,
        "INSERT INTO assets (user_id, ledger_id, category_id, name, currency, symbol, quote_source, "
        "quantity, net_value, current_value, cost_basis) "
        "VALUES (?, ?, ?, 'Apple Inc', 'USD', 'AAPL', 'stock_us', 2.0, 10.0, 20.0, 20.0) RETURNING id",
        (const char*[]){uid_str, lid_str, cat_str, NULL});
    assert(asset_res && csilk_json_array_size(asset_res) == 1);
    int64_t asset_id = db_get_int(csilk_json_array_get(asset_res, 0), "id");
    csilk_json_free(asset_res);

    assert(exchange_rate_set("USD", 5.0) == 0);

    time_t    now_t = time(NULL);
    struct tm tm_now;
    localtime_r(&now_t, &tm_now);
    char today_str[16];
    strftime(today_str, sizeof(today_str), "%Y-%m-%d", &tm_now);

    /* Create a snapshot for today so we can verify market sync invalidates it */
    assert(ledger_snapshot_save(
               pool, user_id, asset_id, today_str, NULL, money_zero(CURRENCY_USD), today_str, 1, NULL) ==
           0);
    csilk_json_t* snap_before = NULL;
    assert(ledger_snapshot_get_latest(pool, user_id, asset_id, today_str, &snap_before) == 0 &&
           snap_before != NULL);
    csilk_json_free(snap_before);

    csilk_json_t*           sync_data = NULL;
    market_usecase_result_t mres = {0};
    int rc = market_usecase_sync_single(pool, user_id, asset_id, &sync_data, &mres);
    assert(rc == 0 && mres.code == 0 && sync_data != NULL);
    assert(fabs(db_get_num(sync_data, "net_value") - 20.0) < 1e-4);
    csilk_json_free(sync_data);

    char aid_str[32];
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    csilk_json_t* check_rows = csilk_db_query_param_json(
        pool,
        "SELECT net_value, current_value, currency FROM assets WHERE id=?",
        (const char*[]){aid_str, NULL});
    assert(check_rows && csilk_json_array_size(check_rows) == 1);
    const csilk_json_t* ar = csilk_json_array_get(check_rows, 0);
    assert(fabs(db_get_num(ar, "net_value") - 20.0) < 1e-4);
    assert(fabs(db_get_num(ar, "current_value") - 40.0) < 1e-4);
    assert(strcmp(csilk_json_get_string(ar, "currency"), "USD") == 0);
    csilk_json_free(check_rows);

    csilk_json_t* hist = mf_market_repo_price_history_list(pool, user_id, asset_id, 10);
    assert(hist && csilk_json_array_size(hist) == 1);
    const csilk_json_t* hr = csilk_json_array_get(hist, 0);
    assert(fabs(db_get_num(hr, "price") - 20.0) < 1e-4);
    assert(strcmp(csilk_json_get_string(hr, "currency"), "USD") == 0);
    csilk_json_free(hist);

    csilk_json_t* snap_after = NULL;
    assert(ledger_snapshot_get_latest(pool, user_id, asset_id, today_str, &snap_after) == 0 &&
           snap_after == NULL);

    /* Bulk sync test with a second USD asset */
    csilk_json_t* asset2_res = csilk_db_query_param_json(
        pool,
        "INSERT INTO assets (user_id, ledger_id, category_id, name, currency, symbol, quote_source, "
        "quantity, net_value, current_value, cost_basis) "
        "VALUES (?, ?, ?, 'Microsoft', 'USD', 'MSFT', 'stock_us', 3.0, 5.0, 15.0, 15.0) RETURNING id",
        (const char*[]){uid_str, lid_str, cat_str, NULL});
    assert(asset2_res && csilk_json_array_size(asset2_res) == 1);
    int64_t asset2_id = db_get_int(csilk_json_array_get(asset2_res, 0), "id");
    csilk_json_free(asset2_res);

    int synced = 0, failed = 0;
    assert(market_usecase_do_sync_user(pool, user_id, &synced, &failed) == 0);
    assert(synced == 2 && failed == 0);

    char aid2_str[32];
    snprintf(aid2_str, sizeof(aid2_str), "%lld", (long long)asset2_id);
    csilk_json_t* check2 = csilk_db_query_param_json(
        pool,
        "SELECT net_value, current_value FROM assets WHERE id=?",
        (const char*[]){aid2_str, NULL});
    assert(check2 && csilk_json_array_size(check2) == 1);
    assert(fabs(db_get_num(csilk_json_array_get(check2, 0), "net_value") - 20.0) < 1e-4);
    assert(fabs(db_get_num(csilk_json_array_get(check2, 0), "current_value") - 60.0) < 1e-4);
    csilk_json_free(check2);

    assert(exchange_rate_set("USD", 7.20) == 0);
    unlink(test_db);
    unlink(shm);
    unlink(wal);
    printf("PASS: test_market_sync_currency_alignment\n");
}

int main(void) {
    currency_t cny = currency_from_str("CNY");

    // Scenario: Buy 1000 @ 2.0 = 2000, fee 10; Sell 400 @ 3.0 = 1200, fee 8; Dividend 100
    mf_holding_item_t item = {0};
    item.asset_id = 200;
    item.currency = cny;
    quantity_from_double(600.0, 4, &item.quantity);
    price_from_double(2.5, 4, cny, &item.net_value);
    money_from_double(1200.0, cny, &item.cost_basis);

    mf_portfolio_trade_event_t events[3] = {0};
    // 1. Buy 1000 @ 2.0 = 2000, fee 10
    events[0].asset_id = 200;
    snprintf(events[0].type, sizeof(events[0].type), "buy");
    quantity_from_double(1000.0, 4, &events[0].quantity);
    price_from_double(2.0, 4, cny, &events[0].price);
    money_from_double(2000.0, cny, &events[0].amount);
    money_from_double(10.0, cny, &events[0].fee);

    // 2. Sell 400 @ 3.0 = 1200, fee 8
    events[1].asset_id = 200;
    snprintf(events[1].type, sizeof(events[1].type), "sell");
    quantity_from_double(400.0, 4, &events[1].quantity);
    price_from_double(3.0, 4, cny, &events[1].price);
    money_from_double(1200.0, cny, &events[1].amount);
    money_from_double(8.0, cny, &events[1].fee);

    // 3. Dividend 100
    events[2].asset_id = 200;
    snprintf(events[2].type, sizeof(events[2].type), "dividend");
    money_from_double(100.0, cny, &events[2].amount);

    int rc = mf_portfolio_rule_apply_trade_events(&item, 1, events, 3);
    assert(rc == 0);

    // Realized capital gain = (1200 - 8) - 2010 * 0.4 + 100 = 488.0
    double realized = money_to_double(item.realized_pnl);
    assert(fabs(realized - 488.0) < 1e-4);

    // Cost basis remaining for 600 shares = 1200.0
    double cost = money_to_double(item.cost_basis);
    assert(fabs(cost - 1200.0) < 1e-4);

    printf("PASS: test_portfolio_pnl_alignment\n");
    test_market_sync_currency_alignment();
    return 0;
}
