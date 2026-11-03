# 多计价成本法与先进先出（FIFO）批次核算引擎（Milestone 2）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 Minefolio 投资标的建立多策略成本结转体系，引入先进先出（FIFO）批次追踪表（`transaction_lots`），支持资产层成本计量方法配置与不可逆锁定，并在买入时建批、卖出时逐层核销计算精确已实现盈亏，结合增量快照实现批次状态固化。

**Architecture:** 在数据库层通过 Flyway 模式部署 V021 迁移（资产增加 `cost_method` 字段，新建 `transaction_lots` 批次表）；在 `core/ledger/` 中引入成本策略算子，支持移动加权平均（Moving Average）与先进先出（FIFO）；在 `application/asset/` 中实现资产存在交易时锁定成本法规则；在 `ledger_engine.c` 中联动快照批次序列化与增量重放。

**Tech Stack:** C23, SQLite 3, PostgreSQL (libpq), csilk DB abstraction, 128-bit decimal fixed-point arithmetic (`decimal.h`, `money.h`, `price.h`, `quantity.h`), CTest.

**Spec:** [`docs/superpowers/specs/2026-11-02-ledger-and-ai-workflow-engine-design.md`](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-11-02-ledger-and-ai-workflow-engine-design.md)

## Global Constraints

- **Language & Formatting**: 纯 C23 标准，代码严格符合项目现存的 Clang-format 规范。
- **Financial Precision**: 严禁在批次扣减、单位成本与盈亏核算中引入裸 `double` 运算，必须使用 128 位定点数（`money_t`, `price_t`, `quantity_t`）与 `decimal.h` 高精度除法算子。
- **Database Parity**: 所有数据库变更必须同时提供 SQLite (`sql/migrations/sqlite/V021*.sql`) 和 PostgreSQL (`sql/migrations/postgres/V021*.sql`) 双方言版本，并同步更新 `sql/migration.sql` 与 `sql/migration_postgres.sql`。
- **Transaction Safety**: 所有批次创建、扣减、重置及资产更新必须包裹在业务已有的 `db_tx_scope_t` 事务保存点内。
- **Regression Invariant**: 保证现存 46 个 CTest 单元测试套件与 `test_link.sh`（174 断言）100% 绿灯。

## Review Focus

- **RF-1 (Partial Lot Consumption)**: 当一笔卖出交易横跨多个批次（如第一个批次剩余 300 股，卖出 500 股，需消耗第一批次全量 300 股并消耗第二批次 200 股），剩余份额必须精确更新为 0 和相应减少值，批次状态从 `open` 转为 `closed`，成本扣减和已实现盈亏必须分段累加精确至分。
- **RF-2 (Cost Method Freeze on Transactions Exist)**: 当资产已存在任何已入账交易（`transactions` 表中存在记录）时，修改资产接口若试图改变 `cost_method`（例如由 `weighted_average` 改为 `fifo` 或反之），必须严格阻断并返回 HTTP 1004 错误码，不能篡改既有历史核算基础。
- **RF-3 (Rebuild Replay Lots Reset)**: 从零重建 FIFO 资产时，旧批次记录必须在原子事务内彻底重置清空（`DELETE FROM transaction_lots WHERE user_id=? AND asset_id=?`），并按交易时间戳严格升序逐笔重建批次，保证重算结果与初始录入结果 100% 幂等一致。
- **RF-4 (Checkpoint Serialization Roundtrip)**: 为 FIFO 资产生成检查点快照时，所有当前未平仓（`open`）的批次必须完整结构化序列化到 `ledger_snapshots.lots_snapshot_json` 中；从快照增量重算时必须能从 JSON 还原批次链表，不能丢失单批次的成交价与剩余数量。
- **RF-5 (Fee Allocation per Lot)**: 买入批次的单位成本计算公式必须包含买入手续费 `cost_per_unit = (buy_gross + buy_fee) / buy_qty`，严禁遗漏手续费造成计税本金低估或浮动盈亏计算失真。

---

### Task 1: V021 Database Migration for Asset Cost Method & Transaction Lots

