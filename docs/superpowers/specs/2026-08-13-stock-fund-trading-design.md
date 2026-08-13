# 股票/基金交易优化设计 (Stock & Fund Trading Optimization)

> **日期**: 2026-08-13
> **状态**: 设计定稿，待用户审阅
> **关联**: 基于 2026-08-13-transaction-direction（方向数据化）的延续

## 背景与目标

用户需求：*「交易中针对股票, 基金等特殊的交易方式进行优化完善, 使其可以准确记录实际的交易情况」*

当前实现的缺陷（已逐一实证）：
1. **无持仓概念**：`assets` 表只有 `current_value` 金额，无份额/成本字段。买 1000 份×2 元只知道 +2000 元，不知道持有 1000 份、成本 2000。
2. **盈亏语义错误**：`report_transaction_performance` 按资金流向记盈亏——买入一律记亏损、卖出一律记收益，非真实盈亏。
3. **细节缺失**：手续费只能独立记录，与买卖无关联；净值变化无法体现。

## 方案选型

- **方案 A（采纳）**：assets 加持仓字段，buy/sell 联动持仓与成本，报表改真实盈亏
- 方案 B（否决）：lot 级成本法 + 分红除息 + 外部行情 API —— 过度设计
- 方案 C（否决）：只改报表不加持仓字段 —— 无源之水

## 用户确认的设计决策

| 决策点 | 选择 |
|---|---|
| 净值维护 | **A2 自动计算**：加 `net_value` 字段，`current_value = quantity × net_value` 自动算 |
| 手续费 | **同表单便捷输入**：买/卖表单内加手续费输入，保存时生成关联 fee 交易行 |
| 数量单位 | **按类型自动带单位**：股票→股、基金→份、其他→无（仅展示，不落库） |

## 设计

### 1. 数据模型（assets 表加 3 列）

```sql
ALTER TABLE assets ADD COLUMN quantity   DECIMAL(18,4) NOT NULL DEFAULT 0;  -- 持有份额/股数
ALTER TABLE assets ADD COLUMN cost_basis DECIMAL(18,4) NOT NULL DEFAULT 0;  -- 累计买入成本
ALTER TABLE assets ADD COLUMN net_value  DECIMAL(18,4) NOT NULL DEFAULT 0;  -- 单位净值
```

- **迁移机制**：照 2026-08-13-transaction-direction 先例（db.c 条件迁移）。
  - SQLite：`col_exists` 门控 ALTER TABLE ADD COLUMN（3 个），`PRAGMA table_info` 检测。
  - PG：`ALTER TABLE ... ADD COLUMN IF NOT EXISTS`（3 个）。
- 只对 `stock/fund/bond/crypto` 类资产有意义，现金类保持 0。

### 2. 交易联动（transactions.c）

buy/sell 时在金额联动基础上扩展持仓联动：

| 交易 | 金额联动（现有，不动） | 持仓联动（新增） |
|---|---|---|
| buy | 投资资产 `+amount`，资金账户 `−amount` | `quantity += qty`；`cost_basis += amount`；`net_value = price_per_unit` |
| sell | 投资资产 `−amount`，资金账户 `+amount` | `quantity −= qty`；`cost_basis −= qty × avg_cost`（售出前均价 = 卖出前 cost_basis/quantity）；`net_value` 不变 |
| 净值更新 | assets_update 改 net_value | `current_value = quantity × net_value` 自动重算 |

**关键语义变化（A2）**：
- `current_value` 不再是用户手输的独立值，对投资类资产它**派生**自 `quantity × net_value`。
- buy 时 `net_value = price_per_unit`（成交价即净值起点）；此后净值由用户通过资产编辑维护。
- sell 后 `current_value` 自动重算为剩余份额 × 净值。
- **已实现盈亏** = `amount − qty × avg_cost`（每次 sell 实时累计）。

**手续费（同表单）**：buy/sell 表单加「手续费」输入，保存时**同事务**生成关联 fee 交易行：
- fee 行：`transaction_type=fee`、`asset_id=投资资产`（或资金账户）、`amount=fee`、`note=关联主交易`
- 手续费计入成本：buy 时 `cost_basis += fee`；sell 时已实现盈亏 `−= fee`

