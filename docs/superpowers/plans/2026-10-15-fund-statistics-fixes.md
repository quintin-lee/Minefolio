# Fund Statistics Logic Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Thoroughly fix all 12 fund and money statistics bugs across Minefolio's backend and frontend, ensuring financial accuracy, correct multi-currency conversion, ledger space isolation, and robust ledger rebuild replay.

**Architecture:** 
- Fixes are divided cleanly across the DDD architecture layers: Core financial/ledger engine (`core/ledger/`), Domain portfolio rules (`domain/portfolio/`), Infrastructure repositories (`infrastructure/repositories/`), Application usecases (`application/`), Interfaces/HTTP controllers, and Vue 3 frontend components.
- All code comments must be in English. All C changes follow C23 standard.
- Tests (CTest unit suites, integration tests, and Vitest) are updated and run at each task.

**Tech Stack:** C23, CMake, SQLite / PostgreSQL, csilk HTTP framework, Vue 3, TypeScript, Vitest.

---

### Task 1: Fix Ledger Rebuild Engine Initial Balance & Position Baseline (Items 11 & 12)

**Files:**
- Modify: `backend/src/infrastructure/repositories/asset_repo_impl.c`
- Modify: `backend/src/core/ledger/ledger_engine.c`
- Modify: `backend/src/common/balance.c`
- Test: `backend/tests/unit/test_ledger_engine.c`

- [ ] **Step 1: Write unit test reproducing initial balance loss during `ledger_rebuild_account`**
  Add a test case in `tests/unit/test_ledger_engine.c` that inserts an asset with `current_value = 10000`, runs `ledger_rebuild_account`, and asserts that `current_value` remains `10000` rather than resetting to `0`.
- [ ] **Step 2: Run test to verify it fails**
  Run: `cmake --build backend/build && ctest --test-dir backend/build -R test_ledger_engine --output-on-failure`
- [ ] **Step 3: Implement baseline log generation on asset creation and update**
  In `asset_insert`, if `current_value != 0`, insert an initial audit row into `asset_balance_logs` with `source_type = 'init'`, `delta = current_value`, `balance_after = current_value`.
  In `asset_update_basic`, if `current_value` changed, compute the delta and log to `asset_balance_logs` with `source_type = 'adjustment'`.
  In `ledger_rebuild_position`, preserve the asset's initial quantity/cost basis when no buy/sell transaction rows exist.
- [ ] **Step 4: Run test to verify it passes**
  Run: `cmake --build backend/build && ctest --test-dir backend/build -R test_ledger_engine --output-on-failure`
- [ ] **Step 5: Commit changes**
  Commit message: `fix(ledger): preserve initial asset balance and position during rebuild`

---

### Task 2: Fix Portfolio Rules Fee Handling in Trade Events (Item 8)

**Files:**
- Modify: `backend/src/domain/portfolio/rules.c`
- Test: `backend/tests/unit/test_portfolio_rules.c`

- [ ] **Step 1: Write test case for fee inclusion in buy cost and sell proceeds**
  In `test_portfolio_rules.c`, test that `mf_portfolio_rule_apply_trade_events` incorporates `ev->fee` into `acc->cost_for_pnl` on `buy`, and deducts `ev->fee` from `trade_realized` on `sell`.
- [ ] **Step 2: Run test to verify it fails**
  Run: `cmake --build backend/build && ctest --test-dir backend/build -R test_portfolio_rules --output-on-failure`
- [ ] **Step 3: Implement fee handling in `mf_portfolio_rule_apply_trade_events`**
  In `domain/portfolio/rules.c`:
  - On `buy`: `cost_for_pnl += amount + fee` (via `money_add`).
  - On `sell`: `trade_realized = (amount - fee) - sold_cost` (via `money_sub`).
- [ ] **Step 4: Run test to verify it passes**
  Run: `cmake --build backend/build && ctest --test-dir backend/build -R test_portfolio_rules --output-on-failure`
- [ ] **Step 5: Commit changes**
  Commit message: `fix(portfolio): incorporate trade fees into cost basis and realized pnl calculation`

---

### Task 3: Fix Transaction Performance Multi-Asset Grouping & Realized Formula (Item 1)

**Files:**
- Modify: `backend/src/application/portfolio/usecases.c`
- Test: `backend/tests/unit/test_portfolio_rules.c` or new test in `test_link.sh`