**Files:**
- Create: `backend/sql/migrations/sqlite/V021__asset_cost_method_and_lots.sql`
- Create: `backend/sql/migrations/postgres/V021__asset_cost_method_and_lots.sql`
- Modify: `backend/sql/migration.sql`
- Modify: `backend/sql/migration_postgres.sql`
- Modify: `backend/tests/unit/test_migration_engine.c`

**Interfaces:**
- Consumes: csilk migration engine (`<csilk/drivers/db/migration.h>`)
- Produces: `assets.cost_method` column with default `'weighted_average'`, `transaction_lots` table with composite index `idx_lots_open`.

- [x] **Step 1: Write SQLite V021 migration script** (Commit: `079115cd`)

Create `backend/sql/migrations/sqlite/V021__asset_cost_method_and_lots.sql`:
```sql
-- V021: 资产多策略成本结转与交易批次明细表
ALTER TABLE assets ADD COLUMN cost_method TEXT DEFAULT 'weighted_average';

CREATE TABLE IF NOT EXISTS transaction_lots (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id             INTEGER NOT NULL,
    asset_id            INTEGER NOT NULL,
    buy_tx_id           INTEGER NOT NULL,
    buy_date            TEXT NOT NULL,
    original_quantity   TEXT NOT NULL,
    remaining_quantity  TEXT NOT NULL,
    cost_per_unit       TEXT NOT NULL,
    fee_allocated       TEXT NOT NULL,
    status              TEXT DEFAULT 'open',
    created_at          TEXT DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(buy_tx_id) REFERENCES transactions(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_lots_open 
    ON transaction_lots(user_id, asset_id, status, buy_date ASC, id ASC);
```

- [x] **Step 2: Write PostgreSQL V021 migration script** (Commit: `079115cd`)

Create `backend/sql/migrations/postgres/V021__asset_cost_method_and_lots.sql`:
```sql
-- V021: 资产多策略成本结转与交易批次明细表 (PostgreSQL)
ALTER TABLE assets ADD COLUMN IF NOT EXISTS cost_method VARCHAR(32) DEFAULT 'weighted_average';

CREATE TABLE IF NOT EXISTS transaction_lots (
    id                  BIGSERIAL PRIMARY KEY,
    user_id             BIGINT NOT NULL,
    asset_id            BIGINT NOT NULL,
    buy_tx_id           BIGINT NOT NULL,
    buy_date            VARCHAR(32) NOT NULL,
    original_quantity   VARCHAR(64) NOT NULL,
    remaining_quantity  VARCHAR(64) NOT NULL,
    cost_per_unit       VARCHAR(64) NOT NULL,
    fee_allocated       VARCHAR(64) NOT NULL,
    status              VARCHAR(16) DEFAULT 'open',
    created_at          TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(buy_tx_id) REFERENCES transactions(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_lots_open 
    ON transaction_lots(user_id, asset_id, status, buy_date ASC, id ASC);
```

- [x] **Step 3: Append DDL into `sql/migration.sql` and `sql/migration_postgres.sql`** (Commit: `079115cd`)

Append `ALTER TABLE assets ADD COLUMN cost_method ...` and `transaction_lots` table definition to both baseline files.

- [x] **Step 4: Update `test_migration_engine.c` assertions and run test** (Commit: `079115cd`)

Update migration count assertion to `>= 21` and add table presence check for `transaction_lots`.
Run: `cmake --build backend/build --target test_migration_engine && ./backend/build/test_migration_engine`
Expected: PASS with 100% verification.

- [x] **Step 5: Commit Task 1** (Commit: `079115cd`)

```bash
git add backend/sql/migrations/ backend/sql/migration.sql backend/sql/migration_postgres.sql backend/tests/unit/test_migration_engine.c
git commit -m "feat(migration): 📦 deploy V021 asset cost method and transaction lots table"
```

---

### Task 2: Transaction Lots Infrastructure Repository

**Files:**
- Create: `backend/src/infrastructure/repositories/transaction_lot_repo_impl.h`
- Create: `backend/src/infrastructure/repositories/transaction_lot_repo_impl.c`
- Create: `backend/tests/unit/test_transaction_lots.c`
- Modify: `backend/CMakeLists.txt`

