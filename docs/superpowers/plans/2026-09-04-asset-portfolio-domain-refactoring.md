# Minefolio P1-02: Asset / Portfolio Domain 重构实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 重构 Asset / Portfolio 领域模型，明确分离 Asset, Account, Position, CostBasis, Valuation, PnL 与 Portfolio，实现 Ledger 到 Position 的单向派生真实来源，支持显式多币种汇率折算与投资组合集中度风险度量，并建立 5 大纯 C 领域测试套件。

**Architecture:**
- Domain 实体与值对象（`domain/asset/` 和 `domain/portfolio/`）仅依赖基础金融类型（`core/financial/`），完全不依赖 SQLite/PostgreSQL、HTTP 或 JSON 框架。
- Position 作为 Ledger 交易事件流的纯物化投影（Projection），不可直接修改。
- Portfolio 聚合明确区分原生币种与报告币种，通过显式 `mf_fx_rate_table_t` 进行转换，严格拦截跨币种隐式 1:1 相加。
- 各层严格遵循 DDD 架构，向后兼容现有 REST 接口与数据库存储结构。

**Tech Stack:** C23, CMake, CTest, Csilk, SQLite3 / PostgreSQL.

---

### Task 1: CostBasis & PnL 值对象与纯领域规则（带 CTest 单元测试）

**Files:**
- Create: `backend/src/domain/asset/cost_basis.h`
- Create: `backend/src/domain/asset/cost_basis.c`
- Create: `backend/src/domain/asset/pnl.h`
- Create: `backend/src/domain/asset/pnl.c`
- Create: `backend/tests/unit/test_domain_cost_basis.c`
- Create: `backend/tests/unit/test_domain_pnl.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: 编写失败的 CostBasis 单元测试**

创建 `backend/tests/unit/test_domain_cost_basis.c`：
```c
#include "domain/asset/cost_basis.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

void test_cost_basis_buy_and_average() {
    mf_cost_basis_t cb = mf_cost_basis_init(CURRENCY_CNY);
    money_t realized = money_zero(CURRENCY_CNY);

    // Buy 100 shares @ 10 CNY, fee 5 CNY -> total cost 1005 CNY, avg cost 10.05
    quantity_t q1 = quantity_from_int(100);
    money_t amt1 = money_from_int(1000, CURRENCY_CNY);
    money_t fee1 = money_from_int(5, CURRENCY_CNY);
    assert(mf_cost_basis_apply_buy(&cb, q1, amt1, fee1) == 0);

    assert(quantity_to_double(cb.quantity) == 100.0);
    assert(money_to_double(cb.total_cost) == 1005.0);
    assert(fabs(price_to_double(cb.average_cost) - 10.05) < 1e-4);

    // Buy another 100 shares @ 20 CNY, fee 5 CNY -> total cost 1005 + 2005 = 3010 CNY, avg cost 3010/200 = 15.05
    quantity_t q2 = quantity_from_int(100);
    money_t amt2 = money_from_int(2000, CURRENCY_CNY);
    money_t fee2 = money_from_int(5, CURRENCY_CNY);
    assert(mf_cost_basis_apply_buy(&cb, q2, amt2, fee2) == 0);

    assert(quantity_to_double(cb.quantity) == 200.0);
    assert(money_to_double(cb.total_cost) == 3010.0);
    assert(fabs(price_to_double(cb.average_cost) - 15.05) < 1e-4);
    printf("PASS: test_cost_basis_buy_and_average\n");
}