**编辑/删除回滚**：transactions_update/delete 已有旧值读取（SELECT asset_id, linked_asset_id, amount, transaction_type, quantity, price_per_unit），扩展为：旧值负向回滚持仓（quantity/cost_basis/net_value），新值正向应用，全部在同一事务内。

### 3. 真实盈亏报表（reports.c）

`report_transaction_performance` 重写，返回结构扩展：

```json
{
  "total_cost_basis_remaining": 2000.0,   // 当前持仓总成本
  "total_market_value": 2500.0,           // 当前市值 = Σ(current_value)
  "floating_pnl": 500.0,                  // 浮动盈亏 = market_value − cost_basis
  "realized_pnl": 400.0,                  // 已实现盈亏 = Σ(sell: amount−qty×avg_cost) − fee
  "trades": [
    { "id": 1, "asset_name": "xx基金", "transaction_type": "buy", "quantity": 1000,
      "price_per_unit": 2.0, "amount": 2000, "avg_cost_at_trade": 2.0,
      "realized": null, "fee": 0, "date": "2026-08-13" }
  ]
}
```

- buy 行：不再记 loss，只累加成本
- sell 行：`realized = amount − qty × avg_cost`
- 分红（income，category 为投资类）视为成本返还：`cost_basis −= amount`，`realized_pnl += amount`
- 旧字段 `total_gain/total_loss/net_gain` 保留为兼容（前端旧卡片仍可用），新增字段供新前端使用

### 4. 前端

1. **Assets.vue**：
   - 表格加「份额 / 成本 / 净值」列（仅投资类 asset_type 显示）
   - 对话框：新增「持有份额」「单位净值」输入（仅投资类显示）；保存时后端重算 current_value
2. **资产详情**（api/assets.ts detail 返回扩展）：持仓摘要（份额/成本/均价/现值/浮动盈亏）+ 历史交易
3. **Transactions.vue 表单**：
   - buy/sell 已有单价×数量；加「手续费」输入（同表单，保存生成 fee 行）
   - 数量单位按 asset_type 显示：stock→股、fund→份、其他→数字
4. **Reports.vue**：交易表现卡片显示 已实现盈亏 / 浮动盈亏 / 持仓市值

### 5. 类型与接口

- `types/index.ts`：`Asset` 加 `quantity?`、`cost_basis?`、`net_value?`
- `api/assets.ts`：create/update 支持 quantity/cost_basis/net_value 字段
- performance 响应类型扩展（`reports.ts`）

### 6. 测试计划（test_link.sh）

| 测试 | 步骤 | 断言 |
|---|---|---|
| T1 买入建仓 | 买 1000 份×2 元（asset=xx基金） | 资产 quantity=1000, cost_basis=2000, net_value=2, current_value=2000 |
| T2 净值更新 | PUT assets 改 net_value=2.5 | current_value=2500 |
| T3 卖出+已实现盈亏 | 卖 400 份×3 元 | 份额=600；已实现盈亏=400×3−400×2=+400；net_gain=400 |
| T4 浮动盈亏 | GET performance | floating_pnl=+300（600×(2.5−2.0)） |
| T5 手续费 | buy 100 份×2 元 + fee 5 元 | fee 行落库（type=fee）；cost_basis 含 5 |
| T6 编辑回滚 | 修改 T1 交易 qty 1000→500 | 持仓按差值重算 |
| T7 删除回滚 | 删除 T5 buy | 持仓/成本/份额归零 |

### 7. 验证门

- `cmake --build backend/build`
- `npm --prefix frontend run build`（0 错误）
- `cd backend && ./tests/test_link.sh` → PASS ≥ 68

## 风险与边界

- **净值维护负担**：A2 下用户需在净值变化时手动更新 net_value（或接受 current_value 停留在旧净值）。这是 A2 的固有权衡（用户已确认接受）。
- **历史数据**：存量投资类资产无 quantity/cost_basis/net_value（默认 0）——报表浮动盈亏=0−0=0，不报错但无意义；用户可自行补录或重录交易。
- **不做**：外部行情 API、lot 级 FIFO、除权除息、分红自动识别（YAGNI）。