**Interfaces:**
- Consumes: `common/db.h`, `core/financial/*`, `transaction_lots` table
- Produces:
  ```c
  int64_t tx_lot_create(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id, int64_t buy_tx_id, const char* buy_date, quantity_t qty, price_t cost_per_unit, money_t fee);
  csilk_json_t* tx_lots_get_open(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id);
  int tx_lot_deduct(csilk_db_pool_t* pool, int64_t lot_id, quantity_t deduct_qty, quantity_t new_remaining_qty, bool is_closed);
  int tx_lots_delete_by_asset(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id);
  int tx_lots_delete_by_buy_tx(csilk_db_pool_t* pool, int64_t user_id, int64_t buy_tx_id);
  char* tx_lots_serialize_open_json(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id);
  ```

- [x] **Step 1: Write unit test `test_transaction_lots.c`** (Commit: `eedf3e27`)

Create `backend/tests/unit/test_transaction_lots.c` testing:
1. `tx_lot_create` creates open lot with 128-bit fixed point quantities.
2. `tx_lots_get_open` returns lots sorted by `buy_date ASC, id ASC`.
3. `tx_lot_deduct` partially deducts lot and updates remaining quantity; fully deducts and marks `status='closed'`.
4. `tx_lots_serialize_open_json` produces valid JSON array of open lots.
5. `tx_lots_delete_by_asset` cleans up lots cleanly.

- [x] **Step 2: Register `test_transaction_lots` in `backend/CMakeLists.txt`** (Commit: `eedf3e27`)

Add `test_transaction_lots` via `add_financial_unit_test`.

- [x] **Step 3: Run test to verify failure before implementation** (Commit: `eedf3e27`)

Run: `cmake --build backend/build --target test_transaction_lots`
Expected: FAIL with missing headers/functions.

- [x] **Step 4: Implement `transaction_lot_repo_impl.h` and `transaction_lot_repo_impl.c`** (Commit: `eedf3e27`)

Implement parameterized queries with `?` placeholders, 128-bit decimal serialization (`quantity_to_string_fixed`, `price_to_string`, `money_to_string`), and clean memory management.

- [x] **Step 5: Run test to verify pass** (Commit: `eedf3e27`)

Run: `cmake --build backend/build --target test_transaction_lots && ./backend/build/test_transaction_lots`
Expected: PASS.

- [x] **Step 6: Commit Task 2** (Commit: `eedf3e27`)

```bash
git add backend/src/infrastructure/repositories/transaction_lot_repo_impl.* backend/tests/unit/test_transaction_lots.c backend/CMakeLists.txt
git commit -m "feat(ledger): 📦 implement transaction lots repository for FIFO queue tracking"
```

---

### Task 3: FIFO Cost Basis Accounting & Multi-Lot Sell Deduction Engine

**Files:**
- Modify: `backend/src/core/ledger/ledger_engine.h`
- Modify: `backend/src/core/ledger/ledger_engine.c`
- Create: `backend/tests/unit/test_cost_basis_fifo.c`
- Modify: `backend/CMakeLists.txt`

**Interfaces:**
- Consumes: `transaction_lot_repo_impl.h`, `core/financial/*`, `ledger_snapshots`
- Produces:
  ```c
  int ledger_calc_sell_position_fifo(csilk_db_pool_t* pool,
                                     int64_t          user_id,
                                     int64_t          asset_id,
                                     quantity_t       sell_qty,
                                     price_t          sell_price,
                                     money_t          sell_fee,
                                     quantity_t*      out_qty,
                                     money_t*         out_cost,
                                     money_t*         out_cost_reduction,
                                     money_t*         out_realized_pnl);
  ```

- [x] **Step 1: Write unit test `test_cost_basis_fifo.c`** (Commits: `6688f77d`, `48e76fa8`)

Create `backend/tests/unit/test_cost_basis_fifo.c`:
- Scenario 1: Buy 100 shares @ 10 CNY (Lot 1), Buy 100 shares @ 20 CNY (Lot 2). Total = 200 shares, 3000 CNY cost.
- Scenario 2: Sell 50 shares @ 25 CNY:
  - Consumes 50 from Lot 1 @ 10 CNY. Cost reduction = 500 CNY. Realized PnL = 1250 - 500 = 750 CNY.
  - Remaining: Lot 1 has 50 shares @ 10 CNY, Lot 2 has 100 shares @ 20 CNY. Total cost basis = 2500 CNY.