void test_cost_basis_sell_proportional() {
    mf_cost_basis_t cb = mf_cost_basis_init(CURRENCY_CNY);
    money_t realized = money_zero(CURRENCY_CNY);

    // Buy 200 shares @ 10 CNY (cost 2000)
    assert(mf_cost_basis_apply_buy(&cb, quantity_from_int(200), money_from_int(2000, CURRENCY_CNY), money_zero(CURRENCY_CNY)) == 0);

    // Sell 50 shares @ 15 CNY (amount 750, fee 10) -> proceeds 740, cost deducted 500, realized = 740 - 500 = 240
    money_t sell_amt = money_from_int(750, CURRENCY_CNY);
    money_t sell_fee = money_from_int(10, CURRENCY_CNY);
    assert(mf_cost_basis_apply_sell(&cb, &realized, quantity_from_int(50), sell_amt, sell_fee) == 0);

    assert(quantity_to_double(cb.quantity) == 150.0);
    assert(money_to_double(cb.total_cost) == 1500.0);
    assert(money_to_double(realized) == 240.0);

    // Oversell defense: sell 200 shares when only 150 available -> must return error -1
    assert(mf_cost_basis_apply_sell(&cb, &realized, quantity_from_int(200), money_from_int(3000, CURRENCY_CNY), money_zero(CURRENCY_CNY)) == -1);

    // Sell remaining 150 shares -> total cost should reset to 0
    assert(mf_cost_basis_apply_sell(&cb, &realized, quantity_from_int(150), money_from_int(1500, CURRENCY_CNY), money_zero(CURRENCY_CNY)) == 0);
    assert(quantity_to_double(cb.quantity) == 0.0);
    assert(money_to_double(cb.total_cost) == 0.0);
    assert(price_to_double(cb.average_cost) == 0.0);

    printf("PASS: test_cost_basis_sell_proportional\n");
}

void test_cost_basis_dividend() {
    mf_cost_basis_t cb = mf_cost_basis_init(CURRENCY_CNY);
    money_t realized = money_zero(CURRENCY_CNY);

    assert(mf_cost_basis_apply_buy(&cb, quantity_from_int(100), money_from_int(1000, CURRENCY_CNY), money_zero(CURRENCY_CNY)) == 0);

    // Dividend 100 CNY -> realized +100, total_cost_pnl reduced to 900
    assert(mf_cost_basis_apply_dividend(&cb, &realized, money_from_int(100, CURRENCY_CNY)) == 0);
    assert(money_to_double(realized) == 100.0);
    assert(money_to_double(cb.total_cost_pnl) == 900.0);
    // Holding quantity & accounting total_cost remain unchanged
    assert(quantity_to_double(cb.quantity) == 100.0);
    assert(money_to_double(cb.total_cost) == 1000.0);

    printf("PASS: test_cost_basis_dividend\n");
}

int main() {
    test_cost_basis_buy_and_average();
    test_cost_basis_sell_proportional();
    test_cost_basis_dividend();
    printf("All domain cost basis tests passed!\n");
    return 0;
}
```

- [ ] **Step 2: 编写失败的 PnL 单元测试**

创建 `backend/tests/unit/test_domain_pnl.c`：
```c
#include "domain/asset/pnl.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

void test_pnl_unrealized_and_total() {
    money_t cost = money_from_int(1000, CURRENCY_CNY);
    money_t market = money_from_int(1500, CURRENCY_CNY);
    money_t realized = money_from_int(200, CURRENCY_CNY);

    mf_pnl_t pnl = mf_pnl_calculate(cost, market, realized);
    assert(money_to_double(pnl.unrealized_pnl) == 500.0);
    assert(money_to_double(pnl.realized_pnl) == 200.0);
    assert(money_to_double(pnl.total_pnl) == 700.0);
    assert(fabs(pnl.unrealized_pct - 50.0) < 1e-4);
    assert(fabs(pnl.total_return_pct - 70.0) < 1e-4);

    printf("PASS: test_pnl_unrealized_and_total\n");
}

int main() {
    test_pnl_unrealized_and_total();
    printf("All domain pnl tests passed!\n");
    return 0;
}
```

- [ ] **Step 3: 注册 CTest 测试目标至 CMakeLists.txt 并验证失败**

在 `backend/CMakeLists.txt` 中添加目标：
```cmake
add_executable(test_domain_cost_basis tests/unit/test_domain_cost_basis.c src/domain/asset/cost_basis.c)
target_include_directories(test_domain_cost_basis PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_cost_basis PRIVATE -UNDEBUG)
target_link_libraries(test_domain_cost_basis PRIVATE m)
add_test(NAME test_domain_cost_basis COMMAND test_domain_cost_basis)

