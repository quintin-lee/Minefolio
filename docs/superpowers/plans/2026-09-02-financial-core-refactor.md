# Minefolio P0-01: 重构 Financial Core 金融数值系统实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建独立、精确、类型安全的 Financial Core 金融数值引擎（`core/financial/`），彻底消除核心业务逻辑与模型中直接使用 `double` 导致的浮点舍入与精度损失问题。

**Architecture:** 基于 128 位定点数（`__int128_t` mantissa + `int32_t` scale）构建 `decimal_t` 底层运算引擎，并上层构建强类型领域模型：`currency_t`（币种与精度）、`money_t`（币种绑定金额与同币种运算校验）、`quantity_t`（份额持仓）、`price_t`（单价）、`rate_t`（汇率与三角换算）、`percentage_t`（百分比与涨跌率）。随后逐层迁移 `models`、`common/balance`、`common/tx_types`、`repositories` 与 `services`。

**Tech Stack:** C23 (GCC 14 / Clang 18), `__int128_t`, CTest / Standalone C Unit Test Runner, SQLite 3, csilk HTTP framework.

---

## 目录结构规划

```text
backend/src/core/financial/
├── currency.h / currency.c     # ISO 4217 币种与小数位精度定义
├── decimal.h / decimal.c       # 128位高精度定点数引擎（加减乘除、舍入模式、格式化）
├── money.h / money.c           # 货币金额强类型（币种校验、加减比对）
├── quantity.h / quantity.c     # 标的资产份额与数量类型
├── price.h / price.c           # 标的成交单价与量价转换
├── rate.h / rate.c             # 外汇汇率、费率与三角链式折算
└── percentage.h / percentage.c # 百分比率、收益率与权重占比

backend/tests/unit/
├── test_decimal.c              # 定点数算术、舍入与极值单元测试
├── test_money.c                # 货币运算与币种不一致防范测试
├── test_quantity.c             # 份额数量运算测试
├── test_price.c                # 量价乘积与除法转换测试
├── test_rate.c                 # 外汇折算与链式汇率测试
└── test_pnl.c                  # 持仓均价、双因子汇兑损益精确核算测试
```

---

## Task 1: 扫描并建立全项目 double 金融字段清单

**Files:**
- Create: `docs/financial-double-inventory.md`

- [ ] **Step 1: 全面扫描 backend/src 中的金融字段**
  统计所有 `amount`, `balance`, `current_value`, `cost_basis`, `net_value`, `price`, `quantity`, `fee`, `tax`, `pnl`, `rate`, `percentage` 字段在 models, common, repositories, services, controllers 中的使用。
- [ ] **Step 2: 输出清单文档 `docs/financial-double-inventory.md`**
  详细记录文件路径、结构体/变量名、原类型 (`double`)、拟替换类型（`money_t`, `quantity_t`, `price_t`, `rate_t`, `percentage_t`）及迁移批次。
- [ ] **Step 3: 提交清单文档**
  `docs: 📝 add comprehensive double financial field inventory`

---

## Task 2: 实现 `currency.h` 与 `currency.c`

**Files:**
- Create: `backend/src/core/financial/currency.h`
- Create: `backend/src/core/financial/currency.c`
- Create: `backend/tests/unit/test_currency.c`

- [ ] **Step 1: 编写 Currency 单元测试**
  测试常用币种（CNY, USD, EUR, HKD, JPY, BTC, ETH）解析、精度判断（JPY=0, CNY=2, BTC=8）、相等性比较与未知币种校验。
- [ ] **Step 2: 实现 `currency.h` 与 `currency.c`**
  定义 `currency_t`，提供 `currency_from_str`, `currency_code`, `currency_equals`, `currency_is_valid`, `currency_precision`。
- [ ] **Step 3: 运行测试并验证**
- [ ] **Step 4: 提交代码**
  `feat(core): ✨ implement currency module in financial core`

---

## Task 3: 实现 `decimal.h` 与 `decimal.c` 高精度定点数引擎

**Files:**
- Create: `backend/src/core/financial/decimal.h`
- Create: `backend/src/core/financial/decimal.c`
- Create: `backend/tests/unit/test_decimal.c`

- [ ] **Step 1: 编写 Decimal 单元测试**
  覆盖：
  * 加法、减法、乘法、除法（零除错误、溢出错误检测）
  * 舍入模式：`ROUND_HALF_UP`（四舍五入）、`ROUND_HALF_EVEN`（银行家舍入）、`ROUND_DOWN`、`ROUND_UP`、`ROUND_CEIL`、`ROUND_FLOOR`
  * 极值测试（$10^{18}$ 级大额、18 位微小值）、负数、零值判定
  * 字符串转换与解析（`"12345.67"` <-> `decimal_t`，去除浮点噪声）
- [ ] **Step 2: 实现 `decimal.h` 与 `decimal.c`**
  使用 `__int128_t` 作为内部 mantissa，支持 0-18 动态 scale 与精确溢出校验。
- [ ] **Step 3: 运行 `test_decimal` 单元测试并通过**
- [ ] **Step 4: 提交代码**
  `feat(core): ✨ implement 128-bit fixed-point decimal arithmetic engine`

---

## Task 4: 实现 `money.h` 与 `money.c` 货币金额模型

**Files:**
- Create: `backend/src/core/financial/money.h`
- Create: `backend/src/core/financial/money.c`
- Create: `backend/tests/unit/test_money.c`

