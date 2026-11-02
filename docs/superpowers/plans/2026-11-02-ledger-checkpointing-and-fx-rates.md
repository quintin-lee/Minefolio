# 账本增量快照检查点与历史时点汇率（Milestone 1）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 Minefolio 账本引擎建立增量检查点快照机制（`ledger_snapshots`）与历史时点汇率切片（`transactions.fx_rate_to_cny`），将历史重放耗时由 $O(N)$ 降低至常数级增量回放 $O(\Delta)$，并消除历史多币种估值在长期趋势中的汇率漂移。

**Architecture:** 在数据库层通过 Flyway 模式部署 V020 迁移（快照表与交易生效汇率），在 `core/ledger/` 中实现双段式增量重放引擎（优先命中前驱快照），在交易写入与修改路径上增加级联快照失效保证位一致性，并在资产历史走势报表中接入真实时点汇率采样。

**Tech Stack:** C23, SQLite 3, PostgreSQL (libpq), csilk DB migration & pool abstraction, 128-bit decimal fixed-point arithmetic, CTest.

**Spec:** [`docs/superpowers/specs/2026-11-02-ledger-and-ai-workflow-engine-design.md`](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-11-02-ledger-and-ai-workflow-engine-design.md)

## Global Constraints

- **Language & Formatting**: 纯 C23 标准，严格遵守项目现存的 Clang-format 规范。
- **Financial Precision**: 严禁在快照与回放中引入 `double` 裸浮点数，必须使用 128 位定点数（`money_t`, `price_t`, `quantity_t`）与 `decimal.h` 算术算子。
- **Database Parity**: 所有迁移必须提供 SQLite (`sql/migrations/sqlite/V020*.sql`) 和 PostgreSQL (`sql/migrations/postgres/V020*.sql`) 双版本，并同步更新 `sql/migration.sql` 与 `sql/migration_postgres.sql`。
- **Transaction Safety**: 所有快照读写与级联失效必须包裹在业务已有的 `db_tx_scope_t` 事务保存点内，严格禁止裸 SQL 事务。
- **Regression Invariant**: 保持现存 45 个 CTest 测试套件与 `test_link.sh`（174 断言）100% 绿灯。

## Review Focus

- **RF-1 (Backdated insertion)**: 插入一条交易日期早于已有快照日期的历史交易时，必须自动失效该日期及之后的所有下游快照，重算状态不能受过期快照污染。
- **RF-2 (Historical FX fallback)**: 交易日期早于 `exchange_rate_history` 记录的最早日期时，汇率回填必须平滑回退至基准当前汇率（或 1.0），严禁除以零或产生 NaN。
- **RF-3 (Partial Replay Sorting)**: 增量重放不仅比较日期，在同日有多笔交易时必须严格按 `(transaction_date ASC, id ASC)` 顺序过滤，防止漏掉同日同 ID 或后序号交易。
- **RF-4 (Zero-transaction Baseline)**: 无交易的初始资产在生成快照或触发重建时，必须准确继承初始 `quantity`, `cost_basis`, `net_value`，不得被重设为 0。
- **RF-5 (Database Dialect JSON Compatibility)**: 快照表中的 `lots_snapshot_json` 字段在 PostgreSQL 与 SQLite 中统一为 TEXT 类型，以便后续 Milestone 2 无缝向下兼容。

---

### Task 1: V020 Database Migration for Ledger Snapshots and Historical FX Rate

**Files:**
- Create: `backend/sql/migrations/sqlite/V020__ledger_snapshots_and_tx_fx.sql`
- Create: `backend/sql/migrations/postgres/V020__ledger_snapshots_and_tx_fx.sql`
- Modify: `backend/sql/migration.sql`
- Modify: `backend/sql/migration_postgres.sql`
- Test: `backend/tests/unit/test_migration_engine.c`

**Interfaces:**
- Consumes: csilk migration engine (`<csilk/drivers/db/migration.h>`)
- Produces: `ledger_snapshots` table with index `idx_snapshots_lookup`, `transactions.fx_rate_to_cny` column with backfilled rates.

- [ ] **Step 1: Write SQLite V020 migration script**

