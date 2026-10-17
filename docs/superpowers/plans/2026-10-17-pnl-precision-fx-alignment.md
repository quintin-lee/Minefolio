# PnL Alignment, Precision Duality & FX Semantics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reconcile dual PnL calculation algorithms (R1), eliminate precision duality between write and report paths (R2), and unify currency exchange rate semantics (R3) across Minefolio's financial core, domain rules, and reporting services.

**Architecture:** Consolidate portfolio trade-event replay and realized PnL logic into the Domain layer (`domain/portfolio/rules.c`) using the fixed-point financial core (`core/financial/`). Replace the floating-point `asset_perf_tracker_t[256]` in `portfolio_usecase_get_performance` with fixed-point calculations and unified fee accounting. Standardize exchange rate lookups with explicit fallback warnings/errors and deterministic DTO rounding.

**Tech Stack:** C23, CMake, CTest, csilk HTTP/JSON framework, fixed-point financial core (`money_t`, `decimal_t`, `quantity_t`, `price_t`, `rate_t`), SQLite/PostgreSQL.

---

## File Structure & Responsibilities

- **`backend/src/services/market/exchange_rate_service.h/.c`**: Exchange rate service. Adds `exchange_rate_get_to_cny_safe()` for strict currency validation and adds explicit warning logs on unknown currency fallbacks.
- **`backend/src/domain/portfolio/rules.h/.c`**: Domain rules for portfolio holdings and performance. Unifies trade event replay, capital gains (pure price spread), fee accounting, and fixed-point aggregation.
- **`backend/src/infrastructure/repositories/portfolio_repo_impl.c`**: Data access for trade events and holdings. Aligns transaction queries to filter fee children consistently (`parent_tx_id IS NULL OR parent_tx_id = 0`).
- **`backend/src/application/portfolio/usecases.c`**: Application orchestrator for `/api/reports/holdings` and `/api/reports/transaction/performance`. Eliminates the `trackers[256]` float array and IEEE double arithmetic, delegating calculation to the domain rules and financial core.
- **`backend/tests/unit/test_fx_semantics.c`**: Unit test suite for currency lookup safety, warning semantics, and rate validation.
- **`backend/tests/unit/test_portfolio_pnl_alignment.c`**: Unit test suite verifying that holdings and performance produce identical realized PnL and cost basis for identical transaction histories.

---

### Task 1: Unify Currency FX Rate Lookup Semantics & Unknown Fallback Warnings (R3)

**Files:**
- Modify: `backend/src/services/market/exchange_rate_service.h:18-35`
- Modify: `backend/src/services/market/exchange_rate_service.c:54-95`
- Create: `backend/tests/unit/test_fx_semantics.c`
- Modify: `backend/CMakeLists.txt:270-320`

- [ ] **Step 1: Write the failing unit test for exchange rate semantics**

Create `backend/tests/unit/test_fx_semantics.c`:
```c
#include "services/market/exchange_rate_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    // 1. Known currencies must succeed with code 0
    double rate = 0.0;
    int rc = exchange_rate_get_to_cny_safe("CNY", &rate);
    assert(rc == 0 && rate == 1.0);

    rc = exchange_rate_get_to_cny_safe("USD", &rate);
    assert(rc == 0 && rate > 0.0);

    rc = exchange_rate_get_to_cny_safe("HKD", &rate);
    assert(rc == 0 && rate > 0.0);

    // 2. Unknown currency must return -1 in safe mode
    rc = exchange_rate_get_to_cny_safe("XYZ_UNKNOWN", &rate);
    assert(rc == -1);

    // 3. NULL or empty currency must return -1 in safe mode
    rc = exchange_rate_get_to_cny_safe(NULL, &rate);
    assert(rc == -1);
    rc = exchange_rate_get_to_cny_safe("", &rate);
    assert(rc == -1);

    // 4. Legacy fallback function still returns 1.0 for backwards compatibility
    double fallback_rate = exchange_rate_get_to_cny("XYZ_UNKNOWN");
    assert(fallback_rate == 1.0);

    printf("PASS: test_fx_semantics\n");
    return 0;
}
```

- [ ] **Step 2: Register test in CMakeLists.txt and verify it fails**

In `backend/CMakeLists.txt`, add:
```cmake
add_executable(test_fx_semantics tests/unit/test_fx_semantics.c)
target_link_libraries(test_fx_semantics PRIVATE minefolio_common)
add_test(NAME test_fx_semantics COMMAND test_fx_semantics)
```

Run:
```bash
cmake -B backend/build -G "Unix Makefiles" backend && cmake --build backend/build --target test_fx_semantics
```
Expected: FAIL with "implicit declaration of function 'exchange_rate_get_to_cny_safe'"

