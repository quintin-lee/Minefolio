# Minefolio P1-02: Asset / Portfolio Domain 重构设计方案

- **日期**：2026-09-03
- **状态**：Approved / Ready for Plan
- **领域范围**：`core/financial/`, `domain/asset/`, `domain/portfolio/`

---

## 1. 目标与设计背景

在 Minefolio 历史设计中，`assets` 表及 `mf_asset_t` 将 **Account（资金账户）**、**Asset（投资标的）** 与 **Position（持仓状态）** 混在了一个模型中（既有 `account_no`、`category_id`，又有 `quantity`、`cost_basis`、`net_value`、`symbol`）。这导致：
1. 投资品定义与资金托管账户混杂。
2. 持仓直接作为独立事实被修改，缺乏可追溯的金融流水一致性。
3. 组合分析与跨币种聚合存在隐式汇率假设风险。

本设计旨在彻底理清以下概念之间的关系：
- **`Asset`** = 投资标的定义（客观金融工具，不包含账户或持仓数据）
- **`Account`** = 资金/资产账户（现金存取、负债管理与托管空间）
- **`CostBasis`** = 成本基础（持仓份额、累计加权成本基础、分红调整）
- **`Valuation`** = 当前市场价值（最新市价与计价时点）
- **`PnL`** = 损益指标（严格区分已实现盈亏与未实现浮动盈亏）
- **`Position`** = 用户对 Asset 的持仓（由 Ledger 流水计算出的物化状态）
- **`Portfolio`** = 多个 Position 的组合聚合（负责聚合、配置分配、多币种折算、风险与集中度度量，绝不负责交易写入）

---

## 2. 领域实体与值对象架构

### 2.1 投资标的与账户实体

#### `mf_instrument_asset_t` (`domain/asset/instrument.h`)
```c
typedef struct {
    int64_t    id;               /**< 标的唯一 ID */
    char       symbol[32];       /**< 代码 (如 "AAPL", "600519", "BTC") */
    char       name[128];        /**< 标的名称 (如 "贵州茅台") */
    char       asset_type[32];   /**< 标的类别: "stock", "fund", "bond", "crypto" */
    currency_t native_currency;  /**< 原生计价货币 (如 USD, CNY) */
    char       quote_source[32]; /**< 行情源 (如 "stock_cn", "yahoo") */
    char       note[256];
} mf_instrument_asset_t;
```

#### `mf_account_t` (`domain/asset/account.h`)
```c
typedef struct {
    int64_t    id;               /**< 账户 ID */
    int64_t    user_id;          /**< 所属用户 */
    int64_t    ledger_id;        /**< 所属账本空间 */
    char       name[128];        /**< 账户名称 (如 "招行活期") */
    char       account_no[64];   /**< 账号/卡号 */
    char       account_type[32]; /**< "cash", "bank", "credit_card", "broker", "loan" */
    currency_t currency;         /**< 账户法定货币 */
    money_t    balance;          /**< 可用现金余额（或负债额） */
    bool       is_liability;     /**< 是否为负债账户 */
    char       note[256];
} mf_account_t;
```

---

### 2.2 成本、估值与盈亏值对象

#### `mf_cost_basis_t` (`domain/asset/cost_basis.h`)
```c
typedef struct {
    quantity_t quantity;       /**< 当前持仓份额 */
    money_t    total_cost;     /**< 累计总成本 (含买入手续费) */
    money_t    total_cost_pnl; /**< 用于盈亏核算的净成本 (扣除分红/剔除手续费) */
    price_t    average_cost;   /**< 加权平均成本单价 */
} mf_cost_basis_t;
```

#### `mf_valuation_t` (`domain/asset/valuation.h`)
```c
typedef struct {
    price_t    current_price;   /**< 最新单位价格/净值 */
    money_t    market_value;    /**< 当前市场总价值 = quantity * current_price */
    char       as_of[32];       /**< 估值基准时点 */
    char       price_source[32];/**< 价格源 */
} mf_valuation_t;
```

