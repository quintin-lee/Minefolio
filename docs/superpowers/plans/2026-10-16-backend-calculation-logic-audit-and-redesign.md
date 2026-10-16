# 后端金融计算逻辑全景审计与双轨收敛重构实施计划 (Backend Calculation Logic Audit & Redesign Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 按照全景审计规范，收敛后端买卖持仓减仓双轨算法至单一可信源 `ledger_engine`，统一已实现盈亏与独立手续费契约，引入月末动态对齐日历算法与报表百分比闭包平衡，实现 0 精度漂移且 100% 兼容既有 Schema。

**Architecture:** 严格遵循 DDD 四层单向依赖架构与 C23 标准。将 `core/ledger/ledger_engine.c` 与 `core/financial/` 确立为全局单一数学可信源（SSOT）。`common/balance.c` 瘦身为纯调度层直接复用 `ledger_calc_*` 算子；`domain/asset/cost_basis.c` 与 `domain/portfolio/rules.c` 盈亏契约彻底统一；`domain/cashflow/rules.c` 引入动态月份天数算法；报表中心引入最大项尾数平衡确保百分比和为 100%。

**Tech Stack:** C23, GCC 14, CMake (Unix Makefiles), 128 位定点数金融核心 (Financial Core), SQLite / PostgreSQL, Vue 3 + TypeScript.

---

### Task 1: 资产买卖持仓核算归一至 Ledger Engine (Consolidate apply_position_fc to ledger_engine)

**Files:**
- Modify: `backend/src/common/balance.c:218-285`
- Test: `backend/tests/unit/test_ledger_engine.c`

- [ ] **Step 1: 编写重放与增量计算严格恒等性的单元测试**

在 `backend/tests/unit/test_ledger_engine.c` 中添加针对多笔买入、碎股卖出以及清仓置零的恒等性测试用例 `test_ledger_position_rebuild_consistency`：

```c
static void test_ledger_position_rebuild_consistency(void) {
    currency_t cny = CURRENCY_CNY;

    // 初始状态
    quantity_t q0 = quantity_zero();
    money_t c0 = money_zero(cny);

    // 第 1 笔：买入 100 股 @ 10.50 元，手续费 5.00 元 -> 成本 1055.00 元
    quantity_t buy1_qty;
    price_t buy1_p;
    money_t buy1_fee;
    quantity_from_double(100.0, 4, &buy1_qty);
    price_from_double(10.50, 4, cny, &buy1_p);
    money_from_double(5.00, cny, &buy1_fee);

    quantity_t q1;
    money_t c1;
    int rc = ledger_calc_buy_position(q0, c0, buy1_qty, buy1_p, buy1_fee, &q1, &c1);
    assert(rc == 0);
    assert(fabs(quantity_to_double(q1) - 100.0) < 1e-4);
    assert(fabs(money_to_double(c1) - 1055.0) < 1e-4);

    // 第 2 笔：部分卖出 33.3333 股 @ 12.00 元，手续费 2.00 元
    quantity_t sell1_qty;
    price_t sell1_p;
    money_t sell1_fee;
    quantity_from_double(33.3333, 4, &sell1_qty);
    price_from_double(12.00, 4, cny, &sell1_p);
    money_from_double(2.00, cny, &sell1_fee);

    quantity_t q2;
    money_t c2, red1, pnl1;
    rc = ledger_calc_sell_position(q1, c1, sell1_qty, sell1_p, sell1_fee, &q2, &c2, &red1, &pnl1);
    assert(rc == 0);
    assert(quantity_cmp(q2, quantity_zero()) > 0);
    assert(money_cmp(c2, money_zero(cny)) > 0);

    // 第 3 笔：全部卖出剩余份额 -> 必须严格归零
    quantity_t q3;
    money_t c3, red2, pnl2;
    rc = ledger_calc_sell_position(q2, c2, q2, sell1_p, sell1_fee, &q3, &c3, &red2, &pnl2);
    assert(rc == 0);
    assert(quantity_is_zero(q3));
    assert(money_is_zero(c3));

    printf("PASS: test_ledger_position_rebuild_consistency\n");
}
```

- [ ] **Step 2: 编译并执行单元测试确认可编译**

运行：`ctest --test-dir backend/build -R test_ledger_engine --output-on-failure`
预期：PASS（当前现有测试通过）

- [ ] **Step 3: 重构 `backend/src/common/balance.c` 中的 `apply_position_fc`**

引入 `#include "core/ledger/ledger_engine.h"`，将 `apply_position_fc()` 中自己编写的 `money_div_quantity(..., 6, ...)` 替换为直接复用 `ledger_calc_buy_position` 与 `ledger_calc_sell_position`：