- [ ] **Step 3: Implement `exchange_rate_get_to_cny_safe` and unknown currency logging**

In `backend/src/services/market/exchange_rate_service.h`:
```c
/**
 * @brief 安全获取指定货币对 CNY 的汇率
 * @param currency 货币代码 (如 "USD", "HKD")
 * @param out_rate 输出汇率
 * @return 0 成功匹配已知汇率; -1 未知货币或参数非法
 */
int exchange_rate_get_to_cny_safe(const char* currency, double* out_rate);
```

In `backend/src/services/market/exchange_rate_service.c`:
```c
int
exchange_rate_get_to_cny_safe(const char* currency, double* out_rate)
{
    if (!out_rate) {
        return -1;
    }
    *out_rate = 1.0;
    if (!currency || !currency[0]) {
        return -1;
    }

    char cur_upper[16];
    size_t len = strlen(currency);
    if (len >= sizeof(cur_upper)) {
        len = sizeof(cur_upper) - 1;
    }
    for (size_t i = 0; i < len; i++) {
        cur_upper[i] = (char)toupper((unsigned char)currency[i]);
    }
    cur_upper[len] = '\0';

    if (strcmp(cur_upper, "CNY") == 0 || strcmp(cur_upper, "RMB") == 0) {
        *out_rate = 1.0;
        return 0;
    }

    pthread_mutex_lock(&g_rate_mutex);
    for (int i = 0; g_rates[i].currency[0] != '\0'; i++) {
        if (strcmp(g_rates[i].currency, cur_upper) == 0) {
            *out_rate = g_rates[i].rate_to_cny;
            pthread_mutex_unlock(&g_rate_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_rate_mutex);

    return -1;
}

double
exchange_rate_get_to_cny(const char* currency)
{
    double rate = 1.0;
    if (exchange_rate_get_to_cny_safe(currency, &rate) == 0) {
        return rate > 0.0 ? rate : 1.0;
    }
    if (currency && currency[0] && strcasecmp(currency, "CNY") != 0 && strcasecmp(currency, "RMB") != 0) {
        fprintf(stderr, "[WARN] Unknown currency '%s' requested in exchange_rate_get_to_cny; falling back to 1.0\n", currency);
    }
    return 1.0;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
cmake --build backend/build --target test_fx_semantics && ./backend/build/test_fx_semantics
```
Expected: PASS with "PASS: test_fx_semantics"

- [ ] **Step 5: Commit**

```bash
git add backend/src/services/market/exchange_rate_service.h backend/src/services/market/exchange_rate_service.c backend/tests/unit/test_fx_semantics.c backend/CMakeLists.txt
git commit -m "feat(market): ✨ add exchange_rate_get_to_cny_safe and warn on unknown currency fallback"
```

---

### Task 2: Align Repository Trade Event Queries to Exclude Fee Child Rows (R1)

**Files:**
- Modify: `backend/src/infrastructure/repositories/portfolio_repo_impl.c:120-155`

- [ ] **Step 1: Write test case verifying fee child row exclusion**

In `backend/tests/unit/test_domain_portfolio.c`, inspect or append a verification that trade events passed to portfolio domain rules must not contain duplicate fee child rows.

- [ ] **Step 2: Verify current trade event SQL missing parent_tx_id filter**

In `backend/src/infrastructure/repositories/portfolio_repo_impl.c:126-145`:
Notice `mf_portfolio_repo_get_trade_events` queries `transactions t` WITHOUT `(t.parent_tx_id IS NULL OR t.parent_tx_id = 0)`.
Compare this with `portfolio_repo_get_performance_transactions:505`, which explicitly has `AND (t.parent_tx_id IS NULL OR t.parent_tx_id = 0)`.

- [ ] **Step 3: Update `mf_portfolio_repo_get_trade_events` to filter `parent_tx_id`**

In `backend/src/infrastructure/repositories/portfolio_repo_impl.c:126-145`:
Replace SQL:
```c
    if (ledger_id > 0) {
        snprintf(tx_sql,
                 sizeof(tx_sql),
                 "SELECT t.id, t.asset_id, t.transaction_type, t.quantity, t.amount, "
                 "t.price_per_unit, t.fee, "
                 "t.currency "
                 "FROM transactions t JOIN assets a ON t.asset_id = a.id "
                 "WHERE t.user_id = ? AND (t.ledger_id = ? OR a.ledger_id = ?) "
                 "AND (t.parent_tx_id IS NULL OR t.parent_tx_id = 0) "
                 "ORDER BY t.transaction_date ASC, t.id ASC");
        params[pidx++] = lid_str;
        params[pidx++] = lid_str;
    } else {
        snprintf(tx_sql,
                 sizeof(tx_sql),
                 "SELECT t.id, t.asset_id, t.transaction_type, t.quantity, t.amount, "
                 "t.price_per_unit, t.fee, "
                 "t.currency "
                 "FROM transactions t JOIN assets a ON t.asset_id = a.id "
                 "WHERE t.user_id = ? "
                 "AND (t.parent_tx_id IS NULL OR t.parent_tx_id = 0) "
                 "ORDER BY t.transaction_date ASC, t.id ASC");
    }
```