Create `backend/sql/migrations/sqlite/V020__ledger_snapshots_and_tx_fx.sql`:
```sql
-- V020: 账本检查点快照与交易生效汇率
CREATE TABLE IF NOT EXISTS ledger_snapshots (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id             INTEGER NOT NULL,
    asset_id            INTEGER NOT NULL,
    snapshot_date       TEXT NOT NULL,
    quantity            TEXT NOT NULL,
    cost_basis          TEXT NOT NULL,
    net_value           TEXT NOT NULL,
    current_value       TEXT NOT NULL,
    realized_pnl        TEXT NOT NULL,
    balance             TEXT NOT NULL,
    tx_checkpoint_date  TEXT NOT NULL,
    tx_checkpoint_id    INTEGER NOT NULL,
    lots_snapshot_json  TEXT,
    created_at          TEXT DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, asset_id, snapshot_date)
);

CREATE INDEX IF NOT EXISTS idx_snapshots_lookup 
    ON ledger_snapshots(user_id, asset_id, snapshot_date DESC);

-- 为 transactions 添加历史汇率字段
ALTER TABLE transactions ADD COLUMN fx_rate_to_cny REAL DEFAULT 1.0;

-- 本币交易直接置 1.0
UPDATE transactions SET fx_rate_to_cny = 1.0 
WHERE asset_id IN (SELECT id FROM assets WHERE currency = 'CNY');
```

- [ ] **Step 2: Write PostgreSQL V020 migration script**

Create `backend/sql/migrations/postgres/V020__ledger_snapshots_and_tx_fx.sql`:
```sql
-- V020: 账本检查点快照与交易生效汇率 (PostgreSQL)
CREATE TABLE IF NOT EXISTS ledger_snapshots (
    id                  BIGSERIAL PRIMARY KEY,
    user_id             BIGINT NOT NULL,
    asset_id            BIGINT NOT NULL,
    snapshot_date       VARCHAR(32) NOT NULL,
    quantity            VARCHAR(64) NOT NULL,
    cost_basis          VARCHAR(64) NOT NULL,
    net_value           VARCHAR(64) NOT NULL,
    current_value       VARCHAR(64) NOT NULL,
    realized_pnl        VARCHAR(64) NOT NULL,
    balance             VARCHAR(64) NOT NULL,
    tx_checkpoint_date  VARCHAR(32) NOT NULL,
    tx_checkpoint_id    BIGINT NOT NULL,
    lots_snapshot_json  TEXT,
    created_at          TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, asset_id, snapshot_date)
);

CREATE INDEX IF NOT EXISTS idx_snapshots_lookup 
    ON ledger_snapshots(user_id, asset_id, snapshot_date DESC);

ALTER TABLE transactions ADD COLUMN IF NOT EXISTS fx_rate_to_cny DOUBLE PRECISION DEFAULT 1.0;

UPDATE transactions SET fx_rate_to_cny = 1.0 
WHERE asset_id IN (SELECT id FROM assets WHERE currency = 'CNY');
```

- [ ] **Step 3: Append DDL into `sql/migration.sql` and `sql/migration_postgres.sql`**

Append `ledger_snapshots` table definition and `ALTER TABLE transactions ADD COLUMN fx_rate_to_cny` to both baseline schema files.

- [ ] **Step 4: Run unit test `test_migration_engine` to verify migration applies clean**

Run: `cmake --build backend/build --target test_migration_engine && ./backend/build/test_migration_engine`
Expected: PASS with 100% migration verification.

- [ ] **Step 5: Commit Task 1**

```bash
git add backend/sql/migrations/ backend/sql/migration.sql backend/sql/migration_postgres.sql
git commit -m "feat(migration): 📦 deploy V020 ledger snapshots table and tx fx rate column"
```

---

### Task 2: Snapshot Repository & Replay Engine Incremental Rebuild

**Files:**
- Create: `backend/src/infrastructure/repositories/ledger_snapshot_repo_impl.h`
- Create: `backend/src/infrastructure/repositories/ledger_snapshot_repo_impl.c`
- Modify: `backend/src/core/ledger/ledger_engine.h`
- Modify: `backend/src/core/ledger/ledger_engine.c`
- Create: `backend/tests/unit/test_ledger_rebuild_checkpoint.c`
- Modify: `backend/CMakeLists.txt`

**Interfaces:**
- Consumes: `common/db.h`, `core/financial/*`, `ledger_snapshots` table
- Produces:
  - `int ledger_snapshot_get_latest(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id, const char* target_date, csilk_json_t** out_snapshot);`
  - `int ledger_snapshot_save(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id, const char* snapshot_date, const ledger_position_state_t* pos, money_t balance, const char* checkpoint_date, int64_t checkpoint_id, const char* lots_json);`
  - Two-stage `ledger_rebuild_position` in `core/ledger/ledger_engine.c`.

- [ ] **Step 1: Write unit test `test_ledger_rebuild_checkpoint.c` testing incremental replay from snapshot**