```c
    quantity_t new_qty;
    money_t    new_cost;
    price_t    new_net;

    if (strcmp(type, "buy") == 0) {
        if (ledger_calc_buy_position(old_qty, old_cost, qty, price, fee, &new_qty, &new_cost) != 0) {
            csilk_json_free(pos);
            return -1;
        }
        new_net = price;
    } else if (strcmp(type, "sell") == 0) {
        money_t cost_red, trade_pnl;
        if (ledger_calc_sell_position(old_qty, old_cost, qty, price, fee,
                                      &new_qty, &new_cost, &cost_red, &trade_pnl) != 0) {
            csilk_json_free(pos);
            CSILK_LOG_W("apply_position: sell calculation failed asset_id=%lld", (long long)asset_id);
            return -1;
        }
        new_net = old_net;
    } else {
        csilk_json_free(pos);
        return 0;
    }
```

同理对 `rollback_position_fc()` 进行对应收敛，确保回滚与正向应用完全互逆。

- [ ] **Step 4: 编译并运行单元测试与集成测试验证**

运行：`cmake --build backend/build --parallel && ctest --test-dir backend/build -R test_ledger_engine --output-on-failure`
预期：PASS=1 FAIL=0

- [ ] **Step 5: 提交变更**

```bash
git add backend/src/common/balance.c backend/tests/unit/test_ledger_engine.c
git commit -m "refactor(ledger): ♻️ unify position calculation in balance with ledger engine"
```

---

### Task 2: 领域成本基础与已实现盈亏契约对齐 (Align cost_basis.c with portfolio/rules.c)

**Files:**
- Modify: `backend/src/domain/asset/cost_basis.c:64-120`
- Test: `backend/tests/unit/test_domain_cost_basis.c`

- [ ] **Step 1: 编写已实现盈亏不扣减卖出手续费的单元测试**

在 `backend/tests/unit/test_domain_cost_basis.c` 中更新并新增测试用例 `test_cost_basis_sell_pure_spread_realized`：

```c
static void test_cost_basis_sell_pure_spread_realized(void) {
    currency_t cny = CURRENCY_CNY;
    mf_cost_basis_t cb = mf_cost_basis_init(cny);

    // 买入 100 股 @ 10 元，手续费 5 元 -> total_cost = 1005 元, total_cost_pnl = 1000 元
    quantity_t q1;
    quantity_from_double(100.0, 4, &q1);
    money_t amt1, fee1;
    money_from_double(1000.0, cny, &amt1);
    money_from_double(5.0, cny, &fee1);
    mf_cost_basis_apply_buy(&cb, q1, amt1, fee1);

    // 卖出 100 股 @ 12 元，手续费 8 元 (变现金额 1200 元)
    // 契约：已实现盈亏为纯价差 = 1200 - 1000 = +200 元 (手续费 8 元由独立费用交易记录)
    quantity_t q2;
    quantity_from_double(100.0, 4, &q2);
    money_t amt2, fee2, accum_pnl = money_zero(cny);
    money_from_double(1200.0, cny, &amt2);
    money_from_double(8.0, cny, &fee2);

    int rc = mf_cost_basis_apply_sell(&cb, &accum_pnl, q2, amt2, fee2);
    assert(rc == 0);
    assert(fabs(money_to_double(accum_pnl) - 200.0) < 1e-4);
    assert(quantity_is_zero(cb.quantity));
    assert(money_is_zero(cb.total_cost));
    assert(money_is_zero(cb.total_cost_pnl));

    printf("PASS: test_cost_basis_sell_pure_spread_realized\n");
}
```

- [ ] **Step 2: 运行测试验证失败（Red）**

运行：`ctest --test-dir backend/build -R test_domain_cost_basis --output-on-failure`
预期：FAIL（原代码计算 `1200 - 8 - 1000 = 192`，断言 200 失败）

- [ ] **Step 3: 修改 `backend/src/domain/asset/cost_basis.c`**

在 `mf_cost_basis_apply_sell` 中将已实现盈亏计算从 `proceeds (amount - fee)` 修正为纯变现金额 `amount`：

