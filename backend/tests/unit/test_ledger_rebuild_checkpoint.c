#include "core/ledger/ledger_engine.h"
#include "infrastructure/repositories/ledger_snapshot_repo_impl.h"
#include "application/asset/usecases.h"
#include "application/cashflow/usecases.h"
#include "application/transaction/usecases.h"
#include "application/transfer/usecases.h"
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
    ledger_register_balance_hooks();
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

    /* -------------------------------------------------------------
     * Test 5: Backdated transaction invalidation scenario (RF-1)
     * ------------------------------------------------------------- */
    printf("Test 5: Backdated transaction invalidation scenario (RF-1)...\n");
    // Verify snapshot at 2026-09-02 still exists
    snap = NULL;
    rc = ledger_snapshot_get_latest(pool, user_id, 201, NULL, &snap);
    assert(rc == 0 && snap != NULL);
    csilk_json_free(snap);

    // Restore tx1 amount to original 1000.0
    csilk_db_exec(pool, "UPDATE transactions SET amount = 1000.0 WHERE id = 1");

    // Insert backdated tx 4 at 2026-09-01 15:00:00: Buy 1000 shares @ 3.00 CNY, amount 3000.00 CNY
    money_t    b4_amt, b4_fee;
    price_t    b4_price;
    quantity_t b4_qty;
    money_from_double(3000.0, CURRENCY_CNY, &b4_amt);
    money_from_double(0.0, CURRENCY_CNY, &b4_fee);
    price_from_double(3.0, 2, CURRENCY_CNY, &b4_price);
    quantity_from_double(1000.0, 0, &b4_qty);

    csilk_db_exec(pool, "BEGIN TRANSACTION");
    ledger_tx_t tx4 = {.id = 0,
                       .user_id = user_id,
                       .asset_id = 201,
                       .linked_asset_id = 101,
                       .category_id = 20,
                       .type = LEDGER_TX_BUY,
                       .type_str = "buy",
                       .amount = b4_amt,
                       .price = b4_price,
                       .quantity = b4_qty,
                       .fee = b4_fee,
                       .tx_date = "2026-09-01 15:00:00",
                       .note = "Backdated Buy 4",
                       .parent_tx_id = 0};
    rc = ledger_apply_tx(pool, &tx4);
    assert(rc == 0);
    assert(tx4.id > 0);
    csilk_db_exec(pool, "COMMIT");

    // Invalidate snapshots from 2026-09-01 15:00:00 onwards
    rc = ledger_snapshot_invalidate_from(pool, user_id, 201, "2026-09-01 15:00:00");
    assert(rc == 0);

    // Assert latest snapshot is now gone
    snap = NULL;
    rc = ledger_snapshot_get_latest(pool, user_id, 201, NULL, &snap);
    assert(rc == 0);
    assert(snap == NULL);

    // Rebuild position and verify position is rebuilt from scratch incorporating tx4
    ledger_position_state_t pos5;
    memset(&pos5, 0, sizeof(pos5));
    rc = ledger_rebuild_position(pool, user_id, 201, &pos5);
    assert(rc == 0);
    assert(quantity_to_double(pos5.quantity) == 2500.0);
    assert(money_to_double(pos5.cost_basis) == 5000.0);
    assert(money_to_double(pos5.realized_pnl) == 500.0);
    assert(price_to_double(pos5.net_value) == 2.0);
    assert(money_to_double(pos5.current_value) == 5000.0);

    /* -------------------------------------------------------------
     * Test 6: Engine-centralized snapshot invalidation across all mutation paths
     *         including shared-ledger editor invalidating owner's snapshot
     * ------------------------------------------------------------- */
    printf(
        "Test 6: Centralized snapshot invalidation "
        "(apply/reverse/transfer/expense/cashflow/shared)...\n");

    // Seed shared ledger & member user 2 (editor) for asset 201 and 101
    csilk_db_exec(pool, "INSERT INTO users (id, username, password) VALUES (2, 'editor_bob', 'hash456')");
    csilk_db_exec(pool, "INSERT INTO ledgers (id, owner_id, name) VALUES (1, 1, 'Shared Ledger')");
    csilk_db_exec(pool,
                  "INSERT INTO ledger_members (ledger_id, user_id, role) "
                  "VALUES (1, 1, 'owner'), (1, 2, 'editor')");
    csilk_db_exec(pool, "UPDATE assets SET ledger_id = 1 WHERE id IN (101, 201)");

    // Save a snapshot at 2026-09-10 for asset 201 (owner=1)
    assert(ledger_snapshot_save(
               pool, user_id, 201, "2026-09-10", &pos5, zero_bal, "2026-09-10 10:00:00", tx4.id, "[]") == 0);

    // 6a. Backdated ledger_apply_tx WITHOUT manual ledger_snapshot_invalidate_from must auto-invalidate
    csilk_db_exec(pool, "BEGIN TRANSACTION");
    ledger_tx_t tx6a = {.id = 0,
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
                        .tx_date = "2026-09-05 10:00:00",
                        .note = "Auto invalidate apply_tx",
                        .ledger_id = 1};
    assert(ledger_apply_tx(pool, &tx6a) == 0);
    csilk_db_exec(pool, "COMMIT");
    snap = NULL;
    assert(ledger_snapshot_get_latest(pool, user_id, 201, NULL, &snap) == 0);
    assert(snap == NULL);

    // 6b. Shared-ledger editor (user_id=2) creating & reversing tx6b must invalidate owner's (user_id=1) snapshot
    money_t xfer_amt;
    money_from_double(100.0, CURRENCY_CNY, &xfer_amt);
    csilk_db_exec(pool, "BEGIN TRANSACTION");
    ledger_tx_t tx6b = {.id = 0,
                        .user_id = 2,
                        .asset_id = 101,
                        .linked_asset_id = 201,
                        .category_id = 10,
                        .type = LEDGER_TX_TRANSFER_OUT,
                        .type_str = "transfer_out",
                        .amount = xfer_amt,
                        .price = price_zero(CURRENCY_CNY),
                        .quantity = quantity_zero(),
                        .fee = zero_bal,
                        .tx_date = "2026-09-05 12:00:00",
                        .note = "Shared editor xfer",
                        .ledger_id = 1};
    assert(ledger_apply_tx(pool, &tx6b) == 0);
    csilk_db_exec(pool, "COMMIT");

    assert(ledger_snapshot_save(
               pool, user_id, 201, "2026-09-10", &pos5, zero_bal, "2026-09-10 10:00:00", tx6a.id, "[]") == 0);
    assert(ledger_snapshot_save(
               pool, user_id, 101, "2026-09-10", &pos5, zero_bal, "2026-09-10 10:00:00", tx6a.id, "[]") == 0);
    csilk_db_exec(pool, "BEGIN TRANSACTION");
    assert(ledger_reverse_tx(pool, 2, tx6b.id) == 0);
    csilk_db_exec(pool, "COMMIT");
    snap = NULL;
    assert(ledger_snapshot_get_latest(pool, user_id, 201, NULL, &snap) == 0 && snap == NULL);
    assert(ledger_snapshot_get_latest(pool, user_id, 101, NULL, &snap) == 0 && snap == NULL);

    // 6c. Backdated transfer (ledger_apply_transfer_dated) auto-invalidates both assets
    assert(ledger_snapshot_save(
               pool, user_id, 101, "2026-09-10", &pos5, zero_bal, "2026-09-10 10:00:00", tx4.id, "[]") == 0);
    assert(ledger_snapshot_save(
               pool, user_id, 201, "2026-09-10", &pos5, zero_bal, "2026-09-10 10:00:00", tx4.id, "[]") == 0);
    assert(ledger_apply_transfer_dated(
               pool, user_id, 101, 201, xfer_amt, 9001, "Backdated xfer", "2026-09-08") == 0);
    assert(ledger_snapshot_get_latest(pool, user_id, 101, NULL, &snap) == 0 && snap == NULL);
    assert(ledger_snapshot_get_latest(pool, user_id, 201, NULL, &snap) == 0 && snap == NULL);

    // 6d. Backdated daily expense apply & reverse auto-invalidate target snapshot
    assert(ledger_snapshot_save(
               pool, user_id, 101, "2026-09-10", &pos5, zero_bal, "2026-09-10 10:00:00", tx4.id, "[]") == 0);
    assert(ledger_apply_expense_dated(
               pool, user_id, 101, xfer_amt, 0, 9002, "Backdated exp", "2026-09-07") == 0);
    assert(ledger_snapshot_get_latest(pool, user_id, 101, NULL, &snap) == 0 && snap == NULL);

    assert(ledger_snapshot_save(
               pool, user_id, 101, "2026-09-10", &pos5, zero_bal, "2026-09-10 10:00:00", tx4.id, "[]") == 0);
    assert(ledger_reverse_expense_dated(
               pool, user_id, 101, xfer_amt, 0, 9002, "Reverse exp", "2026-09-07") == 0);
    assert(ledger_snapshot_get_latest(pool, user_id, 101, NULL, &snap) == 0 && snap == NULL);

    // 6e. Cashflow confirm income auto-invalidates target asset snapshot while preserving earlier snapshot
    assert(ledger_snapshot_save(
               pool, user_id, 101, "2026-09-01", &pos5, zero_bal, "2026-09-01 10:00:00", tx1.id, "[]") == 0);
    assert(ledger_snapshot_save(
               pool, user_id, 101, "2026-09-10", &pos5, zero_bal, "2026-09-10 10:00:00", tx4.id, "[]") == 0);
    confirm_cashflow_cmd_t cf_cmd = {
        .user_id = user_id,
        .source_asset_id = 201,
        .target_asset_id = 101,
        .amount = 50.0,
        .date = "2026-09-06",
        .name = "Dividend",
        .note = "Q3",
    };
    cashflow_usecase_result_t cf_res = {0};
    int64_t                   cf_tx_id = 0;
    assert(cashflow_usecase_confirm_income(pool, &cf_cmd, &cf_tx_id, &cf_res) == 0 && cf_tx_id > 0);
    assert(ledger_snapshot_get_latest(pool, user_id, 101, NULL, &snap) == 0 && snap != NULL);
    assert(strcmp(csilk_json_get_string(snap, "snapshot_date"), "2026-09-01") == 0);
    csilk_json_free(snap);

    /* -------------------------------------------------------------
     * Test 7: Non-investment asset_usecase_update appends adjustment log
     *         so ledger_rebuild_account preserves updated balance;
     *         investment asset_usecase_update invalidates ledger_snapshots
     * ------------------------------------------------------------- */
    printf("Test 7: asset_usecase_update adjustment log + snapshot invalidation...\n");
    create_asset_cmd_t create_cash_cmd = {
        .user_id = user_id,
        .ledger_id = 1,
        .category_id = 10,
        .name = "Adjustment Cash",
        .current_value = 1000.0,
        .currency = "CNY",
    };
    asset_usecase_result_t a_res = {0};
    assert(asset_usecase_create(pool, &create_cash_cmd, &a_res) == 0 && a_res.id > 0);
    int64_t cash_adj_id = a_res.id;

    update_asset_cmd_t upd_cash_cmd = {
        .user_id = user_id,
        .id = cash_adj_id,
        .name = "Adjustment Cash",
        .current_value = 2500.50,
    };
    memset(&a_res, 0, sizeof(a_res));
    assert(asset_usecase_update(pool, &upd_cash_cmd, &a_res) == 0 && a_res.code == 0);

    char cash_id_str[32];
    snprintf(cash_id_str, sizeof(cash_id_str), "%lld", (long long)cash_adj_id);
    csilk_json_t* adj_logs = csilk_db_query_param_json(
        pool,
        "SELECT source_type, delta, balance_after FROM asset_balance_logs "
        "WHERE asset_id=? ORDER BY id ASC",
        (const char*[]){cash_id_str, NULL});
    assert(adj_logs && csilk_json_array_size(adj_logs) >= 2);
    csilk_json_t* last_log =
        csilk_json_array_get(adj_logs, csilk_json_array_size(adj_logs) - 1);
    assert(strcmp(csilk_json_get_string(last_log, "source_type"), "adjustment") == 0);
    assert(fabs(db_get_num(last_log, "delta") - 1500.50) < 0.001);
    assert(fabs(db_get_num(last_log, "balance_after") - 2500.50) < 0.001);
    csilk_json_free(adj_logs);

    ledger_account_state_t rebuilt_acc = {0};
    assert(ledger_rebuild_account(pool, user_id, cash_adj_id, &rebuilt_acc) == 0);
    assert(fabs(money_to_double(rebuilt_acc.balance) - 2500.50) < 0.001);

    // Investment update must invalidate stale ledger_snapshots
    assert(ledger_snapshot_save(
               pool, user_id, 201, "2026-09-10", &pos5, zero_bal, "2026-09-10 10:00:00", tx4.id, "[]") ==
           0);
    update_asset_cmd_t upd_inv_cmd = {
        .user_id = user_id,
        .id = 201,
        .has_net_value = true,
        .net_value = 3.5,
    };
    memset(&a_res, 0, sizeof(a_res));
    assert(asset_usecase_update(pool, &upd_inv_cmd, &a_res) == 0 && a_res.code == 0);
    snap = NULL;
    assert(ledger_snapshot_get_latest(pool, user_id, 201, NULL, &snap) == 0 && snap == NULL);

    /* -------------------------------------------------------------
     * Test 8: tx_usecase_update rejects transfer_out/transfer_in (1002)
     *         and tx_usecase_create rejects cross-ledger assets (1002)
     * ------------------------------------------------------------- */
    printf("Test 8: Transfer edit immutability & cross-ledger tx create rejection...\n");
    csilk_db_exec(pool, "BEGIN TRANSACTION");
    ledger_tx_t tx_xfer = {.id = 0,
                           .user_id = user_id,
                           .asset_id = 101,
                           .linked_asset_id = cash_adj_id,
                           .category_id = 10,
                           .type = LEDGER_TX_TRANSFER_OUT,
                           .type_str = "transfer_out",
                           .amount = xfer_amt,
                           .price = price_zero(CURRENCY_CNY),
                           .quantity = quantity_zero(),
                           .fee = zero_bal,
                           .tx_date = "2026-09-12 10:00:00",
                           .note = "Immutable xfer",
                           .ledger_id = 1};
    assert(ledger_apply_tx(pool, &tx_xfer) == 0 && tx_xfer.id > 0);
    csilk_db_exec(pool, "COMMIT");

    update_tx_cmd_t upd_xfer_out = {
        .user_id = user_id,
        .ledger_id = 1,
        .tx_id = tx_xfer.id,
        .amount = 200.0,
        .currency = "CNY",
        .date = "2026-09-12 10:00:00",
    };
    tx_usecase_result_t tx_res = {0};
    assert(tx_usecase_update(pool, &upd_xfer_out, &tx_res) != 0 && tx_res.code == 1002);

    // Also reject updating a real transfer_in row (created via transfer_usecase_create)
    create_transfer_cmd_t xfer_cmd = {
        .user_id = user_id,
        .from_asset_id = 101,
        .to_asset_id = cash_adj_id,
        .amount = 50.0,
        .currency = "CNY",
        .transfer_date = "2026-09-12 11:00:00",
        .note = "Transfer pair",
        .ledger_id = 1,
    };
    transfer_usecase_result_t xfer_res = {0};
    assert(transfer_usecase_create(pool, &xfer_cmd, &xfer_res) == 0);

    csilk_json_t* tin_rows = csilk_db_query_param_json(
        pool,
        "SELECT id FROM transactions WHERE transaction_type='transfer_in' ORDER BY id DESC LIMIT 1",
        (const char*[]){NULL});
    assert(tin_rows && csilk_json_array_size(tin_rows) == 1);
    int64_t tin_id = db_get_int(csilk_json_array_get(tin_rows, 0), "id");
    csilk_json_free(tin_rows);

    update_tx_cmd_t upd_xfer_in = upd_xfer_out;
    upd_xfer_in.tx_id = tin_id;
    memset(&tx_res, 0, sizeof(tx_res));
    assert(tx_usecase_update(pool, &upd_xfer_in, &tx_res) != 0 && tx_res.code == 1002);

    // Also reject mutating an existing non-transfer tx into transfer_out
    update_tx_cmd_t upd_to_xfer = {
        .user_id = user_id,
        .ledger_id = 1,
        .tx_id = tx1.id,
        .type = "transfer_out",
        .amount = 100.0,
        .currency = "CNY",
        .date = "2026-09-01 10:00:00",
    };
    memset(&tx_res, 0, sizeof(tx_res));
    assert(tx_usecase_update(pool, &upd_to_xfer, &tx_res) != 0 && tx_res.code == 1002);

    // Cross-ledger tx_usecase_create rejection
    csilk_db_exec(pool,
                  "INSERT INTO ledgers (id, owner_id, name, currency, is_default) "
                  "VALUES (2, 1, 'Ledger 2', 'CNY', 0)");
    create_asset_cmd_t create_l2_cash = {
        .user_id = user_id,
        .ledger_id = 2,
        .category_id = 10,
        .name = "Ledger2 Cash",
        .current_value = 500.0,
        .currency = "CNY",
    };
    memset(&a_res, 0, sizeof(a_res));
    assert(asset_usecase_create(pool, &create_l2_cash, &a_res) == 0 && a_res.id > 0);
    int64_t l2_cash_id = a_res.id;

    create_tx_cmd_t cross_tx_1 = {
        .user_id = user_id,
        .ledger_id = 1,
        .asset_id = l2_cash_id,
        .category_id = 10,
        .type = "income",
        .amount = 50.0,
        .currency = "CNY",
        .date = "2026-09-13",
    };
    memset(&tx_res, 0, sizeof(tx_res));
    assert(tx_usecase_create(pool, &cross_tx_1, &tx_res) != 0 && tx_res.code == 1002);

    create_tx_cmd_t cross_tx_2 = {
        .user_id = user_id,
        .ledger_id = 1,
        .asset_id = 201,
        .linked_asset_id = l2_cash_id,
        .category_id = 20,
        .type = "buy",
        .amount = 100.0,
        .price = 10.0,
        .quantity = 10.0,
        .currency = "CNY",
        .date = "2026-09-13",
    };
    memset(&tx_res, 0, sizeof(tx_res));
    assert(tx_usecase_create(pool, &cross_tx_2, &tx_res) != 0 && tx_res.code == 1002);

    // Cross-ledger tx_usecase_update rejection (target asset_id / linked_asset_id)
    create_asset_cmd_t create_l2_stock = {
        .user_id = user_id,
        .ledger_id = 2,
        .category_id = 20,
        .name = "Ledger2 Stock",
        .current_value = 0.0,
        .currency = "CNY",
    };
    memset(&a_res, 0, sizeof(a_res));
    assert(asset_usecase_create(pool, &create_l2_stock, &a_res) == 0 && a_res.id > 0);
    int64_t l2_stock_id = a_res.id;

    // 1) Update tx1 (in ledger 1) to target asset in ledger 2 -> 1002
    update_tx_cmd_t upd_cross_asset = {
        .user_id = user_id,
        .ledger_id = 1,
        .tx_id = tx1.id,
        .asset_id = l2_stock_id,
        .has_asset_id = true,
        .amount = 1000.0,
        .price = 1.0,
        .quantity = 1000.0,
        .currency = "CNY",
        .date = "2026-09-01 10:00:00",
    };
    memset(&tx_res, 0, sizeof(tx_res));
    assert(tx_usecase_update(pool, &upd_cross_asset, &tx_res) != 0 && tx_res.code == 1002);

    // 2) Update tx1 (in ledger 1) to cross-ledger linked_asset_id -> 1002
    update_tx_cmd_t upd_cross_linked = {
        .user_id = user_id,
        .ledger_id = 1,
        .tx_id = tx1.id,
        .asset_id = 201,
        .has_asset_id = true,
        .linked_asset_id = l2_cash_id,
        .has_linked_asset_id = true,
        .amount = 1000.0,
        .price = 1.0,
        .quantity = 1000.0,
        .currency = "CNY",
        .date = "2026-09-01 10:00:00",
    };
    memset(&tx_res, 0, sizeof(tx_res));
    assert(tx_usecase_update(pool, &upd_cross_linked, &tx_res) != 0 && tx_res.code == 1002);

    // 3) Update tx1 with cmd->ledger_id=0 and mismatched asset/linked ledgers -> 1002
    update_tx_cmd_t upd_cross_pair = upd_cross_linked;
    upd_cross_pair.ledger_id = 0;
    memset(&tx_res, 0, sizeof(tx_res));
    assert(tx_usecase_update(pool, &upd_cross_pair, &tx_res) != 0 && tx_res.code == 1002);

    printf("=== All Ledger Rebuild Checkpoint Tests Passed! ===\n");
    return 0;
}