add_executable(test_domain_pnl tests/unit/test_domain_pnl.c src/domain/asset/pnl.c)
target_include_directories(test_domain_pnl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_pnl PRIVATE -UNDEBUG)
target_link_libraries(test_domain_pnl PRIVATE m)
add_test(NAME test_domain_pnl COMMAND test_domain_pnl)
```
运行构建确认头文件缺失报错。

- [ ] **Step 4: 实现 `cost_basis.h/.c` 与 `pnl.h/.c`**

编写 `backend/src/domain/asset/cost_basis.h` 与 `backend/src/domain/asset/cost_basis.c`：
实现 `mf_cost_basis_init`, `mf_cost_basis_apply_buy`, `mf_cost_basis_apply_sell`, `mf_cost_basis_apply_dividend`。

编写 `backend/src/domain/asset/pnl.h` 与 `backend/src/domain/asset/pnl.c`：
实现 `mf_pnl_calculate`。

- [ ] **Step 5: 运行并验证单元测试通过**

运行：`cmake --build build --target test_domain_cost_basis test_domain_pnl && ./build/test_domain_cost_basis && ./build/test_domain_pnl`
预期：PASS

- [ ] **Step 6: Commit**

```bash
git add backend/src/domain/asset/cost_basis.* backend/src/domain/asset/pnl.* backend/tests/unit/test_domain_cost_basis.c backend/tests/unit/test_domain_pnl.c backend/CMakeLists.txt
git commit -m "feat(domain): ✨ implement CostBasis and PnL value objects and unit tests"
```

---

### Task 2: Valuation, Instrument Asset, Account 与 Position 单向派生（带 CTest 单元测试）

**Files:**
- Create: `backend/src/domain/asset/instrument.h`
- Create: `backend/src/domain/asset/account.h`
- Create: `backend/src/domain/asset/valuation.h`
- Create: `backend/src/domain/asset/position.h`
- Create: `backend/src/domain/asset/position.c`
- Create: `backend/tests/unit/test_domain_position.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: 编写失败的 Position 单元测试**

创建 `backend/tests/unit/test_domain_position.c`：
测试 `mf_position_derive_from_ledger`：
- 构造历史流水：买入 100 股 @ 10 元，卖出 40 股 @ 15 元，分红 50 元。
- 输入最新市价 18 元。
- 断言：
  - `quantity == 60`
  - `cost_basis.total_cost == 600`
  - `valuation.market_value == 1080`
  - `pnl.unrealized_pnl == 480`
  - `pnl.realized_pnl == 250` (平仓收益 200 + 分红 50)
  - 验证超卖交易被拒绝并报错。

- [ ] **Step 2: 注册 CTest 测试目标至 CMakeLists.txt 并验证失败**

```cmake
add_executable(test_domain_position tests/unit/test_domain_position.c src/domain/asset/position.c src/domain/asset/cost_basis.c src/domain/asset/pnl.c src/core/ledger/ledger_types.c)
target_include_directories(test_domain_position PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_position PRIVATE -UNDEBUG)
target_link_libraries(test_domain_position PRIVATE m)
add_test(NAME test_domain_position COMMAND test_domain_position)
```

- [ ] **Step 3: 实现 `instrument.h`, `account.h`, `valuation.h`, `position.h`, `position.c`**

- `instrument.h`：定义 `mf_instrument_asset_t`（纯标的属性：symbol, name, asset_type, native_currency, quote_source）。
- `account.h`：定义 `mf_account_t`（账户属性：account_no, account_type, currency, balance, is_liability）。
- `valuation.h`：定义 `mf_valuation_t` 与 `mf_valuation_calculate`。
- `position.h` & `position.c`：定义 `mf_position_t` 与 `mf_position_derive_from_ledger`。

