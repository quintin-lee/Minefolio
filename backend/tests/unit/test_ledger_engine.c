#include "core/ledger/ledger_engine.h"
#include "common/db.h"
#include "common/config.h"
#include "repositories/transaction_repo.h"
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
    setenv("MINEFOLIO_JWT_SECRET", "test_ledger_engine_jwt_secret_32bytes", 1);

    csilk_db_pool_t* pool = NULL;
    int rc = db_init(&pool);
    assert(rc == 0);
    assert(pool != NULL);

    // Apply migration.sql
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
    printf("=== Running Integration Unit Test: Ledger Engine Core ===\n");
    const char* db_file = "/tmp/test_ledger_engine.db";
    init_test_db(db_file);
    csilk_db_pool_t* pool = db_get_pool();

    // 1. Seed User & Categories & Assets
    int64_t user_id = 1;
    csilk_db_exec(pool, "INSERT INTO users (id, username, password_hash) VALUES (1, 'tester', 'hash123')");
    csilk_db_exec(pool, "INSERT INTO categories (id, user_id, name, type, asset_type) VALUES (10, 1, 'Cash', 'expense', 'cash')");
    csilk_db_exec(pool, "INSERT INTO categories (id, user_id, name, type, asset_type) VALUES (20, 1, 'Stock', 'expense', 'stock')");

    // Funding Asset: Wallet (Initial Balance: 100,000 CNY)
    csilk_db_exec(pool, "INSERT INTO assets (id, user_id, category_id, name, currency, current_value, quantity, cost_basis, net_value) "
                        "VALUES (101, 1, 10, 'Wallet', 'CNY', 100000.0, 0, 0, 0)");

    // Investment Target Asset: Tech ETF (Initial: 0 shares, 0 cost)
    csilk_db_exec(pool, "INSERT INTO assets (id, user_id, category_id, name, currency, current_value, quantity, cost_basis, net_value) "
                        "VALUES (201, 1, 20, 'Tech ETF', 'CNY', 0, 0, 0, 0)");

    printf("  1. Testing Buy Transaction (1000 shares @ 10.00 CNY, Fee: 5.00 CNY)...\n");
    money_t buy1_amt, buy1_fee;
    price_t buy1_price;
    quantity_t buy1_qty;
    money_from_double(10000.0, CURRENCY_CNY, &buy1_amt);
    money_from_double(5.0, CURRENCY_CNY, &buy1_fee);
    price_from_double(10.0, 2, CURRENCY_CNY, &buy1_price);
    quantity_from_double(1000.0, 0, &buy1_qty);

    csilk_db_exec(pool, "BEGIN TRANSACTION");
    ledger_tx_t buy1_tx = {
        .id = 0,
        .user_id = user_id,
        .asset_id = 201,
        .linked_asset_id = 101,
        .category_id = 20,
        .type = LEDGER_TX_BUY,
        .type_str = "buy",
        .amount = buy1_amt,
        .price = buy1_price,
        .quantity = buy1_qty,
        .fee = buy1_fee,
        .tx_date = "2026-09-01 10:00:00",
        .note = "First Buy",
        .parent_tx_id = 0
    };
    int rc = ledger_apply_tx(pool, &buy1_tx);
    assert(rc == 0);
    assert(buy1_tx.id > 0);
    csilk_db_exec(pool, "COMMIT");

    // Verify Target Asset State after Buy 1
    csilk_json_t* ast = csilk_db_query_param_json(pool, "SELECT quantity, cost_basis, net_value, current_value FROM assets WHERE id=?", (const char*[]){"201", NULL});
    assert(ast && csilk_json_array_size(ast) == 1);
    csilk_json_t* row = csilk_json_array_get(ast, 0);
    assert(db_get_num(row, "quantity") == 1000.0);
    assert(db_get_num(row, "cost_basis") == 10005.0); // 10000 + 5 fee
    assert(db_get_num(row, "net_value") == 10.0);
    assert(db_get_num(row, "current_value") == 10000.0);
    csilk_json_free(ast);

    // Verify Funding Account State: 100000 - 10000 - 5 = 89995.0
    ast = csilk_db_query_param_json(pool, "SELECT current_value FROM assets WHERE id=?", (const char*[]){"101", NULL});
    assert(ast && csilk_json_array_size(ast) == 1);
    assert(db_get_num(csilk_json_array_get(ast, 0), "current_value") == 89995.0);
    csilk_json_free(ast);

    // Verify Fee Child Row exists linking to parent_tx_id
    csilk_json_t* fee_rows = tx_child_fee_rows(pool, user_id, buy1_tx.id);
    assert(fee_rows && csilk_json_array_size(fee_rows) == 1);
    assert(db_get_num(csilk_json_array_get(fee_rows, 0), "amount") == 5.0);
    csilk_json_free(fee_rows);
    printf("    ✓ Buy transaction & fee cascade verified.\n");

    printf("  2. Testing Second Buy (500 shares @ 14.00 CNY, Fee: 3.00 CNY)...\n");
    money_t buy2_amt, buy2_fee;
    price_t buy2_price;
    quantity_t buy2_qty;
    money_from_double(7000.0, CURRENCY_CNY, &buy2_amt);
    money_from_double(3.0, CURRENCY_CNY, &buy2_fee);
    price_from_double(14.0, 2, CURRENCY_CNY, &buy2_price);
    quantity_from_double(500.0, 0, &buy2_qty);

    csilk_db_exec(pool, "BEGIN TRANSACTION");
    ledger_tx_t buy2_tx = {
        .id = 0,
        .user_id = user_id,
        .asset_id = 201,
        .linked_asset_id = 101,
        .category_id = 20,
        .type = LEDGER_TX_BUY,
        .type_str = "buy",
        .amount = buy2_amt,
        .price = buy2_price,
        .quantity = buy2_qty,
        .fee = buy2_fee,
        .tx_date = "2026-09-02 10:00:00",
        .note = "Second Buy",
        .parent_tx_id = 0
    };
    rc = ledger_apply_tx(pool, &buy2_tx);
    assert(rc == 0);
    csilk_db_exec(pool, "COMMIT");

    // Total quantity: 1500, Total cost: 10005 + 7003 = 17008.0
    ast = csilk_db_query_param_json(pool, "SELECT quantity, cost_basis, net_value, current_value FROM assets WHERE id=?", (const char*[]){"201", NULL});
    row = csilk_json_array_get(ast, 0);
    assert(db_get_num(row, "quantity") == 1500.0);
    assert(db_get_num(row, "cost_basis") == 17008.0);
    assert(db_get_num(row, "net_value") == 14.0);
    assert(db_get_num(row, "current_value") == 21000.0); // 1500 * 14
    csilk_json_free(ast);
    printf("    ✓ Second buy & weighted cost basis verified.\n");

    printf("  3. Testing Partial Sell (600 shares @ 15.00 CNY, Fee: 10.00 CNY)...\n");
    money_t sell1_amt, sell1_fee;
    price_t sell1_price;
    quantity_t sell1_qty;
    money_from_double(9000.0, CURRENCY_CNY, &sell1_amt);
    money_from_double(10.0, CURRENCY_CNY, &sell1_fee);
    price_from_double(15.0, 2, CURRENCY_CNY, &sell1_price);
    quantity_from_double(600.0, 0, &sell1_qty);

    csilk_db_exec(pool, "BEGIN TRANSACTION");
    ledger_tx_t sell1_tx = {
        .id = 0,
        .user_id = user_id,
        .asset_id = 201,
        .linked_asset_id = 101,
        .category_id = 20,
        .type = LEDGER_TX_SELL,
        .type_str = "sell",
        .amount = sell1_amt,
        .price = sell1_price,
        .quantity = sell1_qty,
        .fee = sell1_fee,
        .tx_date = "2026-09-03 10:00:00",
        .note = "Partial Sell",
        .parent_tx_id = 0
    };
    rc = ledger_apply_tx(pool, &sell1_tx);
    assert(rc == 0);
    csilk_db_exec(pool, "COMMIT");

    // Remaining Quantity: 900, Cost Reduction: 6803.20, Remaining Cost: 10204.80, NAV: 14.0, Value: 12600.0
    ast = csilk_db_query_param_json(pool, "SELECT quantity, cost_basis, net_value, current_value FROM assets WHERE id=?", (const char*[]){"201", NULL});
    row = csilk_json_array_get(ast, 0);
    assert(db_get_num(row, "quantity") == 900.0);
    assert(db_get_num(row, "cost_basis") == 10204.80);
    assert(db_get_num(row, "net_value") == 14.0);
    assert(db_get_num(row, "current_value") == 12600.0); // 900 * 14
    csilk_json_free(ast);
    printf("    ✓ Partial sell & proportional cost basis reduction verified.\n");

    printf("  4. Testing Rebuild State Invariant (original state == rebuild state)...\n");
    // Snapshot current state
    double orig_q = 900.0, orig_cost = 10204.80, orig_nv = 14.0, orig_cv = 12600.0;

    // Zero out target asset materialized state
    csilk_db_exec(pool, "UPDATE assets SET quantity=0, cost_basis=0, net_value=0, current_value=0 WHERE id=201");

    // Execute Rebuild Engine
    ledger_position_state_t rebuilt;
    rc = ledger_rebuild_position(pool, user_id, 201, &rebuilt);
    assert(rc == 0);

    ast = csilk_db_query_param_json(pool, "SELECT quantity, cost_basis, net_value, current_value FROM assets WHERE id=?", (const char*[]){"201", NULL});
    row = csilk_json_array_get(ast, 0);
    assert(db_get_num(row, "quantity") == orig_q);
    assert(db_get_num(row, "cost_basis") == orig_cost);
    assert(db_get_num(row, "net_value") == orig_nv);
    assert(db_get_num(row, "current_value") == orig_cv);
    csilk_json_free(ast);
    printf("    ✓ Rebuild Engine: original state == rebuild state verified!\n");

    printf("  5. Testing Transaction Reversal (Rolling back Sell transaction)...\n");
    csilk_db_exec(pool, "BEGIN TRANSACTION");
    rc = ledger_reverse_tx(pool, user_id, sell1_tx.id);
    assert(rc == 0);
    csilk_db_exec(pool, "COMMIT");

    // After reversing sell, position should be restored back to 1500 shares & 17008.0 cost!
    ast = csilk_db_query_param_json(pool, "SELECT quantity, cost_basis, current_value FROM assets WHERE id=?", (const char*[]){"201", NULL});
    row = csilk_json_array_get(ast, 0);
    assert(db_get_num(row, "quantity") == 1500.0);
    assert(db_get_num(row, "cost_basis") == 17008.0);
    csilk_json_free(ast);
    printf("    ✓ Transaction reversal & state restoration verified.\n");

    unlink(db_file);
    printf("🎉 ALL LEDGER ENGINE INTEGRATION TESTS PASSED SUCCESSFULLY!\n");
    return 0;
}