- [ ] **Step 1: Inspect and write failing test for `portfolio_usecase_get_performance`**
  Verify that two different assets (e.g. Asset 1 and Asset 2) maintain independent cost basis and average prices, and that sell realized PnL equals `(sell_price - avg_cost) * qty - fee` rather than `-fee`.
- [ ] **Step 2: Run test to verify failure**
- [ ] **Step 3: Implement per-asset tracking and correct realized formula**
  In `portfolio_usecase_get_performance` (`application/portfolio/usecases.c`):
  - Maintain an asset-keyed accumulator array/table (`asset_id`, `total_qty`, `total_cost`, `currency`).
  - Update cost and quantity per asset.
  - Calculate `realized = (price - avg_cost) * qty - fee` per trade.
  - Skip child fee transactions (`parent_tx_id IS NOT NULL` / `type == 'fee' && parent_tx_id > 0`) from double counting.
  - Convert values to base currency using `exchange_rate_convert`.
- [ ] **Step 4: Run test to verify it passes**
  Run: `cmake --build backend/build && ctest --test-dir backend/build --output-on-failure`
- [ ] **Step 5: Commit changes**
  Commit message: `fix(portfolio): group transaction performance by asset and correct realized pnl formula`

---

### Task 4: Fix Cashflow Actual Events Query for Dividends & Interests (Item 9)

**Files:**
- Modify: `backend/src/infrastructure/repositories/cashflow_repo_impl.c`
- Test: `backend/tests/test_dca_cashflow.sh`

- [ ] **Step 1: Write test case verifying dividend/interest transaction matching in cashflow calendar**
- [ ] **Step 2: Run test to verify failure**
- [ ] **Step 3: Update `SQL_ACTUAL_TXS` in `cashflow_repo_impl.c`**
  Change:
  `t.transaction_type IN ('income', 'deposit')`
  To:
  `t.transaction_type IN ('income', 'deposit', 'dividend', 'interest')`
- [ ] **Step 4: Run test to verify it passes**
  Run: `./backend/tests/test_dca_cashflow.sh`
- [ ] **Step 5: Commit changes**
  Commit message: `fix(cashflow): include dividend and interest transactions in actual event matching`

---

### Task 5: Fix 30-Day Asset Change Calculation in `report_asset_summary` (Item 7)

**Files:**
- Modify: `backend/src/services/report_asset_service.c`
- Test: `backend/tests/test_link.sh`

- [ ] **Step 1: Write test reproducing investment buy distorting 30-day net asset change**
  Test buying a stock with 10,000 cash from funding account, and assert `change_30d` does NOT jump by +10,000.
- [ ] **Step 2: Run test to verify failure**
- [ ] **Step 3: Implement clean net asset change calculation**
  In `report_asset_summary` (`services/report_asset_service.c`):
  Filter `transactions` query to exclude internal investment asset reallocations (`transaction_type NOT IN ('buy', 'sell')`), or calculate net change directly via net delta of deposits and withdrawals, converted to base currency.
- [ ] **Step 4: Run test to verify it passes**
  Run: `./backend/tests/test_link.sh`
- [ ] **Step 5: Commit changes**
  Commit message: `fix(report): exclude internal investment trades from 30d net asset change`

---

### Task 6: Fix FX PnL Attribution Model & Remove 0.98 Fallback (Item 3)

**Files:**
- Modify: `backend/src/services/report_asset_service.c`
- Test: `backend/tests/test_fx_oauth.sh`

- [ ] **Step 1: Review existing FX PnL tests in `test_fx_oauth.sh`**
- [ ] **Step 2: Update `report_fx_pnl`**
  - Match historical exchange rate based on asset's actual `created_at` or transaction date rather than `ORDER BY rate_date ASC LIMIT 1`.
  - When no historical rate exists, fallback to `curr_fx_r` (current rate, resulting in 0 FX fluctuation gain) instead of multiplying by 0.98.
- [ ] **Step 3: Run test to verify it passes**
  Run: `./backend/tests/test_fx_oauth.sh`
- [ ] **Step 4: Commit changes**
  Commit message: `fix(fx): remove synthetic 0.98 exchange rate fallback and align cost rate to creation date`

---

### Task 7: Fix Multi-Currency Direct Summation in Expense & Dashboard Reports (Items 4 & 5)

**Files:**
- Modify: `backend/src/services/report_expense_service.c`
- Modify: `backend/src/infrastructure/repositories/portfolio_repo_impl.c`
- Modify: `backend/src/application/portfolio/usecases.c`
- Test: `backend/tests/test_link.sh`