- [ ] **Step 4: 运行并验证单元测试通过**

运行：`cmake --build build --target test_domain_position && ./build/test_domain_position`
预期：PASS

- [ ] **Step 5: Commit**

```bash
git add backend/src/domain/asset/instrument.h backend/src/domain/asset/account.h backend/src/domain/asset/valuation.h backend/src/domain/asset/position.* backend/tests/unit/test_domain_position.c backend/CMakeLists.txt
git commit -m "feat(domain): ✨ implement Instrument, Account, Position and ledger derivation"
```

---

### Task 3: 显式多币种 FX 汇率换算表（带 CTest 单元测试）

**Files:**
- Create: `backend/src/domain/portfolio/fx_table.h`
- Create: `backend/src/domain/portfolio/fx_table.c`
- Create: `backend/tests/unit/test_domain_multi_currency.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: 编写失败的 Multi-Currency 单元测试**

创建 `backend/tests/unit/test_domain_multi_currency.c`：
- 同币种折算恒等（USD -> USD 保持精确相等）。
- 异币种显式汇率转换（USD -> CNY @ 7.2000，100 USD -> 720 CNY）。
- 汇率对逆向换算（CNY -> USD @ 7.2000，720 CNY -> 100 USD）。
- **缺失汇率时拒绝隐式 1:1 折算**：EUR -> CNY 若无汇率，返回 `-1` 且不修改输出金额。

- [ ] **Step 2: 注册 CTest 测试目标至 CMakeLists.txt 并验证失败**

```cmake
add_executable(test_domain_multi_currency tests/unit/test_domain_multi_currency.c src/domain/portfolio/fx_table.c)
target_include_directories(test_domain_multi_currency PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_multi_currency PRIVATE -UNDEBUG)
target_link_libraries(test_domain_multi_currency PRIVATE m)
add_test(NAME test_domain_multi_currency COMMAND test_domain_multi_currency)
```

- [ ] **Step 3: 实现 `fx_table.h` 与 `fx_table.c`**

实现：
- `mf_fx_rate_table_init`
- `mf_fx_rate_table_add`
- `mf_fx_convert_money`（严格防守，杜绝隐式折算）

- [ ] **Step 4: 运行并验证单元测试通过**

运行：`cmake --build build --target test_domain_multi_currency && ./build/test_domain_multi_currency`
预期：PASS

- [ ] **Step 5: Commit**

```bash
git add backend/src/domain/portfolio/fx_table.* backend/tests/unit/test_domain_multi_currency.c backend/CMakeLists.txt
git commit -m "feat(domain): ✨ implement explicit FX table and multi-currency conversion rules"
```

---

### Task 4: Portfolio 纯组合聚合、资产配置与集中度风险度量（带 CTest 单元测试）

**Files:**
- Create: `backend/src/domain/portfolio/portfolio.h`
- Create: `backend/src/domain/portfolio/portfolio.c`
- Create: `backend/tests/unit/test_domain_portfolio.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: 编写失败的 Portfolio 单元测试**

创建 `backend/tests/unit/test_domain_portfolio.c`：
- 构建跨币种多 Position（如 Position 1: 100 股 AAPL, 市值 1500 USD；Position 2: 200 股 茅台, 市值 1440 CNY）。
- 设置汇率表 USD/CNY = 7.2。
- 报告货币设置为 CNY：
  - AAPL 市值折算为 1500 * 7.2 = 10800 CNY
  - 茅台市值 1440 CNY
  - 组合总市值 = 12240 CNY
- 资产配置权重计算：
  - AAPL 占比 = 10800 / 12240 ≈ 88.24%
  - 茅台占比 = 1440 / 12240 ≈ 11.76%
- 风险集中度度量断言：
  - `max_holding_weight ≈ 0.8824`
  - `max_holding_asset_id == AAPL`
  - `herfindahl_index ≈ 0.8824^2 + 0.1176^2 ≈ 0.7924`
