# 后端金融计算逻辑全景审计与双轨收敛重构设计 (Backend Calculation Logic Audit & Redesign)

- **创建日期**: 2026-10-16
- **架构标准**: C23, DDD 四层单向依赖架构, 128 位定点数金融核心 (Financial Core)
- **目标分支**: `master`

---

## 1. 概述与设计背景 (Executive Summary)

Minefolio 作为一个自托管的个人财务与全品类投资记账系统，其后端承担着资产净值、复式记账差量、持仓成本基础（加权平均）、买卖已实现与浮动盈亏、多币种汇率折算与损益分离、定投预测及统计聚合等关键算力任务。

在前期业务迭代演进中，底层虽然建立了高精度定点数引擎（`core/financial/`）与账本核心（`core/ledger/`），但在各业务模块的落地过程中，由于历史代码共存，系统存在三项结构性风险：
1. **核算算法“双轨制”**：实时交易写入（`common/balance.c:apply_position_fc`）与账本重放重建（`core/ledger/ledger_engine.c`）分别维护了加权平均减仓算法，中间折算精度分别为 6 位与 12 位，多次交易后产生累计微小舍入舍入差，导致 Rebuild 触发时持仓成本微弱跳变；
2. **盈亏与手续费契约定义不一**：`domain/asset/cost_basis.c` 在卖出已实现盈亏中扣减了手续费，而系统既有的交易流水架构已将手续费作为独立子交易单列扣费并记入费用，导致标的持仓盈亏与财务费用产生双重扣减；
3. **部分报表浮点截断与月末排期边界**：`report_expense_service.c` 仍大量使用 `double` 累加与百分比计算，易产生 `99.999999%` 饼图截断误差；现金流与定投排期将 29~31 号简单截断至 28 号，缺乏平滑的月末对齐（`last_day_of_month`）。

本方案按照**【双轨收敛与契约归一】**原则，在**严格保持数据库 Schema 100% 兼容**的前提下，从纯算法与代码架构层面彻底消除上述分歧与隐患，确立全系统唯一的数学可信源（SSOT）。

---

## 2. 全景审计与缺陷风险矩阵 (Audit Findings Matrix)

| 领域模块 | 涉及源码路径 | 现状问题与缺陷根因 | 潜在风险级别 | 治理与收敛方案 |
| :--- | :--- | :--- | :--- | :--- |
| **投资减仓核算** | `common/balance.c`<br>`core/ledger/ledger_engine.c` | 两套加权平均算法并存：`balance.c` 使用 6 位均价折算扣减成本，`ledger_engine.c` 使用 12 位比例折算扣减成本 | **High (高危)** | 废除 `balance.c` 重复算法，底层直接调用 `ledger_calc_buy_position` / `ledger_calc_sell_position` |
| **手续费与盈亏契约** | `domain/asset/cost_basis.c`<br>`domain/portfolio/rules.c` | `cost_basis.c` 在变现中扣减 fee 计算已实现盈亏；而交易系统已生成独立 `fee` 交易，造成手续费双重扣除 | **High (高危)** | 统一确立持仓已实现盈亏为纯交易价差（`ev->amount - sold_cost`），手续费独立归集，杜绝双重扣减 |
| **资产清仓残余** | `common/balance.c`<br>`domain/portfolio/rules.c` | 部分卖出后若份额减至 0，由于浮点或定点除法存在极其微小的末位余数（Cents/Satoshis）未清空 | **Medium (中危)** | 在 `quantity == 0` 时强制将 `cost_basis`、`cost_for_pnl` 置为 0，防止幽灵成本悬挂 |
| **排期月末日期漂移** | `domain/cashflow/rules.c` | 将大于 28 号的排期粗暴截断为 28 号，导致大月（31天）与小月（30天）排期严重失真 | **Medium (中危)** | 实现 `mf_calendar_days_in_month()` 动态月末对齐算法，大月 31 日平滑触发 |
| **统计百分比闭包** | `services/report_expense_service.c` | 分类支出占比采用原生 `double` 独立除法计算，各项之和经常呈现 $99.98\%$ 或 $100.02\%$ | **Low (低危)** | 引入定点数 `percentage_calc` 与最大项尾数平衡（Largest Remainder Balancing），确保和恒为 100.00% |
| **多币种历史折算** | `services/market/exchange_rate_service.c` | 历史收支聚合时使用最新当前汇率，若未记录历史汇率可能与发生时账面有偏差 | **Low (低危)** | 保留当前即席折算规范，在 `report_asset_service.c` 中完善资产缺失 `initial_rate` 时的平滑兜底 |

---

## 3. 架构与单一可信源（SSOT）设计