- [ ] **Step 4: Run existing CTests to confirm no regression**

Run:
```bash
cmake --build backend/build --parallel && cd backend/build && ctest --output-on-failure && cd ../..
```
Expected: 100% tests passed.

- [ ] **Step 5: Commit**

```bash
git add backend/src/infrastructure/repositories/portfolio_repo_impl.c
git commit -m "fix(portfolio): 🐛 filter out fee child rows in portfolio trade events repository"
```

---

### Task 3: Unify Performance Calculation into Domain Trade Replay & Fixed-Point Core (R1 & R2)

**Files:**
- Modify: `backend/src/domain/portfolio/rules.h:12-25`
- Modify: `backend/src/domain/portfolio/rules.c:10-135`
- Modify: `backend/src/application/portfolio/usecases.c:106-250`
- Create: `backend/tests/unit/test_portfolio_pnl_alignment.c`
- Modify: `backend/CMakeLists.txt:270-320`

- [ ] **Step 1: Write the failing unit test for PnL alignment**

Create `backend/tests/unit/test_portfolio_pnl_alignment.c`:
```c
#include "domain/portfolio/rules.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

int main(void) {
    currency_t cny = currency_from_str("CNY");

    // Scenario: Buy 1000 @ 2.0 = 2000, fee 10; Sell 400 @ 3.0 = 1200, fee 8; Dividend 100
    mf_holding_item_t item = {0};
    item.asset_id = 200;
    item.currency = cny;
    quantity_from_double(600.0, 4, &item.quantity);
    price_from_double(2.5, 4, cny, &item.net_value);
    money_from_double(1200.0, cny, &item.cost_basis);

    mf_portfolio_trade_event_t events[3] = {0};
    // 1. Buy 1000 @ 2.0 = 2000, fee 10
    events[0].asset_id = 200;
    snprintf(events[0].type, sizeof(events[0].type), "buy");
    quantity_from_double(1000.0, 4, &events[0].quantity);
    price_from_double(2.0, 4, cny, &events[0].price);
    money_from_double(2000.0, cny, &events[0].amount);
    money_from_double(10.0, cny, &events[0].fee);

    // 2. Sell 400 @ 3.0 = 1200, fee 8
    events[1].asset_id = 200;
    snprintf(events[1].type, sizeof(events[1].type), "sell");
    quantity_from_double(400.0, 4, &events[1].quantity);
    price_from_double(3.0, 4, cny, &events[1].price);
    money_from_double(1200.0, cny, &events[1].amount);
    money_from_double(8.0, cny, &events[1].fee);

    // 3. Dividend 100
    events[2].asset_id = 200;
    snprintf(events[2].type, sizeof(events[2].type), "dividend");
    money_from_double(100.0, cny, &events[2].amount);

    int rc = mf_portfolio_rule_apply_trade_events(&item, 1, events, 3);
    assert(rc == 0);

    // Realized capital gain = (1200 - 400 * 2.0) + 100 = 500.0
    double realized = money_to_double(item.realized_pnl);
    assert(fabs(realized - 500.0) < 1e-4);

    // Cost basis remaining for 600 shares = 1200.0
    double cost = money_to_double(item.cost_basis);
    assert(fabs(cost - 1200.0) < 1e-4);

    printf("PASS: test_portfolio_pnl_alignment\n");
    return 0;
}
```

- [ ] **Step 2: Register test in CMakeLists.txt and verify build**

In `backend/CMakeLists.txt`:
```cmake
add_executable(test_portfolio_pnl_alignment tests/unit/test_portfolio_pnl_alignment.c)
target_link_libraries(test_portfolio_pnl_alignment PRIVATE minefolio_common)
add_test(NAME test_portfolio_pnl_alignment COMMAND test_portfolio_pnl_alignment)
```

Run:
```bash
cmake -B backend/build -G "Unix Makefiles" backend && cmake --build backend/build --target test_portfolio_pnl_alignment
```

- [ ] **Step 3: Refactor `portfolio_usecase_get_performance` to eliminate `trackers[256]` and use domain fixed-point replay**