- 验证：缺少任一持仓的原生币种汇率时，聚合返回错误并拒绝产出脏数据。

- [ ] **Step 2: 注册 CTest 测试目标至 CMakeLists.txt 并验证失败**

```cmake
add_executable(test_domain_portfolio tests/unit/test_domain_portfolio.c src/domain/portfolio/portfolio.c src/domain/portfolio/fx_table.c src/domain/asset/cost_basis.c src/domain/asset/pnl.c)
target_include_directories(test_domain_portfolio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_portfolio PRIVATE -UNDEBUG)
target_link_libraries(test_domain_portfolio PRIVATE m)
add_test(NAME test_domain_portfolio COMMAND test_domain_portfolio)
```

- [ ] **Step 3: 实现 `portfolio.h` 与 `portfolio.c`**

实现 `mf_portfolio_aggregate`：
- 多币种显式折算总市值、总成本、总未实现盈亏、总已实现盈亏与收益率。
- 计算各项持仓权重占比。
- 计算最大单一重仓占比与赫芬达尔指数（HHI）。

- [ ] **Step 4: 运行并验证单元测试通过**

运行：`cmake --build build --target test_domain_portfolio && ./build/test_domain_portfolio`
预期：PASS

- [ ] **Step 5: Commit**

```bash
git add backend/src/domain/portfolio/portfolio.* backend/tests/unit/test_domain_portfolio.c backend/CMakeLists.txt
git commit -m "feat(domain): ✨ implement Portfolio aggregation, allocation, and risk metrics"
```

---

### Task 5: 适配应用层与仓储投影，确保系统向下兼容

**Files:**
- Modify: `backend/src/domain/asset/entity.h`
- Modify: `backend/src/domain/portfolio/entity.h`
- Modify: `backend/src/application/asset/usecases.c`
- Modify: `backend/src/application/portfolio/usecases.c`

- [ ] **Step 1: 在 `domain/asset/entity.h` 中引入新领域对象并提供过渡投影适配**

保持原 `mf_asset_t` 作为综合视图模型，其内部委托给 `mf_instrument_asset_t`, `mf_account_t`, `mf_position_t`。

- [ ] **Step 2: 在 `application/portfolio/usecases.c` 中接入显式汇率表与 `mf_portfolio_aggregate`**

在聚合持仓报表时，从市场汇率仓储读取汇率注入 `mf_fx_rate_table_t`，避免隐式汇率转换。

- [ ] **Step 3: 重新编译完整项目**

`cmake -B build -G "Unix Makefiles" && cmake --build build --parallel`
确保编译器 0 警告 0 错误。

- [ ] **Step 4: Commit**

```bash
git add backend/src/domain/asset/entity.h backend/src/domain/portfolio/entity.h backend/src/application/asset/usecases.c backend/src/application/portfolio/usecases.c
git commit -m "refactor(application): ♻️ adapt asset and portfolio use cases to clean domain models"
```

---

### Task 6: 全量质量保证与回归验证

- [ ] **Step 1: 运行全量 25 个 CTest 单元测试套件**

运行：`ctest --test-dir build --output-on-failure`
预期：100% PASS (25/25)

- [ ] **Step 2: 运行 7 大端到端集成测试套件**

```bash
./tests/test_link.sh
./tests/test_ledgers.sh
./tests/test_2fa.sh
./tests/test_dca_cashflow.sh
./tests/test_ai_trace.sh
./tests/test_market_sync.sh
./tests/test_fx_oauth.sh
```
预期：所有断言全部 PASS，零回归。

- [ ] **Step 3: 前端测试与生产构建验证**

```bash
npm --prefix frontend test
npm --prefix frontend run build
```
预期：0 错误，SPA 打包成功。

- [ ] **Step 4: 最终提交与总结**

```bash
git commit --allow-empty -m "test(all): ✅ complete Asset/Portfolio domain refactoring verification"
```
