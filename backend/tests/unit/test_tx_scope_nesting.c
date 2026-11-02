#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common/db.h"
#include "common/balance.h"
#include "core/financial/money.h"

int main(void) {
    printf("Running test_tx_scope_nesting...\n");
    unlink("/tmp/test_tx_scope_nesting.db");
    setenv("MINEFOLIO_DB_DSN", "/tmp/test_tx_scope_nesting.db", 1);
    setenv("DB_DRIVER", "sqlite", 1);

    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);
    assert(db_run_migrations(pool) == 0);

    /* Seed test user, category, and asset */
    csilk_db_exec(pool, "INSERT INTO users (id, username, password) VALUES (1, 'testuser', 'pass');");
    csilk_db_exec(pool, "INSERT INTO categories (id, user_id, name, type, asset_type) VALUES (1, 1, 'Cash', 'asset', 'cash');");
    csilk_db_exec(pool, "INSERT INTO assets (id, user_id, category_id, name, current_value, currency) VALUES (1, 1, 1, 'Wallet', 100.0, 'CNY');");

    /* 1. Begin outer transaction */
    db_tx_scope_t outer_scope;
    assert(db_tx_scope_begin(pool, "test_outer", &outer_scope) == 0);

    /* 2. Inner failure and rollback: balance_apply_delta_m on nonexistent asset fails and rolls back its inner savepoint */
    money_t delta_fail;
    assert(money_from_double(50.00, CURRENCY_CNY, &delta_fail) == DECIMAL_OK);
    int rc_fail = balance_apply_delta_m(pool, 99999, 1, delta_fail, "test_fail", 1000, "Should fail");
    assert(rc_fail == -1);

    /* 3. Invariant: Outer transaction remains completely valid and recoverable after inner savepoint rollback */
    money_t delta_ok;
    assert(money_from_double(100.50, CURRENCY_CNY, &delta_ok) == DECIMAL_OK);
    int rc_ok = balance_apply_delta_m(pool, 1, 1, delta_ok, "test_deposit", 1001, "Valid deposit");
    assert(rc_ok == 0);

    /* 4. Commit outer transaction cleanly */
    assert(db_tx_scope_commit(pool, &outer_scope) == 0);

    /* 5. Verify database balance was updated: 100.0 + 100.50 = 200.50 */
    csilk_json_t* res = csilk_db_query_json(pool, "SELECT current_value FROM assets WHERE id = 1");
    assert(res && csilk_json_array_size(res) > 0);
    double cur_val = db_get_num(csilk_json_array_get(res, 0), "current_value");
    assert(cur_val > 200.49 && cur_val < 200.51);
    csilk_json_free(res);

    /* 6. Verify audit logs exist in asset_balance_logs */
    csilk_json_t* logs = csilk_db_query_json(pool, "SELECT id FROM asset_balance_logs WHERE asset_id = 1");
    assert(logs && csilk_json_array_size(logs) == 1);
    csilk_json_free(logs);

    unlink("/tmp/test_tx_scope_nesting.db");
    printf("PASS: test_tx_scope_nesting completed successfully\n");
    return 0;
}
