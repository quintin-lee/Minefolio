#include "core/ledger/ledger_engine.h"
#include "infrastructure/repositories/ledger_snapshot_repo_impl.h"
#include "common/db.h"
#include "common/config.h"
#include "csilk/csilk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

static void
init_test_db(const char* db_path)
{
    unlink(db_path);
    setenv("MINEFOLIO_DB_DRIVER", "sqlite", 1);
    setenv("MINEFOLIO_DB_DSN", db_path, 1);
    setenv("MINEFOLIO_JWT_SECRET", "test_ledger_checkpoint_secret_32bytes", 1);

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
    printf("=== Running Unit Test: Ledger Rebuild Checkpointing (TDD) ===\n");
    const char* db_file = "/tmp/test_ledger_rebuild_checkpoint.db";
    init_test_db(db_file);
    csilk_db_pool_t* pool = db_get_pool();

    int64_t user_id = 1;
    // Seed user & categories
    csilk_db_exec(pool,
                  "INSERT INTO users (id, username, password) VALUES (1, 'tester', 'hash123')");
    csilk_db_exec(pool,
                  "INSERT INTO categories (id, user_id, name, type, asset_type) VALUES (10, 1, "
                  "'Cash', 'expense', 'cash')");
    csilk_db_exec(pool,
                  "INSERT INTO categories (id, user_id, name, type, asset_type) VALUES (20, 1, "
                  "'Stock', 'expense', 'stock')");

    // Funding Asset: Wallet (Initial: 100,000 CNY)
    csilk_db_exec(pool,
                  "INSERT INTO assets (id, user_id, category_id, name, currency, current_value, "
                  "quantity, cost_basis, net_value) "
                  "VALUES (101, 1, 10, 'Wallet', 'CNY', 100000.0, 0, 0, 0)");

    // Asset 301: Zero-tx initial asset (Baseline test RF-4)
    csilk_db_exec(pool,
                  "INSERT INTO assets (id, user_id, category_id, name, currency, current_value, "
                  "quantity, cost_basis, net_value) "
                  "VALUES (301, 1, 20, 'Initial Fund', 'CNY', 1500.0, 100.0, 1500.0, 15.0)");

    // Asset 201: Target Stock for buy/sell & checkpoints
    csilk_db_exec(pool,
                  "INSERT INTO assets (id, user_id, category_id, name, currency, current_value, "
                  "quantity, cost_basis, net_value) "
                  "VALUES (201, 1, 20, 'Alpha Corp', 'CNY', 0, 0, 0, 0)");

    /* -------------------------------------------------------------
     * Test 1: Zero-transaction baseline asset rebuild preserves initial metadata (RF-4)
     * ------------------------------------------------------------- */
    printf("Test 1: Zero-transaction baseline asset rebuild (RF-4)...\n");
    ledger_position_state_t zero_pos;
    memset(&zero_pos, 0, sizeof(zero_pos));
    int rc = ledger_rebuild_position(pool, user_id, 301, &zero_pos);
    assert(rc == 0);
    assert(quantity_to_double(zero_pos.quantity) == 100.0);
    assert(money_to_double(zero_pos.cost_basis) == 1500.0);
    assert(price_to_double(zero_pos.net_value) == 15.0);
    assert(money_to_double(zero_pos.current_value) == 1500.0);

    /* -------------------------------------------------------------
     * Test 2: Asset with 2 buy transactions rebuilt from zero correctly
     * ------------------------------------------------------------- */
    printf("Test 2: Rebuilding asset with 2 buy transactions from zero...\n");
    // Tx 1: Buy 1000 shares @ 1.00 CNY, amount 1000.00 CNY
    money_t    b1_amt, b1_fee;
    price_t    b1_price;
    quantity_t b1_qty;
    money_from_double(1000.0, CURRENCY_CNY, &b1_amt);
    money_from_double(0.0, CURRENCY_CNY, &b1_fee);
    price_from_double(1.0, 2, CURRENCY_CNY, &b1_price);
    quantity_from_double(1000.0, 0, &b1_qty);

    csilk_db_exec(pool, "BEGIN TRANSACTION");
    ledger_tx_t tx1 = {.id = 0,
                       .user_id = user_id,
                       .asset_id = 201,
                       .linked_asset_id = 101,
                       .category_id = 20,
                       .type = LEDGER_TX_BUY,
                       .type_str = "buy",
                       .amount = b1_amt,
                       .price = b1_price,
                       .quantity = b1_qty,
                       .fee = b1_fee,
                       .tx_date = "2026-09-01 10:00:00",
                       .note = "Buy 1",
                       .parent_tx_id = 0};
    rc = ledger_apply_tx(pool, &tx1);
    assert(rc == 0);
    assert(tx1.id > 0);
    csilk_db_exec(pool, "COMMIT");

    // Tx 2: Buy 1000 shares @ 2.00 CNY, amount 2000.00 CNY
    money_t    b2_amt, b2_fee;
    price_t    b2_price;
    quantity_t b2_qty;
    money_from_double(2000.0, CURRENCY_CNY, &b2_amt);
    money_from_double(0.0, CURRENCY_CNY, &b2_fee);
    price_from_double(2.0, 2, CURRENCY_CNY, &b2_price);
    quantity_from_double(1000.0, 0, &b2_qty);

    csilk_db_exec(pool, "BEGIN TRANSACTION");
    ledger_tx_t tx2 = {.id = 0,
                       .user_id = user_id,
                       .asset_id = 201,
                       .linked_asset_id = 101,
                       .category_id = 20,
                       .type = LEDGER_TX_BUY,
                       .type_str = "buy",
                       .amount = b2_amt,
                       .price = b2_price,
                       .quantity = b2_qty,
                       .fee = b2_fee,
                       .tx_date = "2026-09-02 10:00:00",
                       .note = "Buy 2",
                       .parent_tx_id = 0};
    rc = ledger_apply_tx(pool, &tx2);
    assert(rc == 0);
    assert(tx2.id > 0);
    csilk_db_exec(pool, "COMMIT");

    // Rebuild position from zero (no snapshot exists yet)
    ledger_position_state_t pos2;
    memset(&pos2, 0, sizeof(pos2));
    rc = ledger_rebuild_position(pool, user_id, 201, &pos2);
    assert(rc == 0);
    assert(quantity_to_double(pos2.quantity) == 2000.0);
    assert(money_to_double(pos2.cost_basis) == 3000.0);
    assert(price_to_double(pos2.net_value) == 2.0);
    assert(money_to_double(pos2.current_value) == 4000.0);
    assert(money_to_double(pos2.realized_pnl) == 0.0);

    /* -------------------------------------------------------------
     * Test 3: Save checkpoint snapshot
     * ------------------------------------------------------------- */
    printf("Test 3: Saving checkpoint snapshot...\n");
    money_t     zero_bal = money_zero(CURRENCY_CNY);
    const char* lots_json_sample = "[{\"lot_id\":1,\"qty\":\"1000.00000000\",\"price\":\"1.00\"}]";
    rc = ledger_snapshot_save(pool,
                              user_id,
                              201,
                              "2026-09-02",
                              &pos2,
                              zero_bal,
                              "2026-09-02 10:00:00",
                              tx2.id,
                              lots_json_sample);
    assert(rc == 0);

    // Retrieve latest snapshot
    csilk_json_t* snap = NULL;
    rc = ledger_snapshot_get_latest(pool, user_id, 201, NULL, &snap);
    assert(rc == 0);
    assert(snap != NULL);
    assert(db_get_int(snap, "tx_checkpoint_id") == tx2.id);
    const char* snap_date = csilk_json_get_string(snap, "snapshot_date");
    assert(snap_date != NULL && strcmp(snap_date, "2026-09-02") == 0);
    const char* lots_stored = csilk_json_get_string(snap, "lots_snapshot_json");
    assert(lots_stored != NULL && strcmp(lots_stored, lots_json_sample) == 0);
    csilk_json_free(snap);

    // Query with earlier target_date returns NULL
    rc = ledger_snapshot_get_latest(pool, user_id, 201, "2026-08-31", &snap);
    assert(rc == 0);
    assert(snap == NULL);

    // Query with matching target_date returns snapshot
    rc = ledger_snapshot_get_latest(pool, user_id, 201, "2026-09-02", &snap);
    assert(rc == 0);
    assert(snap != NULL);
    csilk_json_free(snap);

    /* -------------------------------------------------------------
     * Test 4: Insert transaction 3 (sell), rebuild incrementally from snapshot
     * ------------------------------------------------------------- */
    printf("Test 4: Insert sell tx and verify incremental rebuild from snapshot...\n");
    // Tx 3: Sell 500 shares @ 3.00 CNY, amount 1500.00 CNY
    money_t    s3_amt, s3_fee;
    price_t    s3_price;
    quantity_t s3_qty;
    money_from_double(1500.0, CURRENCY_CNY, &s3_amt);
    money_from_double(0.0, CURRENCY_CNY, &s3_fee);
    price_from_double(3.0, 2, CURRENCY_CNY, &s3_price);
    quantity_from_double(500.0, 0, &s3_qty);

    csilk_db_exec(pool, "BEGIN TRANSACTION");
    ledger_tx_t tx3 = {.id = 0,
                       .user_id = user_id,
                       .asset_id = 201,
                       .linked_asset_id = 101,
                       .category_id = 20,
                       .type = LEDGER_TX_SELL,
                       .type_str = "sell",
                       .amount = s3_amt,
                       .price = s3_price,
                       .quantity = s3_qty,
                       .fee = s3_fee,
                       .tx_date = "2026-09-03 10:00:00",
                       .note = "Sell 1",
                       .parent_tx_id = 0};
    rc = ledger_apply_tx(pool, &tx3);
    assert(rc == 0);
    assert(tx3.id > 0);
    csilk_db_exec(pool, "COMMIT");

    // To strictly verify that incremental rebuild uses the snapshot checkpoint
    // and does NOT re-evaluate transactions prior to checkpoint, we tamper with tx1
    // (set tx1 amount to an invalid massive value). If rebuild replayed from zero,
    // the result would be wildly different.
    csilk_db_exec(pool, "UPDATE transactions SET amount = 999999.0 WHERE id = 1");

    ledger_position_state_t pos3;
    memset(&pos3, 0, sizeof(pos3));
    rc = ledger_rebuild_position(pool, user_id, 201, &pos3);
    assert(rc == 0);

    // Verify expectations: 1500 qty, 2250 cost, 750 realized_pnl
    assert(quantity_to_double(pos3.quantity) == 1500.0);
    assert(money_to_double(pos3.cost_basis) == 2250.0);
    assert(money_to_double(pos3.realized_pnl) == 750.0);
    assert(price_to_double(pos3.net_value) == 2.0);
    assert(money_to_double(pos3.current_value) == 3000.0);

    printf("=== All Ledger Rebuild Checkpoint Tests Passed! ===\n");
    return 0;
}