系统严格遵循 DDD 四层单向依赖架构，层级依赖严格单向流动：`Interfaces -> Application -> Domain -> Core Financial / Ledger`。

```
┌──────────────────────────────────────────────────────────────┐
│ Interfaces Layer (controllers/*_controller.c)                │
└──────────────────────────────┬───────────────────────────────┘
                               │ calls usecase
┌──────────────────────────────▼───────────────────────────────┐
│ Application Layer (application/*, services/*)                │
└──────────────┬───────────────────────────────┬───────────────┘
               │ delegates                     │ coordinates
┌──────────────▼──────────────┐ ┌──────────────▼───────────────┐
│ Domain Layer (domain/*)     │ │ Shared Balance & Common      │
│ • portfolio/rules.c         │ │ • balance_apply_delta_m()    │
│ • cashflow/rules.c          │ │ • apply_position_fc()        │
│ • asset/cost_basis.c        │ └──────────────┬───────────────┘
└──────────────┬──────────────┘                │ delegates SSOT
               │ pure math                     │
┌──────────────▼───────────────────────────────▼───────────────┐
│ Core Layer (SSOT 单一数学可信源)                              │
│ • core/ledger/ledger_engine.c: 加权平均减仓、重放重算与重建      │
│ • core/financial/*: decimal (128位), money, quantity, price   │
└──────────────────────────────────────────────────────────────┘
```

### 3.1 单一可信源收敛设计
1. **统一核心运算入口**：所有涉及投资资产份额（`quantity`）、成本基础（`cost_basis`）、单位净值（`net_value`）的变动推导，统一由 `core/ledger/ledger_engine.h` 提供的接口执行：
   - `ledger_calc_buy_position()`
   - `ledger_calc_sell_position()`
   - `ledger_calc_unrealized_pnl()`
2. **`common/balance.c` 瘦身为调度层**：`apply_position_fc` 与 `rollback_position_fc` 内部不再自行编写减仓与均价折算公式，而是直接调用 `ledger_calc_buy_position` 与 `ledger_calc_sell_position`，然后落库并更新相关资产行。
3. **Rebuild 严格恒等性保证**：由于实时交易与历史全量 Rebuild 共享完全相同的算子与精度规则，任何时刻执行 `POST /api/assets/:id/rebuild` 重建结果与实时增量累积完全一致。

---

## 4. 核心数学规则与形式化契约 (Mathematical Rules)

### 4.1 加权平均成本法（Weighted Average Cost Basis）
所有持仓核算均采用 128 位定点数（`decimal_t`）与强类型货币模型（`money_t`）：

1. **买入加仓 (Buy)**：
   $$Qty_{new} = Qty_{prev} + Qty_{buy}$$
   $$Cost_{new} = Cost_{prev} + (Price_{buy} \times Qty_{buy}) + Fee_{buy}$$
2. **部分卖出减仓 (Partial Sell)**：
   $$Ratio = \text{decimal\_div}(Qty_{sell}, Qty_{prev}, \text{scale}=12, \text{ROUND\_HALF\_UP})$$
   $$CostReduction = \text{round}_{\text{currency}}(Cost_{prev} \times Ratio)$$
   $$Qty_{new} = Qty_{prev} - Qty_{sell}$$
   $$Cost_{new} = Cost_{prev} - CostReduction$$
   $$RealizedPnL = (Price_{sell} \times Qty_{sell}) - CostReduction$$
3. **全部清仓置零 (Full Liquidation)**：
   当 $Qty_{sell} == Qty_{prev}$ 时：
   $$Qty_{new} = 0, \quad Cost_{new} = 0, \quad CostReduction = Cost_{prev}$$
   $$RealizedPnL = (Price_{sell} \times Qty_{sell}) - CostReduction$$

### 4.2 手续费与分红收益契约
1. **纯价差盈亏定义**：标的已实现盈亏（Realized PnL）定义为纯交易价差，不二次扣减卖出手续费；
2. **手续费单列**：买卖交易发生的手续费自动派生 `fee` 独立子交易，统一计入用户日常收支中的“费用”大类；
3. **分红派息直接累加**：分红（`dividend`）与利息（`interest`）在持仓统计中正向累加至 `realized_pnl`，持仓份额与成本不变：
   $$RealizedPnL_{new} = RealizedPnL_{prev} + Amount_{dividend}$$

### 4.3 多币种汇率损益分离数学模型 (FX Gain/Loss Decomposition)
对外币投资标的（原币 $FC$，本位币 $BC$，建仓汇率 $R_{cost}$，当前最新汇率 $R_{curr}$）：
* **标的价格收益 (Asset Price PnL in BC)**：
  $$PnL_{asset} = (Market_{FC} - Cost_{FC}) \times R_{curr}$$