Create `backend/tests/unit/test_ledger_rebuild_checkpoint.c`:
```c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "common/db.h"
#include "core/financial/decimal.h"
#include "core/financial/money.h"
#include "core/financial/price.h"
#include "core/financial/quantity.h"
#include "core/ledger/ledger_engine.h"
#include "infrastructure/repositories/ledger_snapshot_repo_impl.h"

int main(void) {
    printf("Running test_ledger_rebuild_checkpoint...\n");
    const char* db_path = "/tmp/test_ledger_checkpoint.db";
    unlink(db_path);

    csilk_db_pool_t* pool = csilk_db_pool_create(db_path, 1);
    assert(pool != NULL);
    db_init(pool);

    /* Setup schema */
    csilk_db_exec(pool, "CREATE TABLE assets (id INTEGER PRIMARY KEY, user_id INTEGER, name TEXT, asset_type TEXT, currency TEXT, quantity TEXT, cost_basis TEXT, net_value TEXT, current_value TEXT, balance TEXT);");
    csilk_db_exec(pool, "CREATE TABLE transactions (id INTEGER PRIMARY KEY, user_id INTEGER, asset_id INTEGER, transaction_type TEXT, amount REAL, price_per_unit REAL, quantity REAL, fee REAL, transaction_date TEXT, fx_rate_to_cny REAL DEFAULT 1.0);");
    csilk_db_exec(pool, "CREATE TABLE ledger_snapshots (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER, asset_id INTEGER, snapshot_date TEXT, quantity TEXT, cost_basis TEXT, net_value TEXT, current_value TEXT, realized_pnl TEXT, balance TEXT, tx_checkpoint_date TEXT, tx_checkpoint_id INTEGER, lots_snapshot_json TEXT, created_at TEXT DEFAULT CURRENT_TIMESTAMP, UNIQUE(user_id, asset_id, snapshot_date));");

    /* 1. Insert initial asset and 2 historical transactions in 2026-01 */
    csilk_db_exec(pool, "INSERT INTO assets VALUES (1, 1, 'Test Fund', 'fund', 'CNY', '0', '0', '1.0', '0', '0');");
    csilk_db_exec(pool, "INSERT INTO transactions VALUES (1, 1, 1, 'buy', 1000.0, 1.0, 1000.0, 0.0, '2026-01-15 10:00:00', 1.0);");
    csilk_db_exec(pool, "INSERT INTO transactions VALUES (2, 1, 1, 'buy', 2000.0, 2.0, 1000.0, 0.0, '2026-01-20 10:00:00', 1.0);");

    /* Rebuild from zero: 2000 qty, 3000 cost */
    ledger_position_state_t state1 = {0};
    int rc = ledger_rebuild_position(pool, 1, 1, &state1);
    assert(rc == 0);
    assert(decimal_to_double(state1.quantity.units) == 2000.0);
    assert(decimal_to_double(state1.cost_basis.amount.units) == 3000.0);

    /* 2. Save a checkpoint snapshot for 2026-01-31 */
    rc = ledger_snapshot_save(pool, 1, 1, "2026-01-31", &state1, money_zero(CURRENCY_CNY), "2026-01-20 10:00:00", 2, NULL);
    assert(rc == 0);

    /* 3. Insert transaction 3 in 2026-02: sell 500 qty at 3.0 */
    csilk_db_exec(pool, "INSERT INTO transactions VALUES (3, 1, 1, 'sell', 1500.0, 3.0, 500.0, 0.0, '2026-02-10 10:00:00', 1.0);");

    /* Rebuild: must use checkpoint and replay only tx 3 -> qty=1500, cost=2250, realized_pnl=750 */
    ledger_position_state_t state2 = {0};
    rc = ledger_rebuild_position(pool, 1, 1, &state2);
    assert(rc == 0);
    assert(decimal_to_double(state2.quantity.units) == 1500.0);
    assert(decimal_to_double(state2.cost_basis.amount.units) == 2250.0);
    assert(decimal_to_double(state2.realized_pnl.amount.units) == 750.0);

    csilk_db_pool_destroy(pool);
    unlink(db_path);
    printf("PASS: test_ledger_rebuild_checkpoint completed successfully\n");
    return 0;
}
```

- [ ] **Step 2: Register `test_ledger_rebuild_checkpoint` in `CMakeLists.txt`**

Add to `backend/CMakeLists.txt`:
```cmake
add_executable(test_ledger_rebuild_checkpoint tests/unit/test_ledger_rebuild_checkpoint.c ${LIB_SOURCES})
target_include_directories(test_ledger_rebuild_checkpoint PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_ledger_rebuild_checkpoint PRIVATE -UNDEBUG)
target_link_libraries(test_ledger_rebuild_checkpoint PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
add_test(NAME test_ledger_rebuild_checkpoint COMMAND test_ledger_rebuild_checkpoint)
```