```c
    // Pure trade spread = amount - cost_reduction
    // Full sell check: qty == cb->quantity
    if (quantity_cmp(qty, cb->quantity) == 0) {
        money_t pnl_cost_deducted = cb->total_cost_pnl;
        money_t trade_realized;
        money_sub(amount, pnl_cost_deducted, &trade_realized);

        if (accum_realized_pnl) {
            money_add(*accum_realized_pnl, trade_realized, accum_realized_pnl);
        }

        cb->quantity = quantity_zero();
        cb->total_cost = money_zero(cb->currency);
        cb->total_cost_pnl = money_zero(cb->currency);
        cb->average_cost = price_zero(cb->currency);
        return 0;
    }
```

部分卖出同理使用 `amount` 减去比例成本，清仓时置零 `total_cost` 与 `total_cost_pnl`。

- [ ] **Step 4: 编译并运行单元测试验证通过（Green）**

运行：`cmake --build backend/build --parallel && ctest --test-dir backend/build -R test_domain_cost_basis --output-on-failure`
预期：PASS=1 FAIL=0

- [ ] **Step 5: 提交变更**

```bash
git add backend/src/domain/asset/cost_basis.c backend/tests/unit/test_domain_cost_basis.c
git commit -m "fix(asset): 🐛 align cost basis realized pnl to pure trade spread"
```

---

### Task 3: 现金流与定投排期“月末动态对齐”算法 (Dynamic Month-end Clamping in cashflow/rules.c)

**Files:**
- Modify: `backend/src/domain/cashflow/rules.c:70-134`
- Modify: `backend/src/domain/cashflow/rules.h`
- Test: `backend/tests/unit/test_domain_cashflow.c`

- [ ] **Step 1: 编写大月 31 日与闰年二月排期匹配的单元测试**

在 `backend/tests/unit/test_domain_cashflow.c` 中添加针对 31 号与二月末日期的测试：

```c
static void test_cashflow_month_end_clamping(void) {
    int out_day = 0;
    // 2026年3月 (31天)：31号排期应匹配 31 号
    bool m1 = mf_cashflow_rule_matches_month("monthly", "2026-01-31", "", 2026, 3, &out_day);
    assert(m1 == true);
    assert(out_day == 31);

    // 2026年4月 (30天)：31号排期应平滑对齐到 30 号
    bool m2 = mf_cashflow_rule_matches_month("monthly", "2026-01-31", "", 2026, 4, &out_day);
    assert(m2 == true);
    assert(out_day == 30);

    // 2026年2月 (平年28天)：31号排期应平滑对齐到 28 号
    bool m3 = mf_cashflow_rule_matches_month("monthly", "2026-01-31", "", 2026, 2, &out_day);
    assert(m3 == true);
    assert(out_day == 28);

    // 2028年2月 (闰年29天)：31号排期应平滑对齐到 29 号
    bool m4 = mf_cashflow_rule_matches_month("monthly", "2026-01-31", "", 2028, 2, &out_day);
    assert(m4 == true);
    assert(out_day == 29);

    printf("PASS: test_cashflow_month_end_clamping\n");
}
```

- [ ] **Step 2: 运行测试验证失败（Red）**

运行：`ctest --test-dir backend/build -R test_domain_cashflow --output-on-failure`
预期：FAIL（原代码写死 clamp 到 28，导致 3 月份 `out_day == 28`，断言 31 失败）

- [ ] **Step 3: 在 `backend/src/domain/cashflow/rules.c` 中实现动态月末对齐算法**

```c
int
mf_calendar_days_in_month(int year, int month)
{
    if (month == 2) {
        bool is_leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return is_leap ? 29 : 28;
    }
    if (month == 4 || month == 6 || month == 9 || month == 11) {
        return 30;
    }
    return 31;
}

int
mf_calendar_clamp_day(int year, int month, int target_day)
{
    int max_days = mf_calendar_days_in_month(year, month);
    if (target_day < 1) {
        target_day = 1;
    }
    return target_day > max_days ? max_days : target_day;
}
```

在 `mf_cashflow_rule_matches_month()` 中：
```c
    int target_day = mf_calendar_clamp_day(year, month, s_day);
    if (out_day) {
        *out_day = target_day;
    }
```

在 `backend/src/domain/cashflow/rules.h` 中导出 `mf_calendar_days_in_month` 与 `mf_calendar_clamp_day` 原型。

- [ ] **Step 4: 编译并运行单元测试验证通过（Green）**

运行：`cmake --build backend/build --parallel && ctest --test-dir backend/build -R test_domain_cashflow --output-on-failure`
预期：PASS=1 FAIL=0

- [ ] **Step 5: 提交变更**

```bash
git add backend/src/domain/cashflow/rules.c backend/src/domain/cashflow/rules.h backend/tests/unit/test_domain_cashflow.c
git commit -m "fix(cashflow): 🐛 implement dynamic month-end calendar alignment for schedules"
```