* **汇率波动收益 (FX PnL in BC)**：
  $$PnL_{fx} = Cost_{FC} \times (R_{curr} - R_{cost})$$
* **综合总盈亏恒等式**：
  $$PnL_{total} = PnL_{asset} + PnL_{fx} \equiv Market_{BC} - Cost_{BC}$$

---

## 5. 边界保护与日历算法设计 (Boundary Defense & Calendar)

### 5.1 除零防御与零基线安全
1. **持仓浮动盈亏率**：
   - 当 $CostBasis == 0$ 且 $Quantity == 0$ 时，$\text{floating\_pct} = 0.0\%$；
   - 当 $CostBasis == 0$ 且 $Quantity > 0$ 时，$\text{floating\_pct} = +100.0\%$，浮动盈亏即为全部当前市值；
2. **定投止盈率**：
   - 当 $\text{TotalInvested} \le 0$ 时，$\text{profit\_rate} = 0.0\%$；
3. **超卖保护**：
   - 当 $Qty_{sell} > Qty_{prev}$ 时，严格拒绝执行，返回错误码 `DECIMAL_ERR_INVALID_ARG`。

### 5.2 动态月末对齐算法 (Dynamic Month-end Clamping)
在 `domain/cashflow/rules.c` 中替换原有的写死 28 号截断逻辑，引入基于历法的平滑对齐函数：
```c
static inline int mf_calendar_days_in_month(int year, int month) {
    if (month == 2) {
        bool is_leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return is_leap ? 29 : 28;
    }
    if (month == 4 || month == 6 || month == 9 || month == 11) {
        return 30;
    }
    return 31;
}

static inline int mf_calendar_clamp_day(int year, int month, int target_day) {
    int max_days = mf_calendar_days_in_month(year, month);
    if (target_day < 1) target_day = 1;
    return target_day > max_days ? max_days : target_day;
}
```

### 5.3 报表分类占比尾数平衡 (Percentage Closure)
在分类统计聚合中：
1. 每一项通过 `percentage_calc()` 计算定点数比例（保留 2 位小数）；
2. 计算累计和 $\sum Pct_i$ 与 $100.00\%$ 的差值 $\Delta = 100.00 - \sum Pct_i$；
3. 若 $|\Delta| \le 0.05\%$，将 $\Delta$ 补偿给占比最大的分类项，保证前端饼图总和严格闭合为 $100\%$。

---

## 6. 错误处理与事务保护 (Error Handling & Atomicity)

* **错误码映射**：
  - 核心算术溢出 (`DECIMAL_ERR_OVERFLOW`) $\rightarrow$ HTTP 200 `{code: 1002, message: "金额数值超出安全计算上限"}`
  - 非法参数/超卖 (`DECIMAL_ERR_INVALID_ARG`) $\rightarrow$ HTTP 200 `{code: 1002, message: "交易份额不足或参数非法"}`
  - 除以零 (`DECIMAL_ERR_DIV_BY_ZERO`) $\rightarrow$ 业务平滑兜底为 $0.0\%$，避免系统崩溃。
* **事务回滚**：
  多表或多行联动变更（如主交易、手续费子交易、资产余额变动日志）必须包含在 `BEGIN TRANSACTION` 与 `COMMIT` 之中。一旦核心计算失败，强制执行 `ROLLBACK`，确保无脏数据落盘。

---

## 7. 质量保证与测试验证矩阵 (Verification Matrix)

| 测试层级 | 验证载体 | 验证重点与断言指标 |
| :--- | :--- | :--- |
| **单元测试** | `test_ledger_engine.c` | 验证多笔碎股买卖后减仓精度无漂移，验证全账本 Rebuild 恒等性 |
| **单元测试** | `test_domain_portfolio.c` | 验证已实现盈亏为纯交易价差，分红/派息正向累加且不影响持仓基础 |
| **单元测试** | `test_domain_cashflow.c` | 验证 28/29/30/31 号排期在闰年二月及大/小月的精准对齐 |
| **单元测试** | `test_domain_repositories.c` | 验证日常收支月度汇率折算与多币种聚合稳定性 |
| **集成测试** | `test_link.sh` | 全量运行 38 个业务大项（151 个断言必须 100% PASS，0 FAIL） |
| **集成测试** | `test_ledgers.sh`<br>`test_dca_cashflow.sh`<br>`test_fx_oauth.sh` | 专项端到端测试覆盖多账本隔离、定投日历预测及多币种外汇 PnL |
| **前端验证** | Vitest & 双端构建 | Vitest 78 项测试全过；桌面端与移动端 `vue-tsc -b` 0 报错 |