- [ ] **Step 3: Run test to verify it fails before implementation**

Run: `cmake --build backend/build --target test_ledger_rebuild_checkpoint`
Expected: FAIL with undefined reference to snapshot repo functions.

- [ ] **Step 4: Implement snapshot repo in `ledger_snapshot_repo_impl.h/.c`**

Implement `ledger_snapshot_get_latest` and `ledger_snapshot_save` using parameter queries (`?`).

- [ ] **Step 5: Refactor `ledger_rebuild_position` in `core/ledger/ledger_engine.c` to use snapshots**

1. Call `ledger_snapshot_get_latest(pool, user_id, asset_id, NULL, &snap)`.
2. If snapshot exists:
   - Initialize `curr_qty`, `curr_cost`, `latest_price`, `cum_realized` from snapshot.
   - Query `transactions` with condition: `WHERE user_id=? AND asset_id=? AND transaction_type IN ('buy', 'sell') AND (transaction_date > ? OR (transaction_date = ? AND id > ?)) ORDER BY transaction_date ASC, id ASC`.
3. If no snapshot exists:
   - Fall back to replaying from zero as before.
4. Update asset materialized fields atomically within `db_tx_scope_t`.

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build backend/build --target test_ledger_rebuild_checkpoint && ./backend/build/test_ledger_rebuild_checkpoint`
Expected: PASS.

- [ ] **Step 7: Commit Task 2**

```bash
git add backend/src/infrastructure/repositories/ledger_snapshot_repo_impl.* backend/src/core/ledger/ledger_engine.* backend/tests/unit/test_ledger_rebuild_checkpoint.c backend/CMakeLists.txt
git commit -m "feat(ledger): ⚡ implement checkpoint snapshot repo and incremental position rebuild"
```

---

### Task 3: Snapshot Invalidation on Backdated Insertions, Updates, and Deletions

**Files:**
- Modify: `backend/src/infrastructure/repositories/ledger_snapshot_repo_impl.h`
- Modify: `backend/src/infrastructure/repositories/ledger_snapshot_repo_impl.c`
- Modify: `backend/src/application/transaction/usecases.c:100-350`
- Modify: `backend/tests/unit/test_ledger_rebuild_checkpoint.c`

**Interfaces:**
- Consumes: `ledger_snapshot_repo_impl.h`, `application/transaction/usecases.c`
- Produces: `int ledger_snapshot_invalidate_from(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id, const char* from_date);`

- [ ] **Step 1: Add failing test case in `test_ledger_rebuild_checkpoint.c` for backdated mutation**

Add test scenario:
1. Asset has a snapshot at `2026-01-31`.
2. A backdated transaction is inserted on `2026-01-18`.
3. `ledger_snapshot_invalidate_from(pool, 1, 1, "2026-01-18")` is invoked.
4. Verify `ledger_snapshot_get_latest` on or after `2026-01-18` returns no snapshot.
5. `ledger_rebuild_position` runs and produces the correct recalculated balance incorporating the backdated transaction.

- [ ] **Step 2: Run test to verify failure**

Run: `cmake --build backend/build --target test_ledger_rebuild_checkpoint && ./backend/build/test_ledger_rebuild_checkpoint`
Expected: FAIL with undefined reference `ledger_snapshot_invalidate_from`.

- [ ] **Step 3: Implement `ledger_snapshot_invalidate_from` in `ledger_snapshot_repo_impl.c`**

```c
int ledger_snapshot_invalidate_from(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id, const char* from_date) {
    if (!pool || user_id <= 0 || asset_id <= 0 || !from_date) return -1;
    char uid_str[32], aid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    const char* params[] = {uid_str, aid_str, from_date, NULL};
    return csilk_db_exec_param(pool, 
        "DELETE FROM ledger_snapshots WHERE user_id=? AND asset_id=? AND snapshot_date >= ?", 
        params);
}
```

- [ ] **Step 4: Hook snapshot invalidation in `application/transaction/usecases.c`**

1. In `tx_usecase_create`: before commit, call `ledger_snapshot_invalidate_from(pool, cmd->user_id, cmd->asset_id, cmd->transaction_date)`.
2. In `tx_usecase_update`: before commit, invalidate from `MIN(old_tx_date, new_tx_date)`.
3. In `tx_usecase_delete`: before commit, invalidate from `old_tx_date`.

- [ ] **Step 5: Run tests to verify pass**

Run: `cmake --build backend/build --target test_ledger_rebuild_checkpoint && ./backend/build/test_ledger_rebuild_checkpoint`
Expected: PASS.

- [ ] **Step 6: Commit Task 3**

```bash
git add backend/src/infrastructure/repositories/ledger_snapshot_repo_impl.* backend/src/application/transaction/usecases.c backend/tests/unit/test_ledger_rebuild_checkpoint.c
git commit -m "fix(ledger): 🛡️ cascade invalidate downstream snapshots on backdated transaction changes"
```

---

### Task 4: Historical Point-in-Time FX Rate Alignment in Portfolio & Trend Reports

**Files:**
- Modify: `backend/src/services/market/exchange_rate_service.h`
- Modify: `backend/src/services/market/exchange_rate_service.c`
- Modify: `backend/src/infrastructure/repositories/portfolio_repo_impl.c:420-620`
- Modify: `backend/src/application/transaction/usecases.c:50-130`
- Test: `backend/tests/unit/test_fx_semantics.c`

**Interfaces:**
- Consumes: `exchange_rate_history` table, `transactions.fx_rate_to_cny`
- Produces: `double exchange_rate_get_historical_or_latest(csilk_db_pool_t* pool, currency_t cur, const char* sample_date);`

- [ ] **Step 1: Write unit test in `test_fx_semantics.c` for historical rate lookup with fallback**

Add test case in `backend/tests/unit/test_fx_semantics.c` verifying:
1. Lookup with valid historical date returns historical quote.
2. Lookup with non-existent old date falls back to latest rate without error or NaN.
3. CNY to CNY always returns 1.0.

- [ ] **Step 2: Run test to verify failure**

Run: `cmake --build backend/build --target test_fx_semantics && ./backend/build/test_fx_semantics`
Expected: FAIL with missing `exchange_rate_get_historical_or_latest`.

- [ ] **Step 3: Implement `exchange_rate_get_historical_or_latest` in `exchange_rate_service.c`**

Query `exchange_rate_history` for `quote_date <= sample_date ORDER BY quote_date DESC LIMIT 1`. If found, return rate; if not found, return `exchange_rate_get_to_cny(cur)`.

- [ ] **Step 4: Populate `fx_rate_to_cny` at transaction creation time in `transaction/usecases.c`**

When creating a transaction, look up the effective rate for `transaction_date` and insert into `transactions(..., fx_rate_to_cny)`.

- [ ] **Step 5: Update `portfolio_repo_impl.c` historical trend calculation**

In `portfolio_repo_impl.c` trend loop: replace `exchange_rate_get_to_cny(cur)` with `exchange_rate_get_historical_or_latest(pool, cur, sample_date)`.

- [ ] **Step 6: Run `test_fx_semantics` to verify it passes**

Run: `cmake --build backend/build --target test_fx_semantics && ./backend/build/test_fx_semantics`
Expected: PASS.

- [ ] **Step 7: Commit Task 4**

```bash
git add backend/src/services/market/exchange_rate_service.* backend/src/infrastructure/repositories/portfolio_repo_impl.c backend/src/application/transaction/usecases.c backend/tests/unit/test_fx_semantics.c
git commit -m "feat(portfolio): 📈 evaluate historical asset trends against point-in-time exchange rates"
```

---

### Task 5: Full Integration Verification Gate

**Files:**
- Test: Full CTest unit test suite
- Test: `backend/tests/test_link.sh`
- Test: `backend/tests/test_mcp.sh`
- Test: `backend/tests/test_mcp_server.sh`
- Test: `backend/tests/test_ai_loop_hardening.sh`

- [ ] **Step 1: Run full unit test suite**

Run: `cmake --build backend/build --target all && ctest --test-dir backend/build --output-on-failure`
Expected: 46/46 tests passed (100%).

- [ ] **Step 2: Run core integration test suite**

Run: `bash backend/tests/test_link.sh`
Expected: PASS=174 FAIL=0.

- [ ] **Step 3: Run MCP and AI Loop regression suites**

Run:
```bash
bash backend/tests/test_mcp.sh
bash backend/tests/test_mcp_server.sh
bash backend/tests/test_ai_loop_hardening.sh
```
Expected: All suites PASS.

- [ ] **Step 4: Update plan checkmarks and commit**

```bash
git add docs/superpowers/plans/2026-11-02-ledger-checkpointing-and-fx-rates.md
git commit -m "docs: 📝 mark milestone 1 implementation plan as fully verified"
```