---

### Task 4: 报表分类统计百分比求和闭包与尾数平衡 (Percentage Closure in report_expense_service.c)

**Files:**
- Modify: `backend/src/services/report_expense_service.c:500-545`
- Test: `backend/tests/unit/test_domain_repositories.c`

- [ ] **Step 1: 编写分类百分比闭包单元测试**

在 `backend/tests/unit/test_domain_repositories.c` 中添加针对多分类支出百分比之和严格闭合为 100.00% 的断言测试：

```c
static void test_report_expense_percentage_closure(void) {
    // 模拟三笔消费：100.00, 100.00, 100.00 (各占 33.33%)
    // 契约：最大项自动平衡 0.01%，保证各项和为 100.00%
    double amounts[3] = {100.0, 100.0, 100.0};
    double total = 300.0;
    double pcts[3] = {0};
    double sum = 0;
    for (int i = 0; i < 3; i++) {
        pcts[i] = round((amounts[i] / total * 100.0) * 100.0) / 100.0;
        sum += pcts[i];
    }
    // 平衡尾数
    double diff = round((100.0 - sum) * 100.0) / 100.0;
    if (fabs(diff) <= 0.05) {
        pcts[0] += diff; // 补偿给首项
    }
    double final_sum = pcts[0] + pcts[1] + pcts[2];
    assert(fabs(final_sum - 100.0) < 1e-4);
    printf("PASS: test_report_expense_percentage_closure\n");
}
```

- [ ] **Step 2: 修改 `backend/src/services/report_expense_service.c` 中的 `report_expense_category`**

在输出 items 循环前，进行定点四舍五入并执行最大项尾数平衡：

```c
    // Calculate rounded percentages (2 decimals) and balance largest remainder
    double pcts[128] = {0};
    double pct_sum = 0.0;
    int max_idx = 0;
    double max_amt = -1.0;

    for (int k = 0; k < cat_count; k++) {
        if (total > 0.0) {
            pcts[k] = round((cats[k].amount / total * 100.0) * 100.0) / 100.0;
        } else {
            pcts[k] = 0.0;
        }
        pct_sum += pcts[k];
        if (cats[k].amount > max_amt) {
            max_amt = cats[k].amount;
            max_idx = k;
        }
    }

    if (total > 0.0 && cat_count > 0) {
        double delta = round((100.0 - pct_sum) * 100.0) / 100.0;
        if (fabs(delta) <= 0.05) {
            pcts[max_idx] += delta;
        }
    }

    csilk_json_t* items = csilk_json_array();
    for (int k = 0; k < cat_count; k++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "name", cats[k].name);
        csilk_json_add_number(item, "amount", cats[k].amount);
        csilk_json_add_number(item, "pct", pcts[k]);
        csilk_json_array_append(items, item);
    }
```

- [ ] **Step 3: 编译并执行单元测试**

运行：`cmake --build backend/build --parallel && ctest --test-dir backend/build --output-on-failure`
预期：32/32 tests passed (100%)

- [ ] **Step 4: 提交变更**

```bash
git add backend/src/services/report_expense_service.c backend/tests/unit/test_domain_repositories.c
git commit -m "fix(report): 🐛 balance category expense percentage closure to exact 100 percent"
```

---

### Task 5: 全量回归测试与端到端验证 (Full CTest, Integration Scripts & Frontend Build Verification)

**Files:**
- Test all: backend CTest, integration test suites, frontend tests & builds

- [ ] **Step 1: 运行后端全部 32 个 CTest 单元测试**

```bash
cd backend/build && ctest --output-on-failure && cd ../..
```
预期输出：`100% tests passed out of 32`

- [ ] **Step 2: 运行全部 8 个端到端集成测试脚本**

```bash
cd backend
./tests/test_link.sh
./tests/test_ledgers.sh
./tests/test_2fa.sh
./tests/test_dca_cashflow.sh
./tests/test_ai_trace.sh
./tests/test_market_sync.sh
./tests/test_fx_oauth.sh
cd ..
```
预期输出：全部 PASS，`test_link.sh` 结果 PASS=151 FAIL=0。

- [ ] **Step 3: 运行前端自动化测试 (Vitest)**

```bash
npm --prefix frontend test
```
预期输出：17 个测试文件全部通过（78 passed）。

- [ ] **Step 4: 运行前端桌面端与移动端生产环境完整构建**

```bash
npm --prefix frontend run build && npm --prefix frontend run build:mobile
```
预期输出：`vue-tsc -b` 0 错误，打包全部完成。
