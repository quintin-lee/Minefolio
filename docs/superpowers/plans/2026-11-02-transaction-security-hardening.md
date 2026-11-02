# Transaction Scope & Security Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate transaction nesting collisions and SQLite savepoint conflicts across application use cases, make asset balance mutations self-contained and atomic, strictly enforce 128-bit decimal fixed-point types instead of bare double arithmetic, align unauthenticated `/mcp` requests with JSON-RPC 2.0 specifications without middleware HTTP 401 overrides, persist AI confirmation nonces to survive process restarts, and achieve POSIX compliance in temporary file generation.

**Architecture:** Replace raw `BEGIN TRANSACTION/COMMIT/ROLLBACK` with `db_tx_scope_t` across all application use cases and wrap `balance_apply_delta_m()` in an autonomous transaction scope; adopt `price_times_quantity()` and `money_sub()` in asset and market use cases; refactor `jwt_middleware.c` and rate limiters to pass unauthenticated requests on `/mcp` through to `mcp_endpoint_handler` using non-intrusive `jwt_get_user_id()`; deploy database migration `V019__ai_consumed_nonces.sql` for persistent nonce verification and replay prevention; replace non-standard `mkstemp` with `mkstemps`.

**Tech Stack:** C23, POSIX Threads (`pthreads`), `mkstemps`, csilk framework (`csilk_db_pool_t`, `csilk_json_t`), SQLite / PostgreSQL, CMake / CTest.

**Spec:** [`docs/superpowers/specs/2026-11-02-transaction-security-hardening-design.md`](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-11-02-transaction-security-hardening-design.md)

---

## Working Directory & Command Execution Convention

All build, test, and shell commands in this plan are executed from the backend directory:
`/data/home/quintin/workspace/source/c/Minefolio/backend`

---

## File Structure & Decomposition

```
backend/
├── sql/
│   ├── migration.sql                                      # Modify: append ai_consumed_nonces schema
│   ├── migration_postgres.sql                             # Modify: append ai_consumed_nonces schema
│   └── migrations/
│       ├── sqlite/V019__ai_consumed_nonces.sql            # Create: Flyway migration for SQLite
│       └── postgres/V019__ai_consumed_nonces.sql          # Create: Flyway migration for PostgreSQL
├── src/
│   ├── common/
│   │   └── balance.c                                      # Modify: wrap balance_apply_delta_m in db_tx_scope_t
│   ├── application/
│   │   ├── transfer/usecases.c                            # Modify: replace raw SQL tx with db_tx_scope_t
│   │   ├── transaction/usecases.c                         # Modify: replace raw SQL tx with db_tx_scope_t
│   │   ├── daily_expense/usecases.c                       # Modify: reorder asset check and use db_tx_scope_t
│   │   ├── dca/usecases.c                                 # Modify: replace raw SQL tx with db_tx_scope_t
│   │   ├── asset/usecases.c                               # Modify: replace float math with price_times_quantity
│   │   └── market/usecases.c                              # Modify: eliminate double conversion roundtrip
│   ├── middlewares/
│   │   ├── jwt_middleware.c                               # Modify: add jwt_validate_version_num & mcp passthrough
│   │   └── rate_limit.c                                   # Modify: use jwt_get_user_id in mcp_rate_limit_middleware
│   ├── interfaces/http/controllers/
│   │   ├── admin_controller.c                             # Modify: replace setup tx with db_tx_scope_t
│   │   └── mcp_server_controller.c                        # Modify: use jwt_get_user_id to emit JSON-RPC -32001
│   ├── infrastructure/repositories/
│   │   └── mcp_server_repo_impl.c                         # Modify: replace tools refresh tx with db_tx_scope_t
│   └── services/
│       ├── ai/policy/
│       │   ├── confirmation.h                             # Modify: export ai_confirmation_reset_nonce_cache
│       │   └── confirmation.c                             # Modify: dual-layer DB + ring nonce verification
│       ├── file_parser.h                                  # Modify: export file_parser_write_temp
│       └── file_parser.c                                  # Modify: use mkstemps and unlink on failure
├── tests/
│   ├── unit/
│   │   ├── test_tx_scope_nesting.c                        # Create: unit test for nested savepoints & rollback
│   │   ├── test_financial_precision_edge.c                # Create: unit test for exact 128-bit decimal arithmetic
│   │   ├── test_nonce_persistence.c                       # Create: unit test for DB-backed replay prevention
│   │   └── test_file_parser_mkstemps.c                    # Create: unit test for mkstemps creation & cleanup
│   └── test_mcp_server.sh                                 # Modify: strict assertion for "code":-32001
└── CMakeLists.txt                                         # Modify: register new test targets using ${LIB_SOURCES}
```

