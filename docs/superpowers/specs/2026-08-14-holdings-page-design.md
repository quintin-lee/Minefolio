# 持仓页面设计 (Holdings Page)

> **日期**: 2026-08-14
> **状态**: 设计定稿，待用户审阅
> **关联**: 基于 2026-08-13-stock-fund-trading（持仓字段 + 真实 PnL）的展示层完善

## 背景与目标

用户需求：*「股票, 基金之类的持仓数据通过一个单独的页面来展示」*

当前实现的缺陷：
1. **无投资专属视图**：`Assets.vue` 将银行/现金/股票/基金等全部资产混排在一张表，股票基金虽带份额/成本/净值列，但无盈亏视角。
2. **盈亏数据无页面承载**：后端 `report_transaction_performance` 已算 floating_pnl / realized_pnl，但仅作为交易记录卡片的一行摘要，无持仓维度（按资产逐一盈亏）的展示。

目标：新增独立「持仓」页面 —— 投资仪表盘 + 持仓表格，聚焦股票/基金/债券/加密资产的持仓与盈亏。

## 方案选型

- **方案 A（采纳）**：后端新增聚合接口 `GET /api/reports/holdings`，按资产返回持仓 + 浮动/已实现盈亏，前端一次请求渲染
- 方案 B（否决）：纯前端用 `/assets` + performance 接口二次计算 —— trades 明细全量下发、口径分散
- 方案 C（否决）：复用 performance 接口再拼 `/assets` —— 两次请求两套编排，复杂度更高

## 用户确认的设计决策

| 决策点 | 选择 |
|---|---|
| 页面定位 | **持仓表格 + 投资仪表盘都要** |
| 盈亏口径 | **浮动盈亏 + 已实现盈亏**（已实现需后端按资产聚合） |
| 仪表盘内容 | **汇总卡片 + 资产类型环形图 + 成本vs市值柱状图** |

## 设计

### 1. 后端：`GET /api/reports/holdings`（reports.c 新增 handler）

路由注册：`main.c` 增加 `report_holdings` 前向声明 + `csilk_app_get(app, "/api/reports/holdings", report_holdings)`。

**响应结构**：

```json
{
  "summary": {
    "total_market_value": 2500.0,     // Σ current_value（投资类资产）
    "total_cost_basis": 2000.0,       // Σ cost_basis
    "total_floating_pnl": 500.0,      // 市值 − 成本
    "total_realized_pnl": 400.0,      // Σ 各资产已实现盈亏（含股利收入）
    "floating_pct": 25.0              // 总盈亏率 = floating / cost_basis × 100
  },
  "holdings": [{
    "asset_id": 1,
    "name": "xx基金",
    "asset_type": "fund",             // stock / fund / bond / crypto
    "currency": "CNY",
    "quantity": 1000.0,               // 持有份额/股数
    "net_value": 2.5,                 // 单位净值
    "cost_basis": 2000.0,             // 累计买入成本（含手续费）
    "current_value": 2500.0,          // 市值 = quantity × net_value
    "floating_pnl": 500.0,            // current_value − cost_basis
    "floating_pct": 25.0,             // floating / cost_basis × 100
    "realized_pnl": 400.0             // 该资产累计已实现盈亏
  }]
}
```

**实现要点**：
- 持仓行查询：`assets JOIN categories ON a.category_id=c.id WHERE a.user_id=? AND c.asset_type IN ('stock','fund','bond','crypto')`，复用 `db_get_num` 读取数值列。
- 已实现盈亏聚合：遍历该用户全部 `buy / sell / income` 交易（`ORDER BY transaction_date ASC`，全局时间序在按 asset 分组后保持各资产内时序不变），按 `asset_id` 分组，复用 `report_transaction_performance` 的 PnL 口径：
  - buy：累加 `cost_for_pnl += amount`、`qty += quantity`
  - sell：`realized += amount − qty × avg_cost`（avg_cost = 卖出前 cost_for_pnl / qty），并扣减份额
  - income（分红）：视为成本返还 —— `cost_for_pnl -= amount` **且** `realized += amount`（两个都要，前者影响后续 sell 的 avg_cost，与 performance 完全一致）
  - 手续费不参与 realized 计算（与 performance 一致，成本口径走 cost_basis）
- **除零防护**：`floating_pct` 计算时若 `cost_basis == 0`（空库、全卖光等场景）输出 **0**，避免 0/0=NaN 破坏 JSON。
- 零持仓资产：`quantity=0` 但有交易记录的资产**仍返回**（可能有已实现盈亏需展示），由前端标记呈现。
- 全库空数据时：返回空 `holdings` 数组 + 全 0 summary（floating_pct=0 兜底），不报错。
- 响应走 `respond_ok(c, resp)`，用户校验走 `jwt_get_user_id`。

### 2. 前端：持仓页面 `Holdings.vue`

**路由与导航**：
- 路由 `/holdings`，name `Holdings`，子路由注册在 `Layout.vue` children（router/index.ts）
- `Layout.vue` 菜单「持仓」插在「资产管理」之后，图标用 Element Plus `TrendCharts`
- `Layout.vue` 的 `pageTitle` map 加 `/holdings: '持仓管理'`