In `backend/src/application/portfolio/usecases.c:130-240`:
Replace the fixed `asset_perf_tracker_t trackers[256]` and raw `double` calculations with dynamically allocated trackers or domain trade replay logic:
```c
    typedef struct {
        int64_t    asset_id;
        currency_t currency;
        quantity_t total_qty;
        money_t    cost_basis;
    } perf_tracker_t;

    size_t n = csilk_json_array_size(result);
    perf_tracker_t* trackers = (perf_tracker_t*)calloc(n > 0 ? n : 1, sizeof(perf_tracker_t));
    if (!trackers) {
        csilk_json_free(result);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "内存分配失败");
        return -1;
    }
    size_t tracker_count = 0;
```
For each row in `result`:
- Parse `amount`, `fee`, `quantity`, `price_per_unit` into `money_t`, `price_t`, `quantity_t` using `db_get_money`, `db_get_price`, `db_get_quantity`.
- On `buy`: `cost_basis += amount` (consistent with domain holding invariant where fee is recorded in independent fee ledger rows).
- On `sell`:
  `ratio = sell_qty / total_qty` (fixed-point `decimal_div`).
  `sold_cost = cost_basis * ratio`.
  `realized = sell_amount - sold_cost`.
  `cost_basis -= sold_cost; total_qty -= sell_qty;`
  If `total_qty <= 0`: reset `total_qty = 0; cost_basis = 0;`.
- On `income`/`dividend`/`interest`: `realized = amount`.
- Convert `realized` to base currency using `exchange_rate_convert_m(realized_m, base_cur)` rather than IEEE floating multiplication.
- Free `trackers` before returning.

- [ ] **Step 4: Run unit tests and verify they pass**

Run:
```bash
cmake --build backend/build --parallel && cd backend/build && ctest --output-on-failure && cd ../..
```
Expected: 100% tests passed.

- [ ] **Step 5: Commit**

```bash
git add backend/src/application/portfolio/usecases.c backend/tests/unit/test_portfolio_pnl_alignment.c backend/CMakeLists.txt
git commit -m "refactor(portfolio): ♻️ migrate performance calculation to fixed-point domain rules without static 256 array limit"
```

---

### Task 4: Rounding Normalization on Report JSON DTO Serialization (R2)

**Files:**
- Modify: `backend/src/application/portfolio/usecases.c:64-95, 230-260`
- Modify: `backend/src/services/report_asset_service.c:395-435`

- [ ] **Step 1: Write test verifying JSON double clean formatting**

Create/update unit test asserting that amounts serialized to JSON do not contain IEEE-754 trailing epsilon noise (e.g., `1250.0000000000002`).

- [ ] **Step 2: Add inline rounding helper for report DTO numbers**

In `backend/src/common/response.h` or `backend/src/common/db.h`:
```c
static inline double
mf_round_to_decimals(double val, int decimals)
{
    double factor = 1.0;
    for (int i = 0; i < decimals; i++) {
        factor *= 10.0;
    }
    return round(val * factor) / factor;
}
```

- [ ] **Step 3: Apply normalization to report DTO fields in `portfolio/usecases.c`**

Apply `mf_round_to_decimals(val, 2)` for fiat amounts and `mf_round_to_decimals(val, 4)` for quantities and percentages when appending numbers to JSON response objects in `portfolio_usecase_get_holdings` and `portfolio_usecase_get_performance`.

- [ ] **Step 4: Run all tests to verify clean serialization**

Run:
```bash
cmake --build backend/build --parallel && cd backend/build && ctest --output-on-failure && cd ../..
```
Expected: 100% tests passed.

- [ ] **Step 5: Commit**

```bash
git add backend/src/application/portfolio/usecases.c backend/src/common/db.h
git commit -m "style(portfolio): 🎨 apply deterministic decimal rounding to report JSON DTO fields"
```

---

### Task 5: Integration Regression Verification

**Files:**
- Test: `backend/tests/test_link.sh`

- [ ] **Step 1: Run complete integration test suite**

Run:
```bash
./backend/tests/test_link.sh
```
Verify that:
- Case 28 (买入建仓) PASS
- Case 29 (净值更新) PASS
- Case 30 (卖出+已实现盈亏) PASS
- Case 31 (浮动盈亏) PASS
- Case 32 (手续费) PASS
- Case 33 (持仓报表 H1-H6, I1-I3) PASS
- Case 38 (Ledger Engine 溯源重建) PASS
- Case 39 (历史趋势重溯) PASS

- [ ] **Step 2: Verify zero failures**

Expected output:
```text
结果: PASS=151 FAIL=0
```

- [ ] **Step 3: Final commit if any adjustments needed**

```bash
git status
```
Ensure working tree is clean.
