#include "infrastructure/repositories/transaction_lot_repo_impl.h"
#include "common/db.h"
#include "core/financial/quantity.h"
#include "core/financial/price.h"
#include "core/financial/money.h"
#include "csilk/csilk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
init_test_db(const char* db_path)
{
    unlink(db_path);
    setenv("MINEFOLIO_DB_DRIVER", "sqlite", 1);
    setenv("MINEFOLIO_DB_DSN", db_path, 1);
    setenv("MINEFOLIO_JWT_SECRET", "test_tx_lots_secret_32bytes_key!", 1);

    csilk_db_pool_t* pool = NULL;
    int              rc = db_init(&pool);
    assert(rc == 0);
    assert(pool != NULL);

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

    rc = csilk_db_exec(pool, sql);
    assert(rc == 0);
    free(sql);
}

int
main(void)
{
    printf("=== Running Unit Test: Transaction Lots Repository (TDD) ===\n");
    const char* db_file = "/tmp/test_transaction_lots.db";
    init_test_db(db_file);
    csilk_db_pool_t* pool = db_get_pool();
    assert(pool != NULL);

    int64_t user_id = 1;
    int64_t asset_id = 101;

    // Seed test fixtures: user, category, asset, transactions
    csilk_db_exec(pool,
                  "INSERT INTO users (id, username, password) VALUES (1, 'lot_tester', 'hash')");
    csilk_db_exec(pool,
                  "INSERT INTO categories (id, user_id, name, type, asset_type) VALUES (10, 1, "
                  "'Stock', 'expense', 'stock')");
    csilk_db_exec(pool,
                  "INSERT INTO assets (id, user_id, category_id, name, currency, current_value, "
                  "quantity, cost_basis, net_value) "
                  "VALUES (101, 1, 10, 'Test Stock', 'CNY', 0, 0, 0, 0)");
    csilk_db_exec(
        pool,
        "INSERT INTO transactions (id, user_id, asset_id, transaction_type, direction, amount, "
        "price_per_unit, quantity, fee, currency, transaction_date) "
        "VALUES (1001, 1, 101, 'buy', 'in', 1050.0, 10.50, 100.0, 5.0, 'CNY', '2026-01-01')");
    csilk_db_exec(
        pool,
        "INSERT INTO transactions (id, user_id, asset_id, transaction_type, direction, amount, "
        "price_per_unit, quantity, fee, currency, transaction_date) "
        "VALUES (1002, 1, 101, 'buy', 'in', 600.0, 12.00, 50.0, 2.5, 'CNY', '2026-01-05')");
    csilk_db_exec(
        pool,
        "INSERT INTO transactions (id, user_id, asset_id, transaction_type, direction, amount, "
        "price_per_unit, quantity, fee, currency, transaction_date) "
        "VALUES (1003, 1, 101, 'buy', 'in', 300.0, 15.00, 20.0, 1.0, 'CNY', '2026-01-10')");

    /* -------------------------------------------------------------
     * Test 1: Parameter validation & error boundaries
     * ------------------------------------------------------------- */
    printf("Test 1: Parameter validation & error handling...\n");
    quantity_t q100;
    assert(quantity_from_string("100.0", &q100) == DECIMAL_OK);
    price_t p10_5;
    assert(price_from_string("10.50", CURRENCY_CNY, &p10_5) == DECIMAL_OK);
    money_t m5;
    assert(money_from_string("5.00", CURRENCY_CNY, &m5) == DECIMAL_OK);

    assert(tx_lot_create(NULL, user_id, asset_id, 1001, "2026-01-01", q100, p10_5, m5) == -1);
    assert(tx_lot_create(pool, -1, asset_id, 1001, "2026-01-01", q100, p10_5, m5) == -1);
    assert(tx_lot_create(pool, user_id, -1, 1001, "2026-01-01", q100, p10_5, m5) == -1);
    assert(tx_lot_create(pool, user_id, asset_id, -1, "2026-01-01", q100, p10_5, m5) == -1);
    assert(tx_lot_create(pool, user_id, asset_id, 1001, NULL, q100, p10_5, m5) == -1);
    assert(tx_lot_create(pool, user_id, asset_id, 1001, "", q100, p10_5, m5) == -1);

    assert(tx_lots_get_open(NULL, user_id, asset_id) == NULL);
    assert(tx_lots_get_open(pool, -1, asset_id) == NULL);
    assert(tx_lots_get_open(pool, user_id, -1) == NULL);

    assert(tx_lot_deduct(NULL, user_id, 1, q100, q100, false) == -1);
    assert(tx_lot_deduct(pool, -1, 1, q100, q100, false) == -1);
    assert(tx_lot_deduct(pool, user_id, -1, q100, q100, false) == -1);

    assert(tx_lots_delete_by_asset(NULL, user_id, asset_id) == -1);
    assert(tx_lots_delete_by_asset(pool, -1, asset_id) == -1);
    assert(tx_lots_delete_by_asset(pool, user_id, -1) == -1);

    assert(tx_lots_delete_by_buy_tx(NULL, user_id, 1001) == -1);
    assert(tx_lots_delete_by_buy_tx(pool, -1, 1001) == -1);
    assert(tx_lots_delete_by_buy_tx(pool, user_id, -1) == -1);

    /* -------------------------------------------------------------
     * Test 2: Create lots and check initial open status
     * ------------------------------------------------------------- */
    printf("Test 2: Create lots...\n");
    int64_t lot1 = tx_lot_create(pool, user_id, asset_id, 1001, "2026-01-01", q100, p10_5, m5);
    assert(lot1 > 0);

    quantity_t q50;
    assert(quantity_from_string("50.0", &q50) == DECIMAL_OK);
    price_t p12;
    assert(price_from_string("12.00", CURRENCY_CNY, &p12) == DECIMAL_OK);
    money_t m2_5;
    assert(money_from_string("2.50", CURRENCY_CNY, &m2_5) == DECIMAL_OK);
    int64_t lot2 = tx_lot_create(pool, user_id, asset_id, 1002, "2026-01-05", q50, p12, m2_5);
    assert(lot2 > 0);
    assert(lot2 != lot1);

    /* -------------------------------------------------------------
     * Test 3: Query open lots (FIFO ordering)
     * ------------------------------------------------------------- */
    printf("Test 3: Query open lots (FIFO order)...\n");
    csilk_json_t* open_lots = tx_lots_get_open(pool, user_id, asset_id);
    assert(open_lots != NULL);
    assert(csilk_json_array_size(open_lots) == 2);

    csilk_json_t* item0 = (csilk_json_t*)csilk_json_array_get(open_lots, 0);
    csilk_json_t* item1 = (csilk_json_t*)csilk_json_array_get(open_lots, 1);

    assert(db_get_int(item0, "id") == lot1);
    assert(db_get_int(item0, "buy_tx_id") == 1001);
    assert(strcmp(csilk_json_get_string(item0, "buy_date"), "2026-01-01") == 0);
    assert(strcmp(csilk_json_get_string(item0, "status"), "open") == 0);
    assert(db_get_num(item0, "original_quantity") == 100.0);
    assert(db_get_num(item0, "remaining_quantity") == 100.0);

    assert(db_get_int(item1, "id") == lot2);
    assert(db_get_int(item1, "buy_tx_id") == 1002);
    assert(strcmp(csilk_json_get_string(item1, "buy_date"), "2026-01-05") == 0);
    assert(strcmp(csilk_json_get_string(item1, "status"), "open") == 0);

    csilk_json_free(open_lots);

    /* -------------------------------------------------------------
     * Test 4: Deduct partial from lot 1 (status remains open)
     * ------------------------------------------------------------- */
    printf("Test 4: Partial deduction...\n");
    quantity_t deduct40, rem60;
    assert(quantity_from_string("40.0", &deduct40) == DECIMAL_OK);
    assert(quantity_from_string("60.0", &rem60) == DECIMAL_OK);
    assert(tx_lot_deduct(pool, user_id + 999, lot1, deduct40, rem60, false) == -1);
    int rc = tx_lot_deduct(pool, user_id, lot1, deduct40, rem60, false);
    assert(rc == 0);

    open_lots = tx_lots_get_open(pool, user_id, asset_id);
    assert(open_lots != NULL);
    assert(csilk_json_array_size(open_lots) == 2);

    item0 = (csilk_json_t*)csilk_json_array_get(open_lots, 0);
    assert(db_get_int(item0, "id") == lot1);
    assert(db_get_num(item0, "remaining_quantity") == 60.0);
    assert(strcmp(csilk_json_get_string(item0, "status"), "open") == 0);

    csilk_json_free(open_lots);

    /* -------------------------------------------------------------
     * Test 5: Deduct remainder of lot 1 (status becomes closed)
     * ------------------------------------------------------------- */
    printf("Test 5: Full deduction to closed...\n");
    quantity_t deduct60;
    assert(quantity_from_string("60.0", &deduct60) == DECIMAL_OK);
    quantity_t rem0 = quantity_zero();
    rc = tx_lot_deduct(pool, user_id, lot1, deduct60, rem0, true);
    assert(rc == 0);

    open_lots = tx_lots_get_open(pool, user_id, asset_id);
    assert(open_lots != NULL);
    assert(csilk_json_array_size(open_lots) == 1);

    item0 = (csilk_json_t*)csilk_json_array_get(open_lots, 0);
    assert(db_get_int(item0, "id") == lot2);
    assert(strcmp(csilk_json_get_string(item0, "status"), "open") == 0);

    csilk_json_free(open_lots);

    /* -------------------------------------------------------------
     * Test 6: Serialization of open lots to JSON
     * ------------------------------------------------------------- */
    printf("Test 6: Serialize open lots to JSON string...\n");
    char* json_str = tx_lots_serialize_open_json(pool, user_id, asset_id);
    assert(json_str != NULL);
    assert(strstr(json_str, "2026-01-05") != NULL);
    assert(strstr(json_str, "1002") != NULL);
    assert(strstr(json_str, "2026-01-01") == NULL); // Lot 1 is closed!
    free(json_str);

    // Empty serialization for asset with no lots
    char* empty_json = tx_lots_serialize_open_json(pool, user_id, 9999);
    assert(empty_json != NULL);
    assert(strcmp(empty_json, "[]") == 0);
    free(empty_json);

    /* -------------------------------------------------------------
     * Test 7: Delete lots by buy transaction ID
     * ------------------------------------------------------------- */
    printf("Test 7: Delete by buy_tx_id...\n");
    rc = tx_lots_delete_by_buy_tx(pool, user_id, 1002);
    assert(rc == 0);

    open_lots = tx_lots_get_open(pool, user_id, asset_id);
    assert(open_lots != NULL);
    assert(csilk_json_array_size(open_lots) == 0);
    csilk_json_free(open_lots);

    /* -------------------------------------------------------------
     * Test 8: Delete lots by asset ID
     * ------------------------------------------------------------- */
    printf("Test 8: Delete by asset_id...\n");
    quantity_t q20;
    assert(quantity_from_string("20.0", &q20) == DECIMAL_OK);
    price_t p15;
    assert(price_from_string("15.00", CURRENCY_CNY, &p15) == DECIMAL_OK);
    money_t m1;
    assert(money_from_string("1.00", CURRENCY_CNY, &m1) == DECIMAL_OK);
    int64_t lot3 = tx_lot_create(pool, user_id, asset_id, 1003, "2026-01-10", q20, p15, m1);
    assert(lot3 > 0);

    open_lots = tx_lots_get_open(pool, user_id, asset_id);
    assert(open_lots != NULL);
    assert(csilk_json_array_size(open_lots) == 1);
    csilk_json_free(open_lots);

    rc = tx_lots_delete_by_asset(pool, user_id, asset_id);
    assert(rc == 0);

    open_lots = tx_lots_get_open(pool, user_id, asset_id);
    assert(open_lots != NULL);
    assert(csilk_json_array_size(open_lots) == 0);
    csilk_json_free(open_lots);

    printf("=== All Transaction Lots Repository Unit Tests PASSED! ===\n");
    return 0;
}