---

## Chunk 1: Unified Transaction Scope & Self-Contained Atomic Balance Delta

### Task 1: Self-Contained Atomic Balance Application (`src/common/balance.c`)

**Files:**
- Modify: `src/common/balance.c:30-143`
- Test: `tests/unit/test_tx_scope_nesting.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write unit test for transaction scope nesting (`test_tx_scope_nesting.c`)**

Create `backend/tests/unit/test_tx_scope_nesting.c`:
```c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/db.h"
#include "common/balance.h"
#include "core/financial/money.h"

int main(void) {
    printf("Running test_tx_scope_nesting...\n");
    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);
    assert(db_run_migrations(pool) == 0);

    /* 1. Begin outer transaction */
    db_tx_scope_t outer_scope;
    assert(db_tx_scope_begin(pool, "test_outer", &outer_scope) == 0);

    /* 2. Execute inner balance_apply_delta_m (which opens its own SAVEPOINT) */
    money_t delta = money_from_double_auto(100.50, CURRENCY_CNY);
    /* Asset ID 1 was seeded in migrations */
    int rc = balance_apply_delta_m(pool, 1, 1, delta, "test_deposit", 1001, "Test nested scope");
    assert(rc == 0);

    /* 3. Commit outer transaction cleanly */
    assert(db_tx_scope_commit(pool, &outer_scope) == 0);

    /* 4. Test rollback isolation: outer rollback should undo inner operations */
    assert(db_tx_scope_begin(pool, "test_rollback_outer", &outer_scope) == 0);
    money_t delta2 = money_from_double_auto(50.00, CURRENCY_CNY);
    assert(balance_apply_delta_m(pool, 1, 1, delta2, "test_deposit", 1002, "To be rolled back") == 0);
    assert(db_tx_scope_rollback(pool, &outer_scope) == 0);

    printf("PASS: test_tx_scope_nesting completed successfully\n");
    return 0;
}
```

- [ ] **Step 2: Register `test_tx_scope_nesting` in `CMakeLists.txt`**

Add to `backend/CMakeLists.txt`:
```cmake
add_executable(test_tx_scope_nesting tests/unit/test_tx_scope_nesting.c ${LIB_SOURCES})
target_include_directories(test_tx_scope_nesting PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_tx_scope_nesting PRIVATE -UNDEBUG)
target_link_libraries(test_tx_scope_nesting PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
add_test(NAME test_tx_scope_nesting COMMAND test_tx_scope_nesting)
```

- [ ] **Step 3: Implement internal `db_tx_scope_t` in `balance_apply_delta_m()`**

In `src/common/balance.c`:
1. Call `db_tx_scope_begin(pool, "mf_balance_delta", &scope)` at function entry.
2. If `!row` or `csilk_json_array_size(row) == 0`, call `db_tx_scope_rollback(pool, &scope)` and free `row`.
3. If `!res3` (UPDATE failure), call `db_tx_scope_rollback(pool, &scope)` and return `-2`.
4. If `!after` (balance_after query failure), call `db_tx_scope_rollback(pool, &scope)` and return `-2`.
5. If `!res5` (audit log insertion failure into `asset_balance_logs`), call `db_tx_scope_rollback(pool, &scope)` and return `-2`.
6. If `db_tx_scope_commit(pool, &scope) != 0`, call `db_tx_scope_rollback(pool, &scope)` and return `-2`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -B build -S . && cmake --build build --target test_tx_scope_nesting && ./build/test_tx_scope_nesting`
Expected: PASS

- [ ] **Step 5: Commit Task 1**

```bash
git add src/common/balance.c tests/unit/test_tx_scope_nesting.c CMakeLists.txt
git commit -m "fix(balance): 🐛 ensure balance_apply_delta_m is atomic with internal tx scope"
```

---

### Task 2: Application Use Cases Transaction Scope Migration

**Files:**
- Modify: `src/application/transfer/usecases.c`
- Modify: `src/application/transaction/usecases.c`
- Modify: `src/application/daily_expense/usecases.c`
- Modify: `src/application/dca/usecases.c`
- Modify: `src/interfaces/http/controllers/admin_controller.c`
- Modify: `src/infrastructure/repositories/mcp_server_repo_impl.c`

- [ ] **Step 1: Replace raw transactions in `transfer/usecases.c`**

In `src/application/transfer/usecases.c`:
1. Declare `db_tx_scope_t scope;`.
2. Replace `csilk_db_exec(pool, "BEGIN TRANSACTION")` (line 49) with:
   `if (db_tx_scope_begin(pool, "mf_transfer_create", &scope) != 0) { ... }`
3. Replace all 4 rollback sites (lines 69, 87, 110, 124) with `db_tx_scope_rollback(pool, &scope);`.
4. Replace `csilk_db_exec(pool, "COMMIT")` (line 131) with:
   `if (db_tx_scope_commit(pool, &scope) != 0) { db_tx_scope_rollback(pool, &scope); return -1; }`

- [ ] **Step 2: Replace raw transactions in `transaction/usecases.c`**

In `src/application/transaction/usecases.c`:
1. In `tx_usecase_create` (lines 108, 139): replace with `db_tx_scope_begin(pool, "mf_tx_create", &scope)` and `commit`; rollback at line 133 with `db_tx_scope_rollback`.
2. In `tx_usecase_update` (lines 233, 285): replace with `db_tx_scope_begin(pool, "mf_tx_update", &scope)` and `commit`; rollbacks at lines 242, 278 with `db_tx_scope_rollback`.
3. In `tx_usecase_delete` (lines 306, 507): replace with `db_tx_scope_begin(pool, "mf_tx_delete", &scope)` and `commit`; rollbacks at lines 326, 361, 406, 439, 476, 491, 499 with `db_tx_scope_rollback`.

- [ ] **Step 3: Clean up asset check and replace transactions in `daily_expense/usecases.c`**

In `src/application/daily_expense/usecases.c`:
1. In `check_asset_exists` (line 36): delete `csilk_db_exec(pool, "ROLLBACK");`.
2. In `daily_expense_usecase_create` (line 375): move `check_asset_exists` call to before `db_tx_scope_begin`. Replace `BEGIN TRANSACTION` and `COMMIT` with `db_tx_scope_begin(pool, "mf_expense_create", &scope)` and `commit`; rollbacks at lines 396, 411, 418.
3. In `daily_expense_usecase_update` (line 467): move `check_asset_exists` call to before `db_tx_scope_begin`. Replace `BEGIN TRANSACTION` and `COMMIT` with `db_tx_scope_begin(pool, "mf_expense_update", &scope)` and `commit`; rollbacks at lines 486, 505, 520, 529.
4. In `daily_expense_usecase_delete` (line 555): replace with `db_tx_scope_begin(pool, "mf_expense_delete", &scope)` and `commit`; rollbacks at lines 573, 581.

- [ ] **Step 4: Replace raw transactions in `dca/usecases.c`, `admin_controller.c`, `mcp_server_repo_impl.c`**

1. In `src/application/dca/usecases.c`: replace lines 249, 283 with `db_tx_scope_begin(pool, "mf_dca_scope", &scope)` and `commit`. If begin fails, `csilk_json_free(exec_arr)`. Rollback at line 270 with `db_tx_scope_rollback`.
2. In `src/interfaces/http/controllers/admin_controller.c`: replace lines 115, 161 with `db_tx_scope_begin(pool, "mf_setup_scope", &scope)` and `commit`. Rollback at line 135.
3. In `src/infrastructure/repositories/mcp_server_repo_impl.c`: replace lines 367, 409 with `db_tx_scope_begin(pool, "mf_mcp_tools_refresh", &scope)` and `commit`. Rollbacks at lines 373, 405.

- [ ] **Step 5: Run existing integration tests to verify no transaction regression**

Run: `bash tests/test_link.sh`
Expected: PASS

- [ ] **Step 6: Commit Task 2**

```bash
git add src/application/transfer/usecases.c src/application/transaction/usecases.c src/application/daily_expense/usecases.c src/application/dca/usecases.c src/interfaces/http/controllers/admin_controller.c src/infrastructure/repositories/mcp_server_repo_impl.c
git commit -m "refactor(usecases): ♻️ migrate application transactions to db_tx_scope_t savepoints"
```

---

## Chunk 2: Domain Strong-Typing & Elimination of Bare Doubles

### Task 3: 128-bit Decimal Fixed-Point Arithmetic in Asset & Market Usecases

**Files:**
- Modify: `src/application/asset/usecases.c:220-275`
- Modify: `src/application/market/usecases.c:150-175, 310-335`
- Test: `tests/unit/test_financial_precision_edge.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write unit test for financial precision edge cases (`test_financial_precision_edge.c`)**

Create `backend/tests/unit/test_financial_precision_edge.c`:
```c
#include <assert.h>
#include <stdio.h>
#include "core/financial/price.h"
#include "core/financial/quantity.h"
#include "core/financial/money.h"

int main(void) {
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

    /* 3. Test exact delta subtraction */
    money_t old_val;
    assert(money_from_double(19900.00, CURRENCY_CNY, &old_val) == DECIMAL_OK);

    money_t delta;
    assert(money_sub(total_val, old_val, &delta) == DECIMAL_OK);

    char delta_str[64];
    money_to_string(delta, delta_str, sizeof(delta_str));
    assert(strcmp(delta_str, "90.00") == 0);

    printf("PASS: test_financial_precision_edge completed successfully\n");
    return 0;
}
```

- [ ] **Step 2: Register `test_financial_precision_edge` in `CMakeLists.txt`**

Add to `backend/CMakeLists.txt`:
```cmake
add_executable(test_financial_precision_edge tests/unit/test_financial_precision_edge.c ${LIB_SOURCES})
target_include_directories(test_financial_precision_edge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_financial_precision_edge PRIVATE -UNDEBUG)
target_link_libraries(test_financial_precision_edge PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
add_test(NAME test_financial_precision_edge COMMAND test_financial_precision_edge)
```

- [ ] **Step 3: Refactor `asset/usecases.c` with strong-typed calculation**

In `src/application/asset/usecases.c`:
1. Include `#include "core/financial/price.h"` and `#include "core/financial/quantity.h"`.
2. Compute `new_val_m` via `price_times_quantity(net_obj, qty_obj, &new_val_m)` and `delta_m` via `money_sub(new_val_m, old_val_m, &delta_m)` before starting `db_tx_scope_begin`.
3. Pass `delta_m` directly to `balance_apply_delta_m`.
4. Update `asset_update_basic` with `money_to_double(new_val_m)` and full 10 arguments.
5. Check `if (need_reattach && asset_update_account(db_pool, cmd->user_id, cmd->id, reattach_acct))`.

- [ ] **Step 4: Refactor batch and single asset quote sync in `market/usecases.c`**

In `src/application/market/usecases.c`:
1. In `market_service_sync_quotes` (lines 157–170): eliminate `money_to_double(delta)` and directly invoke `balance_apply_delta_m(db_pool, aid, uid, delta_m, ...)`.
2. In `mf_market_sync_single_asset` (lines 313–328): eliminate `money_to_double(delta)` and directly invoke `balance_apply_delta_m(db_pool, asset_id, user_id, delta_m, ...)`. On failure, rollback `scope` and return `(b_rc == -1) ? 1002 : 1004`.

- [ ] **Step 5: Run tests to verify pass**

Run: `cmake -B build -S . && cmake --build build --target test_financial_precision_edge && ./build/test_financial_precision_edge`
Expected: PASS

- [ ] **Step 6: Commit Task 3**

```bash
git add src/application/asset/usecases.c src/application/market/usecases.c tests/unit/test_financial_precision_edge.c CMakeLists.txt
git commit -m "refactor(financial): ⚡ enforce 128-bit decimal arithmetic in asset and market use cases"
```

---

## Chunk 3: MCP Server Optional Auth & JSON-RPC Protocol Alignment

### Task 4: Clean MCP Unauthenticated Pass-Through & Error Handling

**Files:**
- Modify: `src/middlewares/jwt_middleware.c:45-90, 160-220`
- Modify: `src/middlewares/rate_limit.c:220-230`
- Modify: `src/interfaces/http/controllers/mcp_server_controller.c:175-185`
- Modify: `tests/test_mcp_server.sh:136`

- [ ] **Step 1: Refactor `jwt_validate_version_num` in `jwt_middleware.c`**

In `src/middlewares/jwt_middleware.c`:
1. Implement `static int jwt_validate_version_num(int64_t user_id, int jwt_version)` extracting version from cache / DB without touching `csilk_ctx_t`.
2. In `jwt_middleware_wrapper(csilk_ctx_t* c)`:
   - If missing `Authorization` header on `/mcp`, call `csilk_next(c); return;`.
   - If invalid/expired token on `/mcp`, free payload, call `csilk_next(c); return;`.
   - If user claims invalid or `jwt_validate_version_num` fails on `/mcp`, free payload, call `csilk_next(c); return;`.
   - Only attach `jwt_payload` to `c` if all verifications succeed.

- [ ] **Step 2: Non-intrusive `user_id` retrieval in rate limiter and MCP handler**

1. In `src/middlewares/rate_limit.c:220` (`mcp_rate_limit_middleware`):
   Replace `int64_t user_id = ctx_user_id(c);` with `int64_t user_id = jwt_get_user_id(c);`.
   If `user_id <= 0`, call `csilk_next(c); return;`.
2. In `src/interfaces/http/controllers/mcp_server_controller.c:176`:
   Replace `int64_t user_id = ctx_user_id(c);` with `int64_t user_id = jwt_get_user_id(c);`.
   If `user_id < 0`, emit standard JSON-RPC 2.0 `-32001 (unauthorized)` with HTTP 200.

- [ ] **Step 3: Update `tests/test_mcp_server.sh` with strict assertion**

In `tests/test_mcp_server.sh:136`:
Change regex assertion to strictly require `'"code":-32001'`.

- [ ] **Step 4: Run MCP integration test to verify clean pass-through**

Run: `bash tests/test_mcp_server.sh`
Expected: PASS with all assertions green

- [ ] **Step 5: Commit Task 4**

```bash
git add src/middlewares/jwt_middleware.c src/middlewares/rate_limit.c src/interfaces/http/controllers/mcp_server_controller.c tests/test_mcp_server.sh
git commit -m "fix(mcp): 🐛 align unauthenticated MCP requests with JSON-RPC -32001 error protocol"
```

---

## Chunk 4: Persistent Confirmation Nonce Defense & POSIX `mkstemps` Compliance

### Task 5: Database Migration & Dual-Layer Nonce Replay Prevention

**Files:**
- Create: `sql/migrations/sqlite/V019__ai_consumed_nonces.sql`
- Create: `sql/migrations/postgres/V019__ai_consumed_nonces.sql`
- Modify: `sql/migration.sql`
- Modify: `sql/migration_postgres.sql`
- Modify: `src/services/ai/policy/confirmation.h`
- Modify: `src/services/ai/policy/confirmation.c`
- Test: `tests/unit/test_nonce_persistence.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create V019 migrations for SQLite and PostgreSQL**

Create `backend/sql/migrations/sqlite/V019__ai_consumed_nonces.sql` and `backend/sql/migrations/postgres/V019__ai_consumed_nonces.sql`:
```sql
CREATE TABLE IF NOT EXISTS ai_consumed_nonces (
    nonce       VARCHAR(64) PRIMARY KEY,
    expiration  BIGINT NOT NULL,
    user_id     BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    consumed_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_ai_consumed_nonces_exp ON ai_consumed_nonces(expiration);
```
Append identical statements to `sql/migration.sql` and `sql/migration_postgres.sql`.

- [ ] **Step 2: Write unit test for persistent nonce verification (`test_nonce_persistence.c`)**

Create `backend/tests/unit/test_nonce_persistence.c`:
```c
#include <assert.h>
#include <stdio.h>
#include "common/db.h"
#include "services/ai/policy/confirmation.h"

int main(void) {
    printf("Running test_nonce_persistence...\n");
    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);
    assert(db_run_migrations(pool) == 0);

    /* 1. Create a bound token */
    csilk_json_t* args = csilk_json_object();
    csilk_json_add_string(args, "target", "test_asset");
    char* token = ai_confirmation_create_bound_token(1, 1, "test_action", args, AI_RISK_HIGH, 60);
    assert(token != NULL);

    /* 2. First verification: must succeed */
    assert(ai_confirmation_verify_and_consume(token, 1, "test_action", args) == 0);

    /* 3. Reset in-memory ring cache to simulate daemon restart */
    ai_confirmation_reset_nonce_cache();

    /* 4. Second verification: must fail with AI_CONFIRM_ERR_REPLAY via database check */
    assert(ai_confirmation_verify_and_consume(token, 1, "test_action", args) == AI_CONFIRM_ERR_REPLAY);

    csilk_json_free(args);
    free(token);
    printf("PASS: test_nonce_persistence completed successfully\n");
    return 0;
}
```

- [ ] **Step 3: Register `test_nonce_persistence` in `CMakeLists.txt`**

Add to `backend/CMakeLists.txt`:
```cmake
add_executable(test_nonce_persistence tests/unit/test_nonce_persistence.c ${LIB_SOURCES})
target_include_directories(test_nonce_persistence PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_nonce_persistence PRIVATE -UNDEBUG)
target_link_libraries(test_nonce_persistence PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
add_test(NAME test_nonce_persistence COMMAND test_nonce_persistence)
```

- [ ] **Step 4: Implement persistent nonce storage & verification in `confirmation.c`**

In `src/services/ai/policy/confirmation.c`:
1. Include `#include "common/db.h"` and `#include <stdatomic.h>`.
2. Implement `ai_confirmation_reset_nonce_cache()`.
3. In `ai_confirmation_verify_and_consume()`:
   - Check in-memory ring buffer.
   - If `db_get_pool()` is non-null, query `SELECT 1 FROM ai_consumed_nonces`.
   - Insert into `ai_consumed_nonces` and map primary key collisions to `AI_CONFIRM_ERR_REPLAY`.
   - Update in-memory ring buffer.

- [ ] **Step 5: Run test to verify pass**

Run: `cmake -B build -S . && cmake --build build --target test_nonce_persistence && ./build/test_nonce_persistence`
Expected: PASS

- [ ] **Step 6: Commit Task 5**

```bash
git add sql/migrations/sqlite/V019__ai_consumed_nonces.sql sql/migrations/postgres/V019__ai_consumed_nonces.sql sql/migration.sql sql/migration_postgres.sql src/services/ai/policy/confirmation.h src/services/ai/policy/confirmation.c tests/unit/test_nonce_persistence.c CMakeLists.txt
git commit -m "feat(ai): 🛡️ persist consumed confirmation nonces to prevent token replay attacks"
```

---

### Task 6: POSIX `mkstemps` File Creation Compliance

**Files:**
- Modify: `src/services/file_parser.h`
- Modify: `src/services/file_parser.c`
- Test: `tests/unit/test_file_parser_mkstemps.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write unit test for `file_parser_write_temp` (`test_file_parser_mkstemps.c`)**

Create `backend/tests/unit/test_file_parser_mkstemps.c`:
```c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "services/file_parser.h"

int main(void) {
    printf("Running test_file_parser_mkstemps...\n");

    const char* sample_data = "%PDF-1.4 sample content";
    char out_path[256];

    /* 1. Test .pdf suffix */
    int rc = file_parser_write_temp(sample_data, strlen(sample_data), ".pdf", out_path, sizeof(out_path));
    assert(rc == 0);
    assert(strstr(out_path, ".pdf") != NULL);
    assert(access(out_path, R_OK) == 0);
    unlink(out_path);

    /* 2. Test .zip suffix */
    rc = file_parser_write_temp("PK\x03\x04", 4, ".zip", out_path, sizeof(out_path));
    assert(rc == 0);
    assert(strstr(out_path, ".zip") != NULL);
    assert(access(out_path, R_OK) == 0);
    unlink(out_path);

    printf("PASS: test_file_parser_mkstemps completed successfully\n");
    return 0;
}
```

- [ ] **Step 2: Register `test_file_parser_mkstemps` in `CMakeLists.txt`**

Add to `backend/CMakeLists.txt`:
```cmake
add_executable(test_file_parser_mkstemps tests/unit/test_file_parser_mkstemps.c ${LIB_SOURCES})
target_include_directories(test_file_parser_mkstemps PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(test_file_parser_mkstemps PRIVATE _GNU_SOURCE)
target_compile_options(test_file_parser_mkstemps PRIVATE -UNDEBUG)
target_link_libraries(test_file_parser_mkstemps PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
add_test(NAME test_file_parser_mkstemps COMMAND test_file_parser_mkstemps)
```

- [ ] **Step 3: Implement `file_parser_write_temp` with `mkstemps` in `file_parser.c`**

In `src/services/file_parser.h`:
Export `int file_parser_write_temp(const char* data, size_t len, const char* suffix, char* out_path, size_t out_path_len);`.

In `src/services/file_parser.c`:
1. Put `#ifndef _GNU_SOURCE \n #define _GNU_SOURCE \n #endif` at the very top.
2. Implement `file_parser_write_temp()` using `mkstemps(out_path, (int)suffix_len)`. If write fails, call `unlink(out_path)`.
3. Update `parse_pdf` and `parse_zip` to call `file_parser_write_temp`.

- [ ] **Step 4: Run test to verify pass**

Run: `cmake -B build -S . && cmake --build build --target test_file_parser_mkstemps && ./build/test_file_parser_mkstemps`
Expected: PASS

- [ ] **Step 5: Commit Task 6**

```bash
git add src/services/file_parser.h src/services/file_parser.c tests/unit/test_file_parser_mkstemps.c CMakeLists.txt
git commit -m "fix(services): 🐛 adopt POSIX mkstemps for temporary upload file parsing"
```

---

## Final Verification Gate

- [ ] Run full test suite:
  ```bash
  cmake --build build --target all && ctest --test-dir build --output-on-failure
  bash tests/test_link.sh
  bash tests/test_mcp.sh
  bash tests/test_mcp_server.sh
  bash tests/test_ai_loop_hardening.sh
  ```
- [ ] Expected: 100% test pass across all 44+ unit tests and integration suites with zero memory leaks.