- Scenario 3 (RF-1): Multi-lot consumption: Sell 100 shares @ 30 CNY:
  - Consumes remaining 50 from Lot 1 (cost 500) and 50 from Lot 2 (cost 1000). Total cost reduction = 1500 CNY.
  - Realized PnL = 3000 - 1500 = 1500 CNY. Lot 1 becomes `closed`. Lot 2 has 50 shares @ 20 CNY remaining. Total cost basis = 1000 CNY.
- Scenario 4 (RF-5): Buy with fee: fee correctly allocated to unit cost `(gross + fee) / qty`.
- Scenario 5: Rebuild position: resets lots and replays FIFO transactions to exact same values (RF-3).
- Scenario 6: Checkpoint snapshot saves open lots to `lots_snapshot_json` (RF-4).

- [x] **Step 2: Register in `backend/CMakeLists.txt` and verify RED failure** (Commit: `6688f77d`)

Run: `cmake --build backend/build --target test_cost_basis_fifo`
Expected: FAIL.

- [x] **Step 3: Implement FIFO sell calculator and integrate with `ledger_apply_tx` and `ledger_rebuild_position`** (Commits: `6688f77d`, `48e76fa8`)

1. In `ledger_apply_tx`:
   - Inspect asset `cost_method` (from `assets.cost_method`).
   - If `cost_method == 'fifo'`:
     - On buy: call `tx_lot_create` to record the new lot with `cost_per_unit = (amount + fee) / quantity`.
     - On sell: call `ledger_calc_sell_position_fifo` to deduct from open lots and compute exact realized PnL.
   - If `cost_method == 'weighted_average'` (or default):
     - Use existing weighted average math.
2. In `ledger_rebuild_position`:
   - Query `assets.cost_method`.
   - If `fifo`:
     - If rebuilding from zero: call `tx_lots_delete_by_asset(pool, user_id, asset_id)` before replay (RF-3).
     - Replay buys and sells through FIFO logic.
3. In `ledger_snapshot_save`:
   - When snapshotting a FIFO asset, call `tx_lots_serialize_open_json` and pass into `lots_snapshot_json` (RF-4).

- [x] **Step 4: Run test to verify GREEN pass** (Commits: `6688f77d`, `48e76fa8`)

Run: `cmake --build backend/build --target test_cost_basis_fifo && ./backend/build/test_cost_basis_fifo`
Expected: PASS.

- [x] **Step 5: Commit Task 3** (Commits: `6688f77d`, `48e76fa8`)

```bash
git add backend/src/core/ledger/ledger_engine.* backend/tests/unit/test_cost_basis_fifo.c backend/CMakeLists.txt
git commit -m "feat(ledger): ⚡ implement FIFO cost basis allocation and multi-lot sell deduction engine"
```

---

### Task 4: Asset Use Case Cost Method Protection & Immutability Rules

**Files:**
- Modify: `backend/src/domain/asset/mf_asset_entity.h`
- Modify: `backend/src/domain/asset/mf_asset_rules.h`
- Modify: `backend/src/domain/asset/mf_asset_rules.c`
- Modify: `backend/src/application/asset/usecases.c`
- Modify: `backend/src/infrastructure/repositories/asset_repo_impl.h`
- Modify: `backend/src/infrastructure/repositories/asset_repo_impl.c`
- Modify: `backend/src/dtos/asset_dto.h`
- Modify: `backend/tests/unit/test_domain_asset.c`

**Interfaces:**
- Consumes: `domain/asset/*`, `application/asset/*`
- Produces:
  - `mf_asset_t.cost_method` (`weighted_average`, `fifo`).
  - Validation rule: `cost_method` must be valid enum value.
  - Immutability check in `asset_usecase_update`: If asset has transactions, attempt to change `cost_method` returns HTTP 1004 (RF-2).

- [x] **Step 1: Write unit tests in `test_domain_asset.c` for `cost_method` validation** (Commit: `8d7b23a0`)

Add test cases in `backend/tests/unit/test_domain_asset.c`:
1. Valid `cost_method` values (`weighted_average`, `fifo`, empty defaults to `weighted_average`).
2. Invalid `cost_method` (e.g. `invalid_method`) rejected with validation error.

- [x] **Step 2: Run test to verify failure** (Commit: `8d7b23a0`)

