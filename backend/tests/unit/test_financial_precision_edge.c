#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core/financial/price.h"
#include "core/financial/quantity.h"
#include "core/financial/money.h"
#include "domain/market/rules.h"
#include "application/portfolio/usecases.h"
#include "application/transaction/usecases.h"
#include "core/ledger/ledger_engine.h"
#include "common/db.h"
#include "csilk/csilk.h"

static void
init_precision_test_db(const char* db_path)
{
    unlink(db_path);
    setenv("MINEFOLIO_DB_DRIVER", "sqlite", 1);
    setenv("MINEFOLIO_DB_DSN", db_path, 1);
    setenv("MINEFOLIO_JWT_SECRET", "test_fin_precision_secret_32bytes!", 1);

    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);

    FILE* f = fopen("sql/migration.sql", "r");
    if (!f) {
        f = fopen("../sql/migration.sql", "r");
    }
    if (!f) {
        f = fopen("backend/sql/migration.sql", "r");
    }
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* sql = malloc((size_t)sz + 1);
    assert(sql != NULL);
    fread(sql, 1, (size_t)sz, f);
    sql[sz] = '\0';
    fclose(f);
    assert(csilk_db_exec(pool, sql) == 0);
    free(sql);
}

int main(void) {
    ledger_register_balance_hooks();
    printf("Running test_financial_precision_edge...\n");

    /* 1. Test precision multiplication: 1000.0000 shares @ 19.99 CNY */
    quantity_t qty;
    assert(quantity_from_double(1000.0, 4, &qty) == DECIMAL_OK);

    price_t price;
    assert(price_from_double(19.99, 4, CURRENCY_CNY, &price) == DECIMAL_OK);

    money_t total_val;
    assert(price_times_quantity(price, qty, &total_val) == DECIMAL_OK);

    /* 2. Verify exact balance without float residual */
    char val_str[64];
    money_to_string(total_val, val_str, sizeof(val_str));
    assert(strcmp(val_str, "19990.00") == 0);

    /* 3. Test exact delta subtraction with 3-arg money_sub */
    money_t old_val;
    assert(money_from_double(19900.00, CURRENCY_CNY, &old_val) == DECIMAL_OK);

    money_t delta;
    assert(money_sub(total_val, old_val, &delta) == DECIMAL_OK);

    char delta_str[64];
    money_to_string(delta, delta_str, sizeof(delta_str));
    assert(strcmp(delta_str, "90.00") == 0);

    /* 4. Directly test mf_market_rule_calc_sync_delta calculation */
    price_t p_old;
    assert(price_from_double(0.0, 4, CURRENCY_CNY, &p_old) == DECIMAL_OK);
    price_t p_new = price;
    money_t sync_delta;
    mf_market_rule_calc_sync_delta(p_old, p_new, qty, CURRENCY_CNY, &sync_delta);
    char sync_delta_str[64];
    money_to_string(sync_delta, sync_delta_str, sizeof(sync_delta_str));
    assert(strcmp(sync_delta_str, "19990.00") == 0);

    printf("PASS: test_financial_precision_edge (core) completed, testing trend decimal precision...\n");

    /* 5. Verify portfolio_usecase_get_dashboard_summary trend avoids float accumulation drift:
     *    1e15 + 0.05 + 0.05 - 1e15 in IEEE-754 double collapses to 0.00 because ULP(1e15) = 0.125,
     *    whereas 128-bit decimal_t accumulation preserves exact 0.10 net_worth. */
    const char* db_file = "/tmp/test_financial_precision_trend.db";
    init_precision_test_db(db_file);
    csilk_db_pool_t* pool = db_get_pool();

    assert(csilk_db_exec(pool,
                         "INSERT INTO users (id, username, password) VALUES (1, 'ptester', 'h')") ==
           0);
    assert(csilk_db_exec(
               pool,
               "INSERT INTO ledgers (id, owner_id, name, currency, is_default) "
               "VALUES (1, 1, 'Main', 'CNY', 1)") == 0);
    assert(csilk_db_exec(pool,
                         "INSERT INTO ledger_members (ledger_id, user_id, role) "
                         "VALUES (1, 1, 'owner')") == 0);
    assert(csilk_db_exec(
               pool,
               "INSERT INTO categories (id, user_id, ledger_id, name, type, asset_type) "
               "VALUES (10, 1, 1, 'Cash', 'expense', 'cash'), "
               "(11, 1, 1, 'Loan', 'expense', 'loan')") == 0);
    assert(csilk_db_exec(
               pool,
               "INSERT INTO assets (id, user_id, ledger_id, category_id, name, currency, "
               "current_value, created_at) VALUES "
               "(101, 1, 1, 10, 'BigCash', 'CNY', 1000000000000000.0, '2020-01-01 00:00:00'), "
               "(102, 1, 1, 10, 'PennyA', 'CNY', 0.05, '2020-01-01 00:00:00'), "
               "(103, 1, 1, 10, 'PennyB', 'CNY', 0.05, '2020-01-01 00:00:00'), "
               "(104, 1, 1, 11, 'BigLoan', 'CNY', 1000000000000000.0, '2020-01-01 00:00:00')") ==
           0);

    query_portfolio_cmd_t qcmd = {
        .user_id = 1,
        .ledger_id = 1,
        .base_currency = "CNY",
    };
    csilk_json_t*              resp = NULL;
    portfolio_usecase_result_t pres = {0};
    assert(portfolio_usecase_get_dashboard_summary(pool, &qcmd, &resp, &pres) == 0 && resp != NULL);

    const csilk_json_t* trend = csilk_json_get(resp, "trend");
    assert(trend && csilk_json_array_size(trend) == 30);
    const csilk_json_t* today_pt = csilk_json_array_get(trend, 29);
    double              net_worth = db_get_num(today_pt, "net_worth");
    assert(fabs(net_worth - 0.10) < 0.001);

    csilk_json_free(resp);

    /* 6. Verify 8-decimal crypto precision across db_get_decimal, tx_usecase_create, and rebuild */
    csilk_json_t* num_obj = csilk_json_object();
    csilk_json_add_number(num_obj, "btc_qty", 0.00001234);
    csilk_json_add_number(num_obj, "btc_price", 68000.12345678);
    csilk_json_add_number(num_obj, "whole_amt", 100.0);

    quantity_t q_small = db_get_quantity(num_obj, "btc_qty");
    char       q_small_str[64] = {0};
    quantity_to_string_fixed(q_small, 8, q_small_str, sizeof(q_small_str));
    assert(strcmp(q_small_str, "0.00001234") == 0);

    price_t p_btc = db_get_price(num_obj, "btc_price", CURRENCY_USD);
    char    p_btc_str[64] = {0};
    price_to_string_fixed(p_btc, 8, p_btc_str, sizeof(p_btc_str));
    assert(strcmp(p_btc_str, "68000.12345678") == 0);

    decimal_t d_whole = db_get_decimal(num_obj, "whole_amt");
    assert(d_whole.scale == 0);
    assert(d_whole.mantissa == 100);
    csilk_json_free(num_obj);

    assert(csilk_db_exec(
               pool,
               "INSERT INTO categories (id, user_id, ledger_id, name, type, asset_type) "
               "VALUES (12, 1, 1, 'Crypto', 'expense', 'crypto')") == 0);
    assert(csilk_db_exec(
               pool,
               "INSERT INTO assets (id, user_id, ledger_id, category_id, name, currency, "
               "quantity, cost_basis, net_value, current_value) VALUES "
               "(105, 1, 1, 12, 'BTC', 'USD', 0, 0, 0, 0)") == 0);

    create_tx_cmd_t buy_btc = {
        .user_id = 1,
        .ledger_id = 1,
        .asset_id = 105,
        .linked_asset_id = 0,
        .category_id = 12,
        .type = "buy",
        .amount = 0.84,
        .price = 68000.12345678,
        .quantity = 0.00001234,
        .fee = 0.0,
        .currency = "USD",
        .note = "precision test",
        .date = "2026-01-15",
    };
    tx_usecase_result_t tx_res = {0};
    assert(tx_usecase_create(pool, &buy_btc, &tx_res) == 0);

    csilk_json_t* btc_rows = csilk_db_query_param_json(
        pool, "SELECT quantity FROM assets WHERE id=105", (const char*[]){NULL});
    assert(btc_rows && csilk_json_array_size(btc_rows) == 1);
    quantity_t stored_btc = db_get_quantity(csilk_json_array_get(btc_rows, 0), "quantity");
    char       stored_btc_str[64] = {0};
    quantity_to_string_fixed(stored_btc, 8, stored_btc_str, sizeof(stored_btc_str));
    assert(strcmp(stored_btc_str, "0.00001234") == 0);
    csilk_json_free(btc_rows);

    ledger_position_state_t pos_state = {0};
    assert(ledger_rebuild_position(pool, 1, 105, &pos_state) == 0);
    char reb_qty_str[64] = {0};
    quantity_to_string_fixed(pos_state.quantity, 8, reb_qty_str, sizeof(reb_qty_str));
    assert(strcmp(reb_qty_str, "0.00001234") == 0);

    unlink(db_file);

    printf("PASS: test_financial_precision_edge completed successfully\n");
    return 0;
}