**API 层**（`api/reports.ts`）：
- 新增 `HoldingsItem` / `HoldingsSummary` / `HoldingsReport` 接口（遵循现有"报表类型定义放 api/reports.ts"的模式）
- 新增 `holdings: () => http.get<HoldingsReport, HoldingsReport>('/reports/holdings')`

**页面布局**（`views/Holdings.vue`）：

```
┌──────────────────────────────────────────────┐
│ 持仓管理                          [刷新按钮]  │
├──────────────┬──────────────┬───────────────┤
│ 总市值        │ 总浮动盈亏    │ 总已实现盈亏    │
│ ¥25,000      │ +¥500  ▲     │ +¥400        │
├──────────────┴──────────────┴───────────────┤
│ ┌───────────┐  ┌──────────────────────────┐ │
│ │ 环形图     │  │ 柱状图: 成本 vs 市值      │ │
│ │ 类型占比   │  │ (按资产)                 │ │
│ └───────────┘  └──────────────────────────┘ │
├──────────────────────────────────────────────┤
│ 持仓表格 (列见下)                             │
└──────────────────────────────────────────────┘
```

- **汇总卡片 ×3**：总市值 / 总浮动盈亏 / 总已实现盈亏。盈亏卡片按正负着色（红涨绿跌，与 Assets.vue 的 `income-text`/`expense-text` 语义一致：盈利 #10b981、亏损 #ef4444），带 ± 前缀。
- **环形图**（ECharts pie）：按 `asset_type` 汇总市值占比，四种类型（stock/fund/bond/crypto）配色固定。
- **柱状图**（ECharts bar）：按资产横向对比「成本 vs 市值」双系列。
- **持仓表格**列：

| 列 | 说明 |
|---|---|
| 名称 | 资产名 + asset_type 标签（📈股票 📊基金 💹债券 🪙加密） |
| 类型 | el-tag 显示资产类型中文 |
| 份额 | quantity，单位按类型（股/份） |
| 净值 | net_value (4位小数) |
| 成本 | cost_basis |
| 市值 | current_value（= 净值×份额） |
| 浮动盈亏 | floating_pnl，红绿着色 + ± |
| 盈亏率 | floating_pct，红绿着色 |
| 已实现盈亏 | realized_pnl，红绿着色 |

- **空态**：无投资资产时显示 el-empty「暂无持仓数据」。
- **错误韧性**：`onMounted` try/catch 包裹（遵循 AGENTS.md 标准），失败仅 console.error 不炸树。
- **图表响应式**：`window.resize` 监听触发 `chart.resize()`（参照 Reports.vue 现有 ECharts 用法）。

### 3. 范围外（不做）

- Assets.vue 不动：投资列保留，持仓页为新增视图
- 无外部行情 API、无自动刷新（手动刷新按钮）
- 不做持仓导出/详情下钻（YAGNI）

### 4. 测试计划

**后端（test_link.sh 追加）**：

| 测试 | 步骤 | 断言 |
|---|---|---|
| H1 空态 | 全新用户 GET /reports/holdings | code=0，holdings=[]，summary 全 0 |
| H2 建仓后浮动盈亏 | 买 1000 份×2 元（xx基金），PUT net_value=2.5 | holdings[0].floating_pnl=500，floating_pct=25 |
| H2b 手续费不影响 realized | 带 fee=1 买入 1000 份×2 元，再卖 100 份×2.5 | realized_pnl=250−100×2.0=50（avg_cost 不含 fee，与 performance 一致） |
| H3 卖出后已实现 | 卖 400 份×3 元 | realized_pnl=400；quantity=600 |
| H4 多资产聚合 | 另建股票资产买 100 股×10 元 | holdings 长度 2，summary.total_market_value 正确相加 |
| H5 零持仓资产 | 全卖光后仍返回该资产 | quantity=0 行存在，realized_pnl 保留，floating_pnl=0，floating_pct=0（无 NaN） |
| H6 分红 | income 交易（投资类分类） | realized_pnl 含分红金额 |

**前端**：
- `npm --prefix frontend run build` 0 错误（components.d.ts 自动更新）
- 浏览器实测：图表渲染、红绿着色、空态、菜单跳转

**验证门**：
- `cmake --build backend/build --parallel`
- `npm --prefix frontend run build`
- `cd backend && ./tests/test_link.sh` → 全 PASS

## 风险与边界

- **PnL 口径一致性**：realized 计算必须与 `report_transaction_performance` 完全一致（buy 不含 fee、sell 用 cost_for_pnl 均价、income 作成本返还），避免两张报表数字打架。实现时抽公用辅助函数（若改动 performance 则回归其既有测试）。
- **零持仓资产展示**：可能出现"市值 0、已实现盈亏非 0"的行，前端占比图/柱状图自动忽略（数值 0），表格照常展示。
- **历史数据**：存量投资资产无 quantity/cost_basis 时 floating_pnl=0−0=0，不报错但无意义，与 stock-fund-trading 设计的历史边界一致。