- [ ] **Step 1: 编写 Money 单元测试**
  测试同币种加减、异币种相加报错 (`DECIMAL_ERR_CURRENCY_MISMATCH`)、大小比对、正负判定、货币规格化舍入（如 CNY 舍入到 2 位小数，JPY 舍入到整数）。
- [ ] **Step 2: 实现 `money.h` 与 `money.c`**
  定义 `money_t` 结构体，封装 `money_add`, `money_sub`, `money_cmp`, `money_round`, `money_to_string`。
- [ ] **Step 3: 运行 `test_money` 单元测试并通过**
- [ ] **Step 4: 提交代码**
  `feat(core): ✨ implement currency-bound money model with safety validations`

---

## Task 5: 实现 `quantity.h`, `price.h`, `rate.h`, `percentage.h`

**Files:**
- Create: `backend/src/core/financial/quantity.h` & `quantity.c`
- Create: `backend/src/core/financial/price.h` & `price.c`
- Create: `backend/src/core/financial/rate.h` & `rate.c`
- Create: `backend/src/core/financial/percentage.h` & `percentage.c`
- Create: `backend/tests/unit/test_quantity.c`, `test_price.c`, `test_rate.c`

- [ ] **Step 1: 编写 Quantity & Price 单元测试**
  测试量价相乘得金额 (`price * quantity = money`)、金额除以数量得单价 (`money / quantity = price`)、金额除以单价得数量 (`money / price = quantity`)。
- [ ] **Step 2: 编写 Rate & Percentage 单元测试**
  测试外汇汇率换算 (`money * rate = converted_money`)、三角套汇链式合成 (`rate_chain`)、百分比计算 (`part / whole * 100 = percentage`) 及费率扣减。
- [ ] **Step 3: 实现四大领域类型及其互相转换方法**
- [ ] **Step 4: 运行单元测试并通过**
- [ ] **Step 5: 提交代码**
  `feat(core): ✨ implement quantity, price, rate, and percentage domain models`

---

## Task 6: 编写 `test_pnl.c` 与 `test_fx.c` 投资及外汇损益精确核算测试

**Files:**
- Create: `backend/tests/unit/test_pnl.c`
- Create: `backend/tests/unit/test_fx.c`

- [ ] **Step 1: 编写持仓成本均摊与分红 PnL 测试**
  以无浮点误差的高精度模型核算多次买入、部分卖出、手续费分摊与浮动盈亏。
- [ ] **Step 2: 编写外汇双因子损益核算测试**
  核算外币资产价格盈亏与纯汇率损益的双因子剥离。
- [ ] **Step 3: 编译并运行验证全部通过**
- [ ] **Step 4: 提交代码**
  `test(core): ✅ add comprehensive PnL and FX unit test suites`

---

## Task 7: 逐步迁移核心金融业务层 (`common/balance`, `common/tx_types`, `models`)

**Files:**
- Modify: `backend/src/common/balance.h` & `balance.c`
- Modify: `backend/src/common/tx_types.h` & `tx_types.c`
- Modify: `backend/src/common/db.h` & `db.c` (增加 `db_get_decimal`, `db_get_money`)
- Modify: `backend/src/models/*.h`

- [ ] **Step 1: 升级 `common/db.h` 支持 Decimal/Money 解析**
- [ ] **Step 2: 改造 `balance.c` / `balance.h` 中的 `balance_apply_delta` 与 `apply_position`**
  核心内部计算切换为 `money_t`, `quantity_t`, `price_t` 精确计算。
- [ ] **Step 3: 改造 `tx_types.c` / `tx_types.h`**
  `tx_delta` 与 `tx_effective_ldelta` 采用精确定点数。
- [ ] **Step 4: 执行单元测试与后端集成测试全套套件**
  `cmake --build backend/build --parallel && ./backend/tests/test_link.sh`
- [ ] **Step 5: 提交代码**
  `refactor(core): ♻️ migrate balance engine and tx_types to Financial Core`

---

## Task 8: 逐步迁移服务层与外汇报表 (`services/market`, `services/report_asset_service`)

**Files:**
- Modify: `backend/src/services/market/exchange_rate_service.c`
- Modify: `backend/src/services/report_asset_service.c`
- Modify: `backend/src/services/dca_service.c`
- Modify: `backend/src/services/cashflow_service.c`

- [ ] **Step 1: 重构外汇汇率换算引擎 `exchange_rate_service.c` 使用 `rate_t` 与 `decimal_t`**
- [ ] **Step 2: 重构外汇损益与多币种分桶汇总 `report_asset_service.c`**
- [ ] **Step 3: 重构定投 DCA 均摊核算与现金流推演**
- [ ] **Step 4: 运行 7 组全量集成测试套件验证**
- [ ] **Step 5: 提交代码**
  `refactor(services): ♻️ migrate market FX, DCA, and PnL reporting to Financial Core`

---

## Task 9: 验证、构建与输出总结报告

**Files:**
- Update: `backend/CMakeLists.txt`
- Create: `docs/financial-core-architecture.md`

- [ ] **Step 1: 将所有单元测试集成入 CMake (CTest 或 build targets)**
- [ ] **Step 2: 运行全栈构建与集成测试（零错误通过）**
- [ ] **Step 3: 输出任务结题报告（包含架构设计、double 字段清单、迁移情况、API 兼容性与剩余技术债务）**
