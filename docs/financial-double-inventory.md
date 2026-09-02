# Minefolio 金融数值系统 `double` 字段全面扫描与重构清单

本文档统计并标记了 Minefolio 后端代码库中所有涉及金额、持仓、价格、费率、收益率等金融字段的原有 `double` 声明，并给出了其重构后目标强类型映射。

---

## 1. 金融字段领域类型映射规范

| 业务概念 | 业务语义 | 原类型 | 目标强类型 (`core/financial/`) | 说明 |
| :--- | :--- | :---: | :---: | :--- |
| **金额 (Money)** | 具有法定货币属性的价值，如账户余额、发生额、成本、收入、支出、手续费、分红、利息、已实现/浮动盈亏等 | `double` | `money_t` | 绑定 `currency_t`，跨币种运算强制检查或通过 `rate_t` 转换 |
| **份额/数量 (Quantity)** | 标的资产的份额、股数、持仓张数、加密货币单位等 | `double` | `quantity_t` | 精度通常达 4~8 位小数（如基金 0.0001，加密货币 0.00000001） |
| **价格 (Price)** | 资产每单位的计价（净值、成交价、行情报价） | `double` | `price_t` | 具有报价币种属性，满足 $\text{Price} \times \text{Quantity} = \text{Money}$ |
| **汇率/费率 (Rate)** | 汇率转换因子、费率乘数、折现率、税率 | `double` | `rate_t` | 源币种到目标币种转换因子，支持链式乘除 |
| **百分比 (Percentage)** | 收益率、仓位占比、涨跌幅、止盈率 | `double` | `percentage_t` | 表示相对比率（如 15.5%），支持与 Money 相互作用 |

---

## 2. 字段扫描与重构清单 (Inventory)

### 2.1 核心公共层 (`backend/src/common/`)

| 文件 | 结构体/函数/变量 | 字段名 | 原类型 | 目标类型 | 语义说明 |
| :--- | :--- | :--- | :---: | :---: | :--- |
| `balance.h` / `balance.c` | `balance_apply_delta` | `delta` | `double` | `money_t` | 资产余额变动增减额 |
| `balance.h` / `balance.c` | `balance_apply_delta` | `signed_delta` | `double` | `money_t` | 经负债方向翻转后的带符号增量 |
| `balance.h` / `balance.c` | `apply_position` | `amount` | `double` | `money_t` | 投资买卖发生金额 |
| `balance.h` / `balance.c` | `apply_position` | `fee` | `double` | `money_t` | 交易手续费 |
| `balance.h` / `balance.c` | `apply_position` | `price` | `double` | `price_t` | 成交单价/净值 |
| `balance.h` / `balance.c` | `apply_position` | `qty` | `double` | `quantity_t` | 成交份额 |
| `balance.h` / `balance.c` | `apply_position` | `out_position_delta` | `double*` | `money_t*` | 持仓市值变动量 |
| `balance.h` / `balance.c` | `apply_position` | `old_cost`, `new_cost` | `double` | `money_t` | 持仓总成本 |
| `balance.h` / `balance.c` | `apply_position` | `old_qty`, `new_qty` | `double` | `quantity_t` | 持仓总份额 |
| `balance.h` / `balance.c` | `apply_position` | `old_net`, `new_net` | `double` | `price_t` | 最新单位净值 |
| `balance.h` / `balance.c` | `apply_position` | `avg_cost` | `double` | `price_t` | 持仓加权平均单位成本 |
| `tx_types.h` / `tx_types.c` | `tx_delta` | `amount` | `double` | `money_t` | 交易发生额 |
| `tx_types.h` / `tx_types.c` | `tx_delta` | `price` | `double` | `price_t` | 交易单价 |
| `tx_types.h` / `tx_types.c` | `tx_delta` | `qty` | `double` | `quantity_t` | 交易份额 |
| `tx_types.h` / `tx_types.c` | `tx_effective_ldelta` | `amount`, `tdelta` | `double` | `money_t` | 关联账户变动增量 |
| `market_types.h` | `market_quote_t` | `current_price` | `double` | `price_t` | 实时行情报价 |
| `market_types.h` | `market_quote_t` | `change_percent` | `double` | `percentage_t` | 涨跌幅百分比 |
| `plan_types.h` | `dca_plan_t` | `target_amount` | `double` | `money_t` | 每期定投目标金额 |
| `plan_types.h` | `dca_plan_t` | `take_profit_rate` | `double` | `percentage_t` | 止盈收益率阈值 |
| `plan_types.h` | `dca_execution_t` | `planned_amount` | `double` | `money_t` | 计划扣款金额 |
| `plan_types.h` | `dca_execution_t` | `executed_price` | `double` | `price_t` | 实际成交价格 |
| `plan_types.h` | `dca_execution_t` | `executed_quantity` | `double` | `quantity_t` | 实际买入份额 |
| `plan_types.h` | `cashflow_schedule_t`| `amount` | `double` | `money_t` | 周期性现金流金额 |
| `db.h` / `db.c` | `db_get_num` | 返回值 | `double` | `decimal_t` | 数据库通用数值提取（升级为 `db_get_decimal` / `db_get_money`） |