- [ ] **Step 1: Write test verifying foreign currency daily expenses are converted to base currency in monthly summary**
- [ ] **Step 2: Run test to verify failure**
- [ ] **Step 3: Implement exchange rate conversion**
  - In `report_expense_service.c`: query rows with `amount` and `currency`, convert each row via `exchange_rate_get_to_cny` (or target base currency) before aggregating into totals, category breakdowns, and daily breakdowns.
  - In `portfolio_repo_get_total_liabilities`: query liabilities with their currency, and convert each liability to base currency (CNY) rather than raw `SUM(current_value)`.
- [ ] **Step 4: Run test to verify it passes**
  Run: `./backend/tests/test_link.sh`
- [ ] **Step 5: Commit changes**
  Commit message: `fix(reports): convert foreign currencies in expense and liability aggregations`

---

### Task 8: Unify Multi-Ledger Isolation in Asset & Portfolio Reports (Item 10)

**Files:**
- Modify: `backend/src/interfaces/http/controllers/portfolio_controller.c`
- Modify: `backend/src/application/portfolio/commands.h`
- Modify: `backend/src/application/portfolio/usecases.c`
- Modify: `backend/src/infrastructure/repositories/portfolio_repo_impl.c`
- Modify: `backend/src/services/report_asset_service.c`
- Test: `backend/tests/test_ledgers.sh`

- [ ] **Step 1: Write test verifying assets and dashboard summary respect `ledger_id` filter**
- [ ] **Step 2: Run test to verify failure**
- [ ] **Step 3: Add `ledger_id` filtering to asset, holdings, and dashboard queries**
  - Extract `ledger_id` from `ctx_ledger_id(c, user_id, "viewer")`.
  - Pass `ledger_id` into repository functions (`portfolio_repo_get_holdings`, `portfolio_repo_get_category_assets`, `portfolio_repo_get_total_liabilities`, `report_asset_breakdown`, `report_multi_currency_summary`).
  - When `ledger_id > 0`, filter by `a.ledger_id = ?`.
- [ ] **Step 4: Run test to verify it passes**
  Run: `./backend/tests/test_ledgers.sh`
- [ ] **Step 5: Commit changes**
  Commit message: `fix(ledger): enforce ledger_id isolation across portfolio and asset summaries`

---

### Task 9: Reconstruct 30-Day Net Worth Trend Using Balance Logs (Item 2)

**Files:**
- Modify: `backend/src/infrastructure/repositories/portfolio_repo_impl.c`
- Modify: `backend/src/services/report_asset_service.c`
- Test: `backend/tests/test_link.sh`

- [ ] **Step 1: Write test verifying historical trend reflects balance changes over time**
- [ ] **Step 2: Run test to verify failure**
- [ ] **Step 3: Update trend calculations to reconstruct historical balance from `asset_balance_logs`**
  Instead of evaluating `a.created_at <= days.d` with today's `current_value`, calculate historical balance by subtracting deltas that occurred after that day (`balance_on_day = current_value - SUM(delta where created_at > day)`).
- [ ] **Step 4: Run test to verify it passes**
  Run: `cmake --build backend/build && ./backend/tests/test_link.sh`
- [ ] **Step 5: Commit changes**
  Commit message: `fix(portfolio): reconstruct historical net worth trend from balance delta logs`

---

### Task 10: Fix Frontend Currency Display in Holdings & Mobile Views (Item 6)

**Files:**
- Modify: `frontend/src/views/Holdings.vue`
- Modify: `frontend/src/views-mobile/HoldingsMobile.vue`
- Test: `frontend/test/` (Vitest)

- [ ] **Step 1: Run frontend tests to establish clean baseline**
  Run: `npm --prefix frontend test`
- [ ] **Step 2: Pass currency parameters to `formatCurrency` and `formatSigned`**
  - In `Holdings.vue`:
    - `formatCurrency(row.cost_basis, row.currency)`
    - `formatCurrency(row.current_value, row.currency)`
    - `formatSigned(row.floating_pnl, row.currency)`
    - `formatSigned(row.realized_pnl, row.currency)`
  - In `HoldingsMobile.vue`:
    - `formatSigned(h.floating_pnl, h.currency)`
- [ ] **Step 3: Run frontend type-check and tests**
  Run: `npm --prefix frontend run build && npm --prefix frontend test`
- [ ] **Step 4: Commit changes**
  Commit message: `fix(frontend): display correct currency symbols in desktop and mobile holdings views`
