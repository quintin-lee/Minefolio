#include "core/ledger/ledger_engine.h"
#include "application/asset/usecases.h"
#include "application/asset/commands.h"
#include "application/asset/dtos.h"
#include "infrastructure/repositories/transaction_lot_repo_impl.h"
#include "infrastructure/repositories/ledger_snapshot_repo_impl.h"
#include "infrastructure/repositories/transaction_repo_impl.h"
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
    setenv("MINEFOLIO_JWT_SECRET", "test_cost_basis_fifo_secret_32bytes_key!", 1);

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
    printf("=== Running Unit Test: FIFO Cost Basis Accounting Engine (TDD) ===\n");
    const char* db_file = "/tmp/test_cost_basis_fifo.db";
    init_test_db(db_file);
    csilk_db_pool_t* pool = db_get_pool();
    assert(pool != NULL);

    int64_t user_id = 1;
    int64_t asset_id_a = 101;
    int64_t asset_id_b = 102;
    int64_t funding_asset_id = 100;

    // Seed fixtures
    csilk_db_exec(pool,
                  "INSERT INTO users (id, username, password) VALUES (1, 'fifo_tester', 'hash')");
    csilk_db_exec(pool,
                  "INSERT INTO ledgers (id, owner_id, name, is_default) VALUES (1, 1, 'Default', 1)");
    csilk_db_exec(pool,
                  "INSERT INTO accounts (id, user_id, ledger_id, name, institution) "
                  "VALUES (1, 1, 1, '默认账户', 'cash_wallet')");
    csilk_db_exec(pool,
                  "INSERT INTO categories (id, user_id, name, type, asset_type) "
                  "VALUES (10, 1, 'Stock', 'expense', 'stock')");

    // Cash funding asset with 1,000,000 CNY
    csilk_db_exec(
        pool,
        "INSERT INTO assets (id, user_id, category_id, account_id, name, currency, current_value, "
        "quantity, cost_basis, net_value) "
        "VALUES (100, 1, 10, 1, 'Cash Account', 'CNY', 1000000.0, 0, 0, 0)");

    // FIFO Asset A
    csilk_db_exec(
        pool,
        "INSERT INTO assets (id, user_id, category_id, account_id, name, currency, current_value, "
        "quantity, cost_basis, net_value, cost_method) "
        "VALUES (101, 1, 10, 1, 'FIFO Stock A', 'CNY', 0, 0, 0, 0, 'fifo')");

    // FIFO Asset B
    csilk_db_exec(
        pool,
        "INSERT INTO assets (id, user_id, category_id, account_id, name, currency, current_value, "
        "quantity, cost_basis, net_value, cost_method) "
        "VALUES (102, 1, 10, 1, 'FIFO Stock B', 'CNY', 0, 0, 0, 0, 'fifo')");

    /* -------------------------------------------------------------
     * Scenario 1: Buy 100 @ 10 (Lot 1), Buy 100 @ 20 (Lot 2). Total = 200 shares, 3000 CNY.
     * ------------------------------------------------------------- */
    printf("Scenario 1: Testing Buy 100 @ 10 (Lot 1) & Buy 100 @ 20 (Lot 2)...\n");
    money_t b1_amt, b1_fee;
    price_t b1_price;
    quantity_t b1_qty;
    assert(money_from_string("1000.00", CURRENCY_CNY, &b1_amt) == DECIMAL_OK);
    assert(money_from_string("0.00", CURRENCY_CNY, &b1_fee) == DECIMAL_OK);
    assert(price_from_string("10.00", CURRENCY_CNY, &b1_price) == DECIMAL_OK);
    assert(quantity_from_string("100.00000000", &b1_qty) == DECIMAL_OK);

    ledger_tx_t buy1_tx = {
        .id = 0,
        .user_id = user_id,
        .asset_id = asset_id_a,
        .linked_asset_id = funding_asset_id,
        .category_id = 10,
        .type = LEDGER_TX_BUY,
        .type_str = "buy",
        .amount = b1_amt,
        .price = b1_price,
        .quantity = b1_qty,
        .fee = b1_fee,
        .tx_date = "2026-01-01 10:00:00",
        .note = "Buy 100 @ 10",
        .parent_tx_id = 0,
        .ledger_id = 1
    };
    int rc = ledger_apply_tx(pool, &buy1_tx);
    assert(rc == 0);
    assert(buy1_tx.id > 0);

    money_t b2_amt, b2_fee;
    price_t b2_price;
    quantity_t b2_qty;
    assert(money_from_string("2000.00", CURRENCY_CNY, &b2_amt) == DECIMAL_OK);
    assert(money_from_string("0.00", CURRENCY_CNY, &b2_fee) == DECIMAL_OK);
    assert(price_from_string("20.00", CURRENCY_CNY, &b2_price) == DECIMAL_OK);
    assert(quantity_from_string("100.00000000", &b2_qty) == DECIMAL_OK);

    ledger_tx_t buy2_tx = {
        .id = 0,
        .user_id = user_id,
        .asset_id = asset_id_a,
        .linked_asset_id = funding_asset_id,
        .category_id = 10,
        .type = LEDGER_TX_BUY,
        .type_str = "buy",
        .amount = b2_amt,
        .price = b2_price,
        .quantity = b2_qty,
        .fee = b2_fee,
        .tx_date = "2026-01-05 10:00:00",
        .note = "Buy 100 @ 20",
        .parent_tx_id = 0,
        .ledger_id = 1
    };
    rc = ledger_apply_tx(pool, &buy2_tx);
    assert(rc == 0);
    assert(buy2_tx.id > 0);

    // Verify Asset 101 totals: 200 shares, 3000 CNY cost basis
    csilk_json_t* a_rows = csilk_db_query_param_json(
        pool, "SELECT quantity, cost_basis FROM assets WHERE id=?", (const char*[]){"101", NULL});
    assert(a_rows && csilk_json_array_size(a_rows) == 1);
    assert(db_get_num(csilk_json_array_get(a_rows, 0), "quantity") == 200.0);
    assert(db_get_num(csilk_json_array_get(a_rows, 0), "cost_basis") == 3000.0);
    csilk_json_free(a_rows);

    // Verify open lots: 2 lots, Lot 1 = 100 @ 10, Lot 2 = 100 @ 20
    csilk_json_t* lots = tx_lots_get_open(pool, user_id, asset_id_a);
    assert(lots && csilk_json_array_size(lots) == 2);
    csilk_json_t* l1 = csilk_json_array_get(lots, 0);
    csilk_json_t* l2 = csilk_json_array_get(lots, 1);
    assert(db_get_num(l1, "remaining_quantity") == 100.0);
    assert(db_get_num(l1, "cost_per_unit") == 10.0);
    assert(strcmp(csilk_json_get_string(l1, "status"), "open") == 0);
    assert(db_get_num(l2, "remaining_quantity") == 100.0);
    assert(db_get_num(l2, "cost_per_unit") == 20.0);
    assert(strcmp(csilk_json_get_string(l2, "status"), "open") == 0);
    csilk_json_free(lots);
    printf("  ✓ Scenario 1 passed.\n");

    /* -------------------------------------------------------------
     * Scenario 2: Sell 50 shares @ 25 CNY: consumes 50 from Lot 1 @ 10.
     * Cost reduction = 500, Realized PnL = 750, Remaining cost = 2500.
     * ------------------------------------------------------------- */
    printf("Scenario 2: Testing Sell 50 @ 25 CNY (consumes 50 from Lot 1)...\n");
    money_t s1_amt, s1_fee;
    price_t s1_price;
    quantity_t s1_qty;
    assert(money_from_string("1250.00", CURRENCY_CNY, &s1_amt) == DECIMAL_OK);
    assert(money_from_string("0.00", CURRENCY_CNY, &s1_fee) == DECIMAL_OK);
    assert(price_from_string("25.00", CURRENCY_CNY, &s1_price) == DECIMAL_OK);
    assert(quantity_from_string("50.00000000", &s1_qty) == DECIMAL_OK);

    ledger_tx_t sell1_tx = {
        .id = 0,
        .user_id = user_id,
        .asset_id = asset_id_a,
        .linked_asset_id = funding_asset_id,
        .category_id = 10,
        .type = LEDGER_TX_SELL,
        .type_str = "sell",
        .amount = s1_amt,
        .price = s1_price,
        .quantity = s1_qty,
        .fee = s1_fee,
        .tx_date = "2026-01-10 10:00:00",
        .note = "Sell 50 @ 25",
        .parent_tx_id = 0,
        .ledger_id = 1
    };
    rc = ledger_apply_tx(pool, &sell1_tx);
    assert(rc == 0);
    assert(sell1_tx.id > 0);

    // Verify Asset 101 totals: 150 shares, 2500 CNY cost basis
    a_rows = csilk_db_query_param_json(
        pool, "SELECT quantity, cost_basis FROM assets WHERE id=?", (const char*[]){"101", NULL});
    assert(a_rows && csilk_json_array_size(a_rows) == 1);
    assert(db_get_num(csilk_json_array_get(a_rows, 0), "quantity") == 150.0);
    assert(db_get_num(csilk_json_array_get(a_rows, 0), "cost_basis") == 2500.0);
    csilk_json_free(a_rows);

    // Verify lots: Lot 1 remaining = 50, Lot 2 remaining = 100
    lots = tx_lots_get_open(pool, user_id, asset_id_a);
    assert(lots && csilk_json_array_size(lots) == 2);
    l1 = csilk_json_array_get(lots, 0);
    l2 = csilk_json_array_get(lots, 1);
    assert(db_get_num(l1, "remaining_quantity") == 50.0);
    assert(db_get_num(l1, "cost_per_unit") == 10.0);
    assert(db_get_num(l2, "remaining_quantity") == 100.0);
    assert(db_get_num(l2, "cost_per_unit") == 20.0);
    csilk_json_free(lots);
    printf("  ✓ Scenario 2 passed.\n");

    /* -------------------------------------------------------------
     * Scenario 3 (RF-1): Multi-lot consumption: Sell 100 shares @ 30 CNY:
     * Consumes remaining 50 from Lot 1 (cost 500) and 50 from Lot 2 (cost 1000).
     * Total cost reduction = 1500 CNY, Realized PnL = 1500 CNY.
     * Lot 1 closed, Lot 2 has 50 shares remaining @ 20 CNY.
     * ------------------------------------------------------------- */
    printf("Scenario 3 (RF-1): Testing multi-lot sell (Sell 100 @ 30 across Lot 1 & Lot 2)...\n");
    money_t s2_amt, s2_fee;
    price_t s2_price;
    quantity_t s2_qty;
    assert(money_from_string("3000.00", CURRENCY_CNY, &s2_amt) == DECIMAL_OK);
    assert(money_from_string("0.00", CURRENCY_CNY, &s2_fee) == DECIMAL_OK);
    assert(price_from_string("30.00", CURRENCY_CNY, &s2_price) == DECIMAL_OK);
    assert(quantity_from_string("100.00000000", &s2_qty) == DECIMAL_OK);

    ledger_tx_t sell2_tx = {
        .id = 0,
        .user_id = user_id,
        .asset_id = asset_id_a,
        .linked_asset_id = funding_asset_id,
        .category_id = 10,
        .type = LEDGER_TX_SELL,
        .type_str = "sell",
        .amount = s2_amt,
        .price = s2_price,
        .quantity = s2_qty,
        .fee = s2_fee,
        .tx_date = "2026-01-15 10:00:00",
        .note = "Sell 100 @ 30",
        .parent_tx_id = 0,
        .ledger_id = 1
    };
    rc = ledger_apply_tx(pool, &sell2_tx);
    assert(rc == 0);
    assert(sell2_tx.id > 0);

    // Verify Asset 101 totals: 50 shares, 1000 CNY cost basis
    a_rows = csilk_db_query_param_json(
        pool, "SELECT quantity, cost_basis FROM assets WHERE id=?", (const char*[]){"101", NULL});
    assert(a_rows && csilk_json_array_size(a_rows) == 1);
    assert(db_get_num(csilk_json_array_get(a_rows, 0), "quantity") == 50.0);
    assert(db_get_num(csilk_json_array_get(a_rows, 0), "cost_basis") == 1000.0);
    csilk_json_free(a_rows);

    // Verify open lots: only Lot 2 is open with 50 shares
    lots = tx_lots_get_open(pool, user_id, asset_id_a);
    assert(lots && csilk_json_array_size(lots) == 1);
    l2 = csilk_json_array_get(lots, 0);
    assert(db_get_num(l2, "remaining_quantity") == 50.0);
    assert(db_get_num(l2, "cost_per_unit") == 20.0);
    csilk_json_free(lots);

    // Verify Lot 1 is closed
    csilk_json_t* all_lots = csilk_db_query_param_json(
        pool, "SELECT id, remaining_quantity, status FROM transaction_lots WHERE asset_id=? ORDER BY id ASC", (const char*[]){"101", NULL});
    assert(all_lots && csilk_json_array_size(all_lots) == 2);
    csilk_json_t* al1 = csilk_json_array_get(all_lots, 0);
    assert(db_get_num(al1, "remaining_quantity") == 0.0);
    assert(strcmp(csilk_json_get_string(al1, "status"), "closed") == 0);
    csilk_json_free(all_lots);
    printf("  ✓ Scenario 3 passed.\n");

    /* -------------------------------------------------------------
     * Scenario 4 (RF-5): Buy with fee: fee correctly allocated to unit cost (gross + fee) / qty
     * ------------------------------------------------------------- */
    printf("Scenario 4 (RF-5): Testing Buy with fee allocation on Asset 102...\n");
    money_t b3_amt, b3_fee;
    price_t b3_price;
    quantity_t b3_qty;
    assert(money_from_string("1000.00", CURRENCY_CNY, &b3_amt) == DECIMAL_OK);
    assert(money_from_string("50.00", CURRENCY_CNY, &b3_fee) == DECIMAL_OK);
    assert(price_from_string("10.00", CURRENCY_CNY, &b3_price) == DECIMAL_OK);
    assert(quantity_from_string("100.00000000", &b3_qty) == DECIMAL_OK);

    ledger_tx_t buy3_tx = {
        .id = 0,
        .user_id = user_id,
        .asset_id = asset_id_b,
        .linked_asset_id = funding_asset_id,
        .category_id = 10,
        .type = LEDGER_TX_BUY,
        .type_str = "buy",
        .amount = b3_amt,
        .price = b3_price,
        .quantity = b3_qty,
        .fee = b3_fee,
        .tx_date = "2026-02-01 10:00:00",
        .note = "Buy 100 @ 10 + 50 fee",
        .parent_tx_id = 0,
        .ledger_id = 1
    };
    rc = ledger_apply_tx(pool, &buy3_tx);
    assert(rc == 0);
    assert(buy3_tx.id > 0);

    // Verify Asset 102 total cost basis: 1000 + 50 = 1050 CNY
    a_rows = csilk_db_query_param_json(
        pool, "SELECT quantity, cost_basis FROM assets WHERE id=?", (const char*[]){"102", NULL});
    assert(a_rows && csilk_json_array_size(a_rows) == 1);
    assert(db_get_num(csilk_json_array_get(a_rows, 0), "quantity") == 100.0);
    assert(db_get_num(csilk_json_array_get(a_rows, 0), "cost_basis") == 1050.0);
    csilk_json_free(a_rows);

    // Verify Lot on Asset 102: cost_per_unit = 1050 / 100 = 10.50 CNY, fee_allocated = 50.0
    lots = tx_lots_get_open(pool, user_id, asset_id_b);
    assert(lots && csilk_json_array_size(lots) == 1);
    csilk_json_t* bl = csilk_json_array_get(lots, 0);
    assert(db_get_num(bl, "original_quantity") == 100.0);
    assert(db_get_num(bl, "remaining_quantity") == 100.0);
    assert(db_get_num(bl, "cost_per_unit") == 10.50);
    assert(db_get_num(bl, "fee_allocated") == 50.0);
    csilk_json_free(lots);
    printf("  ✓ Scenario 4 passed.\n");

    /* -------------------------------------------------------------
     * Scenario 5 (RF-3): Rebuild position resets lots and replays FIFO transactions
     * ------------------------------------------------------------- */
    printf("Scenario 5 (RF-3): Testing ledger_rebuild_position for FIFO Asset 101...\n");
    // Zero out Asset 101 and corrupt lots to ensure rebuild resets them completely
    csilk_db_exec(pool, "UPDATE assets SET quantity=0, cost_basis=0, net_value=0, current_value=0 WHERE id=101");
    csilk_db_exec(pool, "UPDATE transaction_lots SET remaining_quantity=999.0 WHERE asset_id=101");

    ledger_position_state_t rebuilt;
    rc = ledger_rebuild_position(pool, user_id, asset_id_a, &rebuilt);
    assert(rc == 0);

    // Check rebuilt state struct: 50 shares, 1000 cost basis, realized_pnl = 750 + 1500 = 2250
    assert(quantity_to_double(rebuilt.quantity) == 50.0);
    assert(money_to_double(rebuilt.cost_basis) == 1000.0);
    assert(money_to_double(rebuilt.realized_pnl) == 2250.0);

    // Check DB assets table
    a_rows = csilk_db_query_param_json(
        pool, "SELECT quantity, cost_basis FROM assets WHERE id=?", (const char*[]){"101", NULL});
    assert(a_rows && csilk_json_array_size(a_rows) == 1);
    assert(db_get_num(csilk_json_array_get(a_rows, 0), "quantity") == 50.0);
    assert(db_get_num(csilk_json_array_get(a_rows, 0), "cost_basis") == 1000.0);
    csilk_json_free(a_rows);

    // Check DB lots after rebuild
    lots = tx_lots_get_open(pool, user_id, asset_id_a);
    assert(lots && csilk_json_array_size(lots) == 1);
    l2 = csilk_json_array_get(lots, 0);
    assert(db_get_num(l2, "remaining_quantity") == 50.0);
    assert(db_get_num(l2, "cost_per_unit") == 20.0);
    csilk_json_free(lots);

    all_lots = csilk_db_query_param_json(
        pool, "SELECT remaining_quantity, status FROM transaction_lots WHERE asset_id=? ORDER BY id ASC", (const char*[]){"101", NULL});
    assert(all_lots && csilk_json_array_size(all_lots) == 2);
    assert(db_get_num(csilk_json_array_get(all_lots, 0), "remaining_quantity") == 0.0);
    assert(strcmp(csilk_json_get_string(csilk_json_array_get(all_lots, 0), "status"), "closed") == 0);
    csilk_json_free(all_lots);
    printf("  ✓ Scenario 5 passed.\n");

    /* -------------------------------------------------------------
     * Scenario 6 (RF-4): Checkpoint snapshot saves open lots to lots_snapshot_json
     * ------------------------------------------------------------- */
    printf("Scenario 6 (RF-4): Testing checkpoint snapshot serialization of FIFO open lots...\n");
    rc = ledger_snapshot_save(pool,
                              user_id,
                              asset_id_a,
                              "2026-01-31",
                              &rebuilt,
                              money_zero(CURRENCY_CNY),
                              "2026-01-15 10:00:00",
                              sell2_tx.id,
                              NULL);
    assert(rc == 0);

    csilk_json_t* snap = NULL;
    rc = ledger_snapshot_get_latest(pool, user_id, asset_id_a, NULL, &snap);
    assert(rc == 0 && snap != NULL);

    const char* lots_json = csilk_json_get_string(snap, "lots_snapshot_json");
    assert(lots_json != NULL && strlen(lots_json) > 2);

    csilk_json_t* parsed_lots = csilk_json_parse(lots_json);
    assert(parsed_lots != NULL && csilk_json_array_size(parsed_lots) == 1);
    csilk_json_t* snap_lot = csilk_json_array_get(parsed_lots, 0);
    assert(db_get_num(snap_lot, "remaining_quantity") == 50.0);
    assert(db_get_num(snap_lot, "cost_per_unit") == 20.0);
    csilk_json_free(parsed_lots);
    csilk_json_free(snap);
    printf("  ✓ Scenario 6 passed.\n");

    /* -------------------------------------------------------------
     * Scenario 7: FIFO sell transaction reversal / usecase delete.
     * Verify ledger_reverse_tx restores open lots correctly,
     * DOES NOT delete the transaction row from transactions, and subsequent
     * tx_delete succeeds.
     * ------------------------------------------------------------- */
    printf("Scenario 7: Testing FIFO sell transaction reversal & tx_delete...\n");
    char s2_id_str[32];
    snprintf(s2_id_str, sizeof(s2_id_str), "%lld", (long long)sell2_tx.id);

    // Verify transaction exists in transactions table before reversal
    csilk_json_t* tx_chk = csilk_db_query_param_json(
        pool, "SELECT id FROM transactions WHERE id=? AND user_id=?",
        (const char*[]){s2_id_str, "1", NULL});
    assert(tx_chk && csilk_json_array_size(tx_chk) == 1);
    csilk_json_free(tx_chk);

    // Reverse sell2_tx
    rc = ledger_reverse_tx(pool, user_id, sell2_tx.id);
    assert(rc == 0);

    // 1. Transaction row MUST STILL EXIST in transactions table
    tx_chk = csilk_db_query_param_json(
        pool, "SELECT id FROM transactions WHERE id=? AND user_id=?",
        (const char*[]){s2_id_str, "1", NULL});
    assert(tx_chk && csilk_json_array_size(tx_chk) == 1);
    csilk_json_free(tx_chk);

    // 2. Asset 101 state restored: 150 shares, 2500 cost basis
    a_rows = csilk_db_query_param_json(
        pool, "SELECT quantity, cost_basis FROM assets WHERE id=?", (const char*[]){"101", NULL});
    assert(a_rows && csilk_json_array_size(a_rows) == 1);
    assert(db_get_num(csilk_json_array_get(a_rows, 0), "quantity") == 150.0);
    assert(db_get_num(csilk_json_array_get(a_rows, 0), "cost_basis") == 2500.0);
    csilk_json_free(a_rows);

    // 3. Open lots restored: Lot 1 (50 shares @ 10.0), Lot 2 (100 shares @ 20.0)
    lots = tx_lots_get_open(pool, user_id, asset_id_a);
    assert(lots && csilk_json_array_size(lots) == 2);
    l1 = csilk_json_array_get(lots, 0);
    l2 = csilk_json_array_get(lots, 1);
    assert(db_get_num(l1, "remaining_quantity") == 50.0);
    assert(db_get_num(l1, "cost_per_unit") == 10.0);
    assert(db_get_num(l2, "remaining_quantity") == 100.0);
    assert(db_get_num(l2, "cost_per_unit") == 20.0);
    csilk_json_free(lots);

    // 4. Subsequent tx_delete succeeds (as performed by tx_usecase_delete)
    int del_ok = tx_delete(pool, user_id, sell2_tx.id);
    assert(del_ok == 1);

    // Verify transaction row is now deleted
    tx_chk = csilk_db_query_param_json(
        pool, "SELECT id FROM transactions WHERE id=? AND user_id=?",
        (const char*[]){s2_id_str, "1", NULL});
    assert(!tx_chk || csilk_json_array_size(tx_chk) == 0);
    if (tx_chk) {
        csilk_json_free(tx_chk);
    }
    printf("  ✓ Scenario 7 passed.\n");

    /* -------------------------------------------------------------
     * Scenario 8: FIFO sell transaction update.
     * Verify ledger_reverse_tx followed by ledger_apply_tx with modified
     * sell quantity succeeds and lot states match new sell quantity.
     * ------------------------------------------------------------- */
    printf("Scenario 8: Testing FIFO sell transaction update...\n");
    // Asset 101 currently has 150 shares (50 in Lot 1 @ 10, 100 in Lot 2 @ 20).
    // Create a new sell transaction sell3_tx: Sell 40 shares @ 30 CNY.
    money_t s3_amt, s3_fee;
    price_t s3_price;
    quantity_t s3_qty;
    assert(money_from_string("1200.00", CURRENCY_CNY, &s3_amt) == DECIMAL_OK);
    assert(money_from_string("0.00", CURRENCY_CNY, &s3_fee) == DECIMAL_OK);
    assert(price_from_string("30.00", CURRENCY_CNY, &s3_price) == DECIMAL_OK);
    assert(quantity_from_string("40.00000000", &s3_qty) == DECIMAL_OK);

    ledger_tx_t sell3_tx = {
        .id = 0,
        .user_id = user_id,
        .asset_id = asset_id_a,
        .linked_asset_id = funding_asset_id,
        .category_id = 10,
        .type = LEDGER_TX_SELL,
        .type_str = "sell",
        .amount = s3_amt,
        .price = s3_price,
        .quantity = s3_qty,
        .fee = s3_fee,
        .tx_date = "2026-02-10 10:00:00",
        .note = "Sell 40 @ 30",
        .parent_tx_id = 0,
        .ledger_id = 1
    };
    rc = ledger_apply_tx(pool, &sell3_tx);
    assert(rc == 0);
    assert(sell3_tx.id > 0);

    // Verify Asset 101 state: 110 shares, 2100 cost basis (10 @ 10 in Lot 1, 100 @ 20 in Lot 2)
    a_rows = csilk_db_query_param_json(
        pool, "SELECT quantity, cost_basis FROM assets WHERE id=?", (const char*[]){"101", NULL});
    assert(a_rows && csilk_json_array_size(a_rows) == 1);
    assert(db_get_num(csilk_json_array_get(a_rows, 0), "quantity") == 110.0);
    assert(db_get_num(csilk_json_array_get(a_rows, 0), "cost_basis") == 2100.0);
    csilk_json_free(a_rows);

    lots = tx_lots_get_open(pool, user_id, asset_id_a);
    assert(lots && csilk_json_array_size(lots) == 2);
    l1 = csilk_json_array_get(lots, 0);
    l2 = csilk_json_array_get(lots, 1);
    assert(db_get_num(l1, "remaining_quantity") == 10.0);
    assert(db_get_num(l2, "remaining_quantity") == 100.0);
    csilk_json_free(lots);

    // Now execute UPDATE flow (as in tx_usecase_update):
    // 1. ledger_reverse_tx on sell3_tx
    rc = ledger_reverse_tx(pool, user_id, sell3_tx.id);
    assert(rc == 0);

    char s3_id_str[32];
    snprintf(s3_id_str, sizeof(s3_id_str), "%lld", (long long)sell3_tx.id);

    // Verify transaction row was preserved (NOT deleted)
    tx_chk = csilk_db_query_param_json(
        pool, "SELECT id FROM transactions WHERE id=? AND user_id=?",
        (const char*[]){s3_id_str, "1", NULL});
    assert(tx_chk && csilk_json_array_size(tx_chk) == 1);
    csilk_json_free(tx_chk);

    // Verify lots were restored to pre-sell3 state: 50 in Lot 1, 100 in Lot 2
    lots = tx_lots_get_open(pool, user_id, asset_id_a);
    assert(lots && csilk_json_array_size(lots) == 2);
    l1 = csilk_json_array_get(lots, 0);
    l2 = csilk_json_array_get(lots, 1);
    assert(db_get_num(l1, "remaining_quantity") == 50.0);
    assert(db_get_num(l2, "remaining_quantity") == 100.0);
    csilk_json_free(lots);

    // 2. Modify sell3_tx to sell 80 shares @ 30 CNY (crosses Lot 1 and consumes 30 from Lot 2)
    assert(quantity_from_string("80.00000000", &sell3_tx.quantity) == DECIMAL_OK);
    assert(money_from_string("2400.00", CURRENCY_CNY, &sell3_tx.amount) == DECIMAL_OK);
    sell3_tx.note = "Sell 80 @ 30 (Updated)";

    // 3. Re-apply transaction via ledger_apply_tx with sell3_tx.id preserved
    rc = ledger_apply_tx(pool, &sell3_tx);
    assert(rc == 0);

    // 4. Verify new lot states:
    // Lot 1 (50 shares) fully depleted (closed, 0 remaining)
    // Lot 2 (100 shares): 30 shares consumed, 70 remaining @ 20.0 CNY
    // Asset 101 totals: 70 shares, 1400.0 CNY cost basis
    a_rows = csilk_db_query_param_json(
        pool, "SELECT quantity, cost_basis FROM assets WHERE id=?", (const char*[]){"101", NULL});
    assert(a_rows && csilk_json_array_size(a_rows) == 1);
    assert(db_get_num(csilk_json_array_get(a_rows, 0), "quantity") == 70.0);
    assert(db_get_num(csilk_json_array_get(a_rows, 0), "cost_basis") == 1400.0);
    csilk_json_free(a_rows);

    lots = tx_lots_get_open(pool, user_id, asset_id_a);
    assert(lots && csilk_json_array_size(lots) == 1);
    l2 = csilk_json_array_get(lots, 0);
    assert(db_get_num(l2, "remaining_quantity") == 70.0);
    assert(db_get_num(l2, "cost_per_unit") == 20.0);
    csilk_json_free(lots);

    all_lots = csilk_db_query_param_json(
        pool, "SELECT remaining_quantity, status FROM transaction_lots WHERE asset_id=? ORDER BY id ASC", (const char*[]){"101", NULL});
    assert(all_lots && csilk_json_array_size(all_lots) == 2);
    assert(db_get_num(csilk_json_array_get(all_lots, 0), "remaining_quantity") == 0.0);
    assert(strcmp(csilk_json_get_string(csilk_json_array_get(all_lots, 0), "status"), "closed") == 0);
    assert(db_get_num(csilk_json_array_get(all_lots, 1), "remaining_quantity") == 70.0);
    assert(strcmp(csilk_json_get_string(csilk_json_array_get(all_lots, 1), "status"), "open") == 0);
    csilk_json_free(all_lots);
    printf("  ✓ Scenario 8 passed.\n");

    /* -------------------------------------------------------------
     * Extra Test: Reversal of Buy deletes its lot
     * ------------------------------------------------------------- */
    printf("Extra: Testing Buy reversal deletes lot on Asset 102...\n");
    rc = ledger_reverse_tx(pool, user_id, buy3_tx.id);
    assert(rc == 0);
    lots = tx_lots_get_open(pool, user_id, asset_id_b);
    assert(!lots || csilk_json_array_size(lots) == 0);
    if (lots) {
        csilk_json_free(lots);
    }
    printf("  ✓ Buy reversal verified.\n");

    /* -------------------------------------------------------------
     * Scenario 9: Asset usecase create with cost_method = 'fifo'
     * ------------------------------------------------------------- */
    printf("Scenario 9: Create asset via usecase with cost_method = 'fifo'...\n");
    create_asset_cmd_t create_cmd = {
        .user_id = user_id,
        .ledger_id = 1,
        .category_id = 10,
        .account_id = 1,
        .name = "FIFO Stock C",
        .currency = "CNY",
        .cost_method = "fifo",
    };
    asset_usecase_result_t create_res = {0};
    rc = asset_usecase_create(pool, &create_cmd, &create_res);
    assert(rc == 0);
    assert(create_res.code == 0);
    int64_t asset_id_c = create_res.id;
    assert(asset_id_c > 0);

    // Verify in DB that cost_method is 'fifo'
    char c_id_str[32];
    snprintf(c_id_str, sizeof(c_id_str), "%lld", (long long)asset_id_c);
    csilk_json_t* c_row = csilk_db_query_param_json(
        pool, "SELECT cost_method FROM assets WHERE id=?", (const char*[]){c_id_str, NULL});
    assert(c_row && csilk_json_array_size(c_row) == 1);
    assert(strcmp(csilk_json_get_string(csilk_json_array_get(c_row, 0), "cost_method"), "fifo") == 0);
    csilk_json_free(c_row);
    printf("  ✓ Scenario 9 passed.\n");

    /* -------------------------------------------------------------
     * Scenario 10: Update asset cost_method to 'weighted_average' without transactions succeeds
     * ------------------------------------------------------------- */
    printf("Scenario 10: Update asset cost_method when no transactions exist...\n");
    update_asset_cmd_t update_cmd = {
        .user_id = user_id,
        .id = asset_id_c,
        .cost_method = "weighted_average",
    };
    asset_usecase_result_t update_res = {0};
    rc = asset_usecase_update(pool, &update_cmd, &update_res);
    assert(rc == 0);
    assert(update_res.code == 0);

    // Verify in DB that cost_method was changed to 'weighted_average'
    c_row = csilk_db_query_param_json(
        pool, "SELECT cost_method FROM assets WHERE id=?", (const char*[]){c_id_str, NULL});
    assert(c_row && csilk_json_array_size(c_row) == 1);
    assert(strcmp(csilk_json_get_string(csilk_json_array_get(c_row, 0), "cost_method"), "weighted_average") == 0);
    csilk_json_free(c_row);
    printf("  ✓ Scenario 10 passed.\n");

    /* -------------------------------------------------------------
     * Scenario 11: Update asset cost_method when transactions exist fails with code 1004 (RF-2)
     * ------------------------------------------------------------- */
    printf("Scenario 11: Update asset cost_method when transactions exist fails with 1004...\n");
    // Add a buy transaction on asset_id_c
    quantity_t c_qty;
    price_t    c_price;
    money_t    c_amt, c_fee;
    assert(quantity_from_string("10.00000000", &c_qty) == DECIMAL_OK);
    assert(price_from_string("100.0000", CURRENCY_CNY, &c_price) == DECIMAL_OK);
    assert(money_from_string("1000.00", CURRENCY_CNY, &c_amt) == DECIMAL_OK);
    assert(money_from_string("5.00", CURRENCY_CNY, &c_fee) == DECIMAL_OK);

    ledger_tx_t c_tx = {
        .id = 0,
        .user_id = user_id,
        .asset_id = asset_id_c,
        .linked_asset_id = funding_asset_id,
        .category_id = 10,
        .type = LEDGER_TX_BUY,
        .type_str = "buy",
        .amount = c_amt,
        .price = c_price,
        .quantity = c_qty,
        .fee = c_fee,
        .tx_date = "2026-03-01 10:00:00",
        .note = "Buy C 10 @ 100",
        .parent_tx_id = 0,
        .ledger_id = 1
    };
    rc = ledger_apply_tx(pool, &c_tx);
    assert(rc == 0);
    assert(c_tx.id > 0);

    // Attempt to change cost_method from 'weighted_average' to 'fifo'
    update_asset_cmd_t update_cmd_blocked = {
        .user_id = user_id,
        .id = asset_id_c,
        .cost_method = "fifo",
    };
    asset_usecase_result_t update_res_blocked = {0};
    rc = asset_usecase_update(pool, &update_cmd_blocked, &update_res_blocked);
    assert(rc == -1);
    assert(update_res_blocked.code == 1004);
    assert(strcmp(update_res_blocked.message, "资产已存在交易记录，禁止变更成本结转方法") == 0);

    // Verify in DB that cost_method remains 'weighted_average'
    c_row = csilk_db_query_param_json(
        pool, "SELECT cost_method FROM assets WHERE id=?", (const char*[]){c_id_str, NULL});
    assert(c_row && csilk_json_array_size(c_row) == 1);
    assert(strcmp(csilk_json_get_string(csilk_json_array_get(c_row, 0), "cost_method"), "weighted_average") == 0);
    csilk_json_free(c_row);

    // Also test: updating with same cost_method ("weighted_average") is not blocked even with transactions
    update_asset_cmd_t update_cmd_same = {
        .user_id = user_id,
        .id = asset_id_c,
        .cost_method = "weighted_average",
    };
    asset_usecase_result_t update_res_same = {0};
    rc = asset_usecase_update(pool, &update_cmd_same, &update_res_same);
    assert(rc == 0);
    assert(update_res_same.code == 0);

    // Also test: invalid cost_method rejected with 1002
    update_asset_cmd_t update_cmd_invalid = {
        .user_id = user_id,
        .id = asset_id_c,
        .cost_method = "invalid_foo",
    };
    asset_usecase_result_t update_res_invalid = {0};
    rc = asset_usecase_update(pool, &update_cmd_invalid, &update_res_invalid);
    assert(rc == -1);
    assert(update_res_invalid.code == 1002);

    printf("  ✓ Scenario 11 passed (RF-2 immutability enforced).\n");

    printf("\n>>> All 11 FIFO Cost Basis Accounting Scenarios PASSED! <<<\n");
    return 0;
}