---

### 2.2 实体模型层 (`backend/src/models/`)

| 文件 | 结构体 | 字段名 | 原类型 | 目标类型 | 语义说明 |
| :--- | :--- | :--- | :---: | :---: | :--- |
| `daily_expense.h` | `daily_expense_t` | `amount` | `double` | `money_t` | 日常收支金额 |
| `asset.h` | `asset_t` | `current_value` | `double` | `money_t` | 当前资产市值/余额 |
| `asset.h` | `asset_t` | `cost_basis` | `double` | `money_t` | 持仓持计成本 |
| `asset.h` | `asset_t` | `quantity` | `double` | `quantity_t` | 持仓数量 |
| `asset.h` | `asset_t` | `net_value` | `double` | `price_t` | 单位净值 |
| `transaction.h` | `transaction_t` | `amount` | `double` | `money_t` | 交易发生总额 |
| `transaction.h` | `transaction_t` | `fee` | `double` | `money_t` | 交易附加手续费 |
| `transaction.h` | `transaction_t` | `price` | `double` | `price_t` | 交易单价 |
| `transaction.h` | `transaction_t` | `quantity` | `double` | `quantity_t` | 交易份额 |

---

### 2.3 业务服务层 (`backend/src/services/`)

| 文件 | 函数 | 涉及变量 | 原类型 | 目标类型 | 语义说明 |
| :--- | :--- | :--- | :---: | :---: | :--- |
| `services/market/exchange_rate_service.c` | `exchange_rate_get_to_cny` | 返回值与汇率字典 | `double` | `rate_t` | 实时外汇汇率 |
| `services/market/exchange_rate_service.c` | `exchange_rate_convert` | `amount`, 返回值 | `double` | `money_t` | 双币种金额折算 |
| `services/report_asset_service.c` | `report_multi_currency_summary`| `assets`, `liabilities`, `net` | `double` | `money_t` | 多币种分桶汇总资产负债 |
| `services/report_asset_service.c` | `report_multi_currency_summary`| `percentage` | `double` | `percentage_t` | 资产币种占比 |
| `services/report_asset_service.c` | `report_fx_pnl` | `cost_basis_orig`, `current_value_orig` | `double` | `money_t` | 原币资产成本与市值 |
| `services/report_asset_service.c` | `report_fx_pnl` | `asset_pnl_base`, `fx_pnl_base` | `double` | `money_t` | 标的价格盈亏与纯汇率损益 |
| `services/dca_service.c` | `dca_service_confirm_execution` | `executed_amount`, `price`, `qty` | `double` | `money_t`/`price_t`/`quantity_t` | 定投成交折算 |
| `services/cashflow_service.c` | `cashflow_service_get_calendar` | `projected_income`, `actual_income` | `double` | `money_t` | 现金流日历月度与年度推演 |
| `services/receipt_service.c` | `receipt_offline_fallback` | `amount`, `confidence` | `double` | `money_t`/`percentage_t` | OCR 票据结构化金额与置信度 |

---

## 3. 数据库字段精度与语义核查 (Database Audit)

| 表名 | 字段名 | SQLite 类型 | PostgreSQL 类型 | 目标精度与语义约束 |
| :--- | :--- | :---: | :---: | :--- |
| `assets` | `current_value` | `NUMERIC(18,4)` | `NUMERIC(18,4)` | 资产余额与市值，精确到 4 位小数（以容纳高精度货币折算） |
| `assets` | `cost_basis` | `NUMERIC(18,4)` | `NUMERIC(18,4)` | 持仓累计建仓与手续费成本 |
| `assets` | `quantity` | `NUMERIC(24,8)` | `NUMERIC(24,8)` | 持仓份额，支持加密货币（8位）与基金（4位） |
| `assets` | `net_value` | `NUMERIC(18,6)` | `NUMERIC(18,6)` | 单位净值，支持基金 4 位与股票 3 位 |
| `daily_expenses` | `amount` | `NUMERIC(18,2)` | `NUMERIC(18,2)` | 日常记账金额，精确到 2 位小数（分） |
| `transactions` | `amount` | `NUMERIC(18,4)` | `NUMERIC(18,4)` | 交易发生总额 |
| `transactions` | `fee` | `NUMERIC(18,4)` | `NUMERIC(18,4)` | 交易附加手续费 |
| `transactions` | `price` | `NUMERIC(18,6)` | `NUMERIC(18,6)` | 交易成交单价 |
| `transactions` | `quantity` | `NUMERIC(24,8)` | `NUMERIC(24,8)` | 交易成交份额 |
| `exchange_rate_history` | `rate` | `NUMERIC(18,6)` | `NUMERIC(18,6)` | 每日外汇汇率中间价 |
| `dca_plans` | `target_amount` | `NUMERIC(18,2)` | `NUMERIC(18,2)` | 定投目标金额 |
| `dca_plans` | `take_profit_rate`| `NUMERIC(8,4)` | `NUMERIC(8,4)` | 止盈百分比率 |
| `cashflow_schedules` | `amount` | `NUMERIC(18,2)` | `NUMERIC(18,2)` | 周期性计划金额 |