#### `mf_pnl_t` (`domain/asset/pnl.h`)
```c
typedef struct {
    money_t    realized_pnl;     /**< 累计已实现盈亏 (卖出平仓 + 现金分红) */
    money_t    unrealized_pnl;   /**< 账面浮动盈亏 = market_value - total_cost */
    money_t    total_pnl;        /**< 综合总盈亏 = realized_pnl + unrealized_pnl */
    double     unrealized_pct;   /**< 浮动盈亏收益率 (%) */
    double     total_return_pct; /**< 综合总回报率 (%) */
} mf_pnl_t;
```

---

### 2.3 持仓实体与单一真实来源派生 (Ledger → Position)

#### `mf_position_t` (`domain/asset/position.h`)
```c
typedef struct {
    int64_t         asset_id;    /**< 标的 ID */
    int64_t         account_id;  /**< 关联账户 ID */
    currency_t      currency;    /**< 计价原生币种 */
    quantity_t      quantity;    /**< 当前持仓数量 */
    mf_cost_basis_t cost_basis;  /**< 成本基础 */
    mf_valuation_t  valuation;   /**< 估值 */
    mf_pnl_t        pnl;         /**< 盈亏 */
} mf_position_t;
```

#### 纯函数派生规则：
```c
int mf_position_derive_from_ledger(int64_t            asset_id,
                                  int64_t            account_id,
                                  currency_t         native_currency,
                                  const ledger_tx_t* tx_events,
                                  size_t             tx_count,
                                  price_t            current_price,
                                  mf_position_t*     out_position);
```
- **Buy**：增加份额，增加成本基础，更新加权均价。
- **Sell**：等比例减少成本基础，计算平仓价差并计入已实现盈亏，若减为 0 成本清零。
- **Dividend**：不改变份额，现金分红计入已实现盈亏，并冲减成本基准。

---

### 2.4 显式多币种汇率表与投资组合聚合

#### `mf_fx_rate_table_t` (`domain/portfolio/fx_table.h`)
```c
typedef struct {
    currency_t from_currency;
    currency_t to_currency;
    rate_t     rate;           /**< target = src * rate */
    bool       is_valid;
} mf_fx_rate_entry_t;

typedef struct {
    size_t              count;
    mf_fx_rate_entry_t* entries;
} mf_fx_rate_table_t;

int mf_fx_convert_money(money_t                   src,
                        currency_t                target_currency,
                        const mf_fx_rate_table_t* rate_table,
                        money_t*                  out_converted);
```
- **拦截隐式折算**：若 `src.currency != target_currency` 且未提供对应汇率，函数立即返回错误，严禁默认 1:1 折算。

#### `mf_portfolio_t` (`domain/portfolio/portfolio.h`)
```c
typedef struct {
    currency_t     reporting_currency;
    size_t         position_count;
    mf_position_t*  positions;

    money_t        total_market_value;
    money_t        total_cost_basis;
    money_t        total_realized_pnl;
    money_t        total_unrealized_pnl;
    money_t        total_pnl;
    double         total_return_pct;

    /* 风险与资产配置指标 */
    double         max_holding_weight;   /**< 最大单标的持仓占比 (0.0~1.0) */
    int64_t        max_holding_asset_id; /**< 重仓标的 ID */
    double         herfindahl_index;     /**< 赫芬达尔集中度指数 (HHI) */
} mf_portfolio_t;
```

---

## 3. 测试套件规划

在 `backend/tests/unit/` 下建立 5 组独立纯 C 测试：
1. **`test_domain_cost_basis`**：测试加权成本、加仓手续费、减仓比例扣除、全部清仓清零、分红抵扣。
2. **`test_domain_pnl`**：测试浮动盈亏正负波动、卖出平仓已实现盈亏（扣手续费）、累计收益率。
3. **`test_domain_position`**：测试从时序交易事件流派生完整 Position、超卖防守。
4. **`test_domain_multi_currency`**：测试显式汇率换算、倒数换算、缺失汇率严格拦截。
5. **`test_domain_portfolio`**：测试多币种跨标的聚合、折算至报告货币、权重分配与 HHI 风险集中度计算。