Run: `cmake --build backend/build --target test_domain_asset && ./backend/build/test_domain_asset`
Expected: FAIL.

- [x] **Step 3: Implement domain rules and repository extensions** (Commit: `8d7b23a0`)

1. In `mf_asset_entity.h`: add `char cost_method[32]`.
2. In `mf_asset_rules.c`: add validation for `cost_method`.
3. In `asset_repo_impl.c`: include `cost_method` in `asset_insert`, `asset_update`, and query SELECT statements.
4. In `asset_usecase_create`: accept optional `cost_method` (default `'weighted_average'`).
5. In `asset_usecase_update` (RF-2):
   - Check if `cmd->cost_method` is supplied and differs from current `cost_method`.
   - If different:
     ```c
     csilk_json_t* tx_check = csilk_db_query_param_json(
         db_pool,
         "SELECT COUNT(*) as cnt FROM transactions WHERE user_id=? AND asset_id=?",
         (const char*[]){uid_str, aid_str, NULL});
     int64_t tx_cnt = db_get_int(csilk_json_array_get(tx_check, 0), "cnt");
     csilk_json_free(tx_check);
     if (tx_cnt > 0) {
         out_res->code = 1004;
         snprintf(out_res->message, sizeof(out_res->message), "资产已存在交易记录，禁止变更成本结转方法");
         return -1;
     }
     ```

- [x] **Step 4: Run test to verify pass** (Commit: `8d7b23a0`)

Run: `cmake --build backend/build --target test_domain_asset && ./backend/build/test_domain_asset`
Expected: PASS.

- [x] **Step 5: Commit Task 4** (Commit: `8d7b23a0`)

```bash
git add backend/src/domain/asset/* backend/src/application/asset/* backend/src/infrastructure/repositories/asset_repo_impl.* backend/src/dtos/asset_dto.h backend/tests/unit/test_domain_asset.c
git commit -m "feat(asset): 🛡️ enforce cost method validation and freeze on assets with transactions"
```

---

### Task 5: Full Integration Verification Gate & Regression Suite

**Files:**
- Test: Full CTest unit test suite (48 suites)
- Test: `backend/tests/test_link.sh`
- Test: `backend/tests/test_mcp.sh`
- Test: `backend/tests/test_mcp_server.sh`
- Test: `backend/tests/test_ai_loop_hardening.sh`
- Modify: `docs/superpowers/plans/2026-11-03-multi-method-cost-basis-fifo.md`

- [x] **Step 1: Run full unit test suite** (Evidence: 48/48 CTest suites passed 100%)

Run: `cmake --build backend/build --target all && ctest --test-dir backend/build --output-on-failure`
Expected: 48/48 tests passed (100%).
Actual: 48/48 passed in 0.90 sec.

- [x] **Step 2: Run core integration test suite** (Evidence: 40 cases, PASS=174, FAIL=0)

Run: `bash backend/tests/test_link.sh`
Expected: PASS=174+ FAIL=0.
Actual: PASS=174, FAIL=0 (100%).

- [x] **Step 3: Run MCP and AI Loop regression suites** (Evidence: `test_mcp.sh` PASS=14, `test_mcp_server.sh` PASS=10, `test_ai_loop_hardening.sh` PASS=28)

Run:
```bash
bash backend/tests/test_mcp.sh
bash backend/tests/test_mcp_server.sh
bash backend/tests/test_ai_loop_hardening.sh
```
Expected: All suites PASS.
Actual:
- `test_mcp.sh`: PASS=14, FAIL=0, SKIP=1
- `test_mcp_server.sh`: PASS=10, FAIL=0
- `test_ai_loop_hardening.sh`: PASS=28, FAIL=0

- [x] **Step 4: Run frontend production build** (Evidence: `npm --prefix frontend run build` 0 errors, built in 11.29s)

Run: `npm --prefix frontend run build`
Expected: 0 errors.
Actual: Built successfully in 11.29s (`dist/` generated with 0 errors).

- [x] **Step 5: Update plan checkmarks and commit**

```bash
git add docs/superpowers/plans/2026-11-03-multi-method-cost-basis-fifo.md
git commit -m "docs: 📝 mark milestone 2 implementation plan as fully verified"
```
