# 持仓管理数据展示设计方案（对标支付宝 / 天天基金）

> 本文件为**纯设计提案**，不含代码实现。目标是在现有 DDD 四层 C 架构 + Vue 3 前端之上，扩展一个接近支付宝/天天基金体验的「持仓总览 + 单只详情 + 组合分析」数据展示体系。

---

## 1. 现状盘点（已具备的能力）

设计前先明确"我们已经有什么"，避免重复造轮子。

### 1.1 后端已有

| 能力 | 位置 | 说明 |
|------|------|------|
| 资产字段 | `assets` 表 + `domain/asset/entity.h` | `quantity` / `cost_basis` / `net_value` / `current_value` / `realized_pnl` / `unrealized_pnl` / `cost_method` |
| 估值对象 | `domain/asset/valuation.h` | `mf_valuation_calculate`（市值 = 数量 × 单价，固定点） |
| 损益对象 | `domain/asset/pnl.h` | `mf_pnl_calculate`（已实现 / 浮动 / 总盈亏 / 收益率） |
| 组合聚合 | `domain/portfolio/portfolio.h` + `portfolio.c` | 多币种折算、资产配置权重 `weight`、集中度 HHI、最大单一持仓、浮动/已实现/总盈亏、回报率 |
| 持仓投影 | `domain/asset/position.h` | `mf_position_t`（数量+成本+估值+损益），由 ledger 事件流重放派生 |
| 账本快照 | `ledger_snapshots` 表 + `ledger_snapshot_repo_impl.c` | 每 (user, asset, snapshot_date) 的 checkpoint；级联失效 + 增量重建（V020） |
| 账本引擎 | `core/ledger/ledger_engine.c` | `ledger_rebuild_position / _account / _portfolio`，从最近 checkpoint 增量重放 |
| 价格历史 | `asset_price_history` 表 + `market_repo_impl.c` | `record_price_history` / `price_history_list`（默认 90 天） |
| 行情索引 | `market_indices` 表 | 大盘指数 + sparkline（`current_price`/`change_percent`/`prev_close` 等） |
| 汇率 | `exchange_rates` + `exchange_rate_history` + 内存表 | 多币种折算、历史汇率回看（`exchange_rate_get_historical_or_latest`） |
| 交易事实 | `transactions` + `transaction_lots`（FIFO 批次） | 成本基础、已实现盈亏 |
| 分红 | `daily_expense` / 分红相关 | 已实现盈亏含现金分红 |
| 报表服务 | `services/report_asset_service.c` | 净资产趋势、资产分布、多币种汇总、汇率损益（`report_usecase_*`） |
| 报表用例 | `application/report/usecases.c` | 资产趋势 / 分布 / 汇总 / 多币种 / 汇率损益 5 个 asset 报表 + 6 个 expense 报表 |

### 1.2 前端已有

- `Reports.vue` 已有 3 个 tab：收支分析、资产分析（净资产趋势 + 分布饼图）、外汇损益。
- `Dashboard.vue` 展示资产总览卡片。
- `market_api` 已有 `getFxHistory`、`getIndices`、`priceHistory`。
- `reports_api` 已有 `assetTrend / assetBreakdown / assetSummary / multiCurrency / fxPnl`。

**结论：核心数据底座（固定点、账本、快照、价格历史、汇率、行情）已齐备，本方案主要做「派生指标补全 + 展示编排 + 单只详情页」三层，而非推倒重来。**

---

## 2. 对标支付宝/天天基金的持仓数据维度

按"支付宝持仓页 → 单只基金详情 → 组合分析"三层拆解。

### 2.1 第一层：持仓总览（Overview / Dashboard 升级）

对标支付宝「总资产」首页。数据点：

| 指标 | 计算方式 | 现有支撑 | 是否需新增 |
|------|---------|---------|-----------|
| 总资产（净资产） | 非负债资产合计 − 负债合计（折算基准币） | `report_asset_summary` / `mf_portfolio` | 复用 |
| 累计收益（综合总盈亏） | Σ(realized + unrealized) − Σ(cost_basis) | `mf_pnl.total_pnl` | 复用 |
| 当日收益 | 当前市值 − 前一日市值 | `asset_price_history` 前一日 | **新增** |
| 当日盈亏率 | 当日收益 / 前一日市值 | 同上 | **新增** |
| 收益走势曲线（近30d/90d/1y） | 5~30 个采样点 | `report_asset_trend` | 复用 |
| 组合资产配置占比（饼图） | Σ by category 市值 | `report_asset_breakdown` | 复用 |
| 资产类别分布 | 股票/基金/债券/加密/现金 | `report_asset_breakdown` + `categories.asset_type` | 复用 |
| 集中度警示（HHI / 最大单一持仓） | `mf_portfolio.hhi` | 已有领域计算 | 复用（前端未展示） |

### 2.2 第二层：单只持仓详情（Position Detail）

对标基金详情页。新增一个"单只资产详情"视图，数据点：

| 指标 | 计算方式 | 现有支撑 | 是否需新增 |
|------|---------|---------|-----------|
| 持有份额 / 摊薄成本（均价） | `quantity` / `cost_basis`；均价 = cost_basis/quantity | `assets` | 复用 |
| 最新净值 / 现价 | `net_value` | 复用 | 复用 |
| 当日涨跌额 / 涨跌幅 | `change_percent` / `change_amount` | `market_indices`（股票类）；基金类需行情同步 | **新增字段到 asset 级** |
| 市值 | `quantity × net_value` | `mf_valuation.market_value` | 复用 |
| 浮动盈亏（绝对 / 百分比） | `mf_pnl.unrealized_pnl / .unrealized_pct` | 复用 | 复用 |
| 已实现盈亏（含分红） | `mf_pnl.realized_pnl` | 复用 | 复用 |
| 总回报率 | `mf_pnl.total_return_pct` | 复用 | 复用 |
| 持仓占比（占组合权重） | `mf_portfolio_item.weight` | 复用 | 复用 |
| **区间收益率（近1M/3M/6M/1Y/ALL）** | 见 §3.3 | 价格历史 + 快照 | **新增** |
| **每日收益走势曲线（本标的）** | `asset_price_history` 按日 | 已有表 | **新增展示** |
| 成本曲线 / 分红记录 | 交易事实 + 分红 | `transaction_lots` / daily_expense | 部分 |
| 行情 sparkline | `market_indices.sparkline` | 已有（指数级） | 可扩展到资产级 |

### 2.3 第三层：组合分析（Portfolio Analytics）

对标天天基金"持仓分析 / 智能诊断"。

| 指标 | 说明 | 现有支撑 |
|------|------|---------|
| 资产配置权重 | 各类别占比 | `mf_portfolio.items[].weight` |
| 集中度 HHI | 风险预警（越接近 1 越集中） | 已有领域计算，**前端未暴露** |
| 最大单一持仓占比 | 重仓提示 | 已有 |
| 资产自身盈亏 vs 汇率盈亏拆解 | 多币种场景 | `report_fx_pnl` 已做 |
| 净值/市值趋势 | 组合走势 | `report_asset_trend` |
| **夏普比率 / 最大回撤 / 波动率** | 基金类风险指标 | **需新增**（依赖价格历史序列） |
| **同类基金对比**（可选） | 对标指数 | **需新增**（拉外部指数） |
| **估值高低提示**（PE/PB 百分位） | 股票类 | **需新增**（依赖外部数据源） |

> 第三层的"风险指标/估值百分位/同类对比"属于**进阶能力**，建议放到二期（见 §7）。一期先做前两层 + HHI/最大持仓暴露。

---

## 3. 数据模型与计算方案（一期核心）

### 3.1 当日收益（Daily P&L）

**问题**：`assets.current_value` 是当前市值，但"当日收益"需要"昨日市值"。

**方案（复用 `asset_price_history` + `ledger_snapshots`）**：
- 行情同步任务（`market_usecase_sync_all` → `record_price_history`）每日 upsert 一条 `asset_price_history(asset_id, price_date, price)`。
- 当日市值 = `quantity × net_value(今日)`；昨日市值 = `quantity × net_value(前一交易日)`（从 `asset_price_history` 取 `price_date < today` 最新一行；若当日有新增交易，昨日市值还要扣除今日新增持仓部分的成本，严格做法用 `ledger_snapshots` 的 `tx_checkpoint` 对齐）。
- **精确版（推荐）**：当日收益 = `ledger_rebuild_position` 到「今日」的 `current_value` − 「昨日 checkpoint」的 `current_value`。`ledger_snapshots` 已支持按 `snapshot_date` 取 checkpoint，无需重放全量。

> 设计要点：当日收益分**两个来源**——① 行情驱动的市值变动（价格波动）；② 当日交易/分红驱动的变动（资金进出）。总当日收益 = 两者之和。前者用价格历史，后者用 `asset_balance_logs` 当日 `delta` 之和（现有 `report_asset_summary` 已对 30d 做过，改造成 daily 即可）。

### 3.2 单只资产区间收益率（Interval Returns）

**输入**：`asset_price_history`（日频净值序列）+ 期初成本。
**输出**：近1M / 3M / 6M / 1Y / ALL 的收益率。

计算规则：
```
区间收益 = (区间末净值 − 区间初净值) / 区间初净值 × 100%
区间初净值 = 区间起点最近的 price_date ≤ 目标日 的 price
```
- 货币/现金类资产：用 `daily_expense` 的七日年化（若做货币基金类型）；默认现金类收益率为 0。
- 股票/基金/加密：走价格历史。
- 加密 7×24 交易日，基金 T+1 确认，股票按交易日——**区间端点取价**统一用「目标日 ≤ 的最近一行价格历史」，无需区分交易日历（对自管数据足够近似；若要严格，二期引入交易日历）。

**领域层落点**：新建 `domain/portfolio/performance.h/.c`（或并入 `domain/asset/pnl.c`），纯函数：
```c
typedef struct {
    double ret_1m, ret_3m, ret_6m, ret_1y, ret_all;  // 百分比
    bool   valid_1m, valid_3m, valid_6m, valid_1y, valid_all; // 数据是否足够
} mf_interval_returns_t;

int mf_interval_returns_from_series(
    const price_point_t* series, size_t n, double cost_at_start,
    mf_interval_returns_t* out);
```
其中 `price_point_t { char date[16]; price_t price; }`。
- 用例层做 128-bit 精度：从价格历史读取 → 在 `decimal_t` 上计算各区间百分比 → `mf_round_to_decimals` 转 double 到 JSON。
- 前端：`asset_usecase_get` 响应体追加 `interval_returns` 对象。

### 3.3 每日收益走势曲线（本标的）

- 直接查 `asset_price_history` 按日序列（`price_history_list` 已支持 limit=90，扩展到 365）。
- 若资产有当日资金进出，叠加 `asset_balance_logs` 的 delta，得到"市值 + 资金流"完整净值曲线。
- 前端 ECharts 折线图（复用 `AssetTrendLine` 组件模式，新增单标的版本 `AssetPriceLine`）。

### 3.4 持仓占比 & 集中度暴露

- `report_usecase_asset_breakdown` 已按 category 聚合市值 + `pct`。
- `mf_portfolio` 已算 `weight` / `hhi` / `max_holding_weight`。
- **一期新增**：在 `report_usecase_asset_summary`（或新 `report_usecase_position_overview`）响应体追加 `hhi`、`max_holding_asset`、`max_holding_weight`，前端 Dashboard 增加一行「集中度提示」。

---

## 4. 分层落点（DDD 四层）

遵循现有严格单向依赖（`interfaces → application → domain/infrastructure`）。

### 4.1 Domain（纯 C，零依赖）

| 新增/复用 | 文件 | 职责 |
|-----------|------|------|
| 复用 `mf_pnl_calculate` | `domain/asset/pnl.c` | 浮动/已实现/总盈亏 |
| 复用 `mf_valuation_calculate` | `domain/asset/valuation.h` | 市值 |
| 复用 `mf_portfolio_aggregate` | `domain/portfolio/portfolio.c` | 权重/HHI/集中度 |
| **新增** 区间收益 | `domain/asset/performance.h/.c` | 纯函数：价格序列 → 1M/3M/6M/1Y/ALL 百分比 |
| **新增**（二期）风险指标 | `domain/portfolio/risk.h/.c` | 夏普、最大回撤、波动率 |

> 命名沿用领域层 `mf_*` 前缀，纯 C，仅依赖 `core/financial`。

### 4.2 Infrastructure（SQL）

| 新增/复用 | 文件 | 说明 |
|-----------|------|------|
| 复用 | `market_repo_impl.c` `mf_market_repo_price_history_list` | 价格历史读取 |
| 复用 | `ledger_snapshot_repo_impl.c` `ledger_snapshot_get_latest` | 取昨日 checkpoint |
| **新增** | `market_repo_impl.c` `mf_market_repo_price_history_series` | 按日期区间取完整序列（供区间收益/风险指标） |
| **新增** | `asset_repo_impl.c` `asset_daily_balance_log_sum` | 当日资金流 delta 求和 |

> 所有 SQL 用 `?` 占位 + `csilk_db_query_param_json`；嵌套 JSON 列 `csilk_json_parse` 一次；数值用 `db_get_num/int`。

### 4.3 Application（用例编排）

| 新增/复用 | 文件 | 说明 |
|-----------|------|------|
| 复用 | `application/report/usecases.c` | 资产趋势/分布/汇总 |
| **扩展** | `application/asset/usecases.c` `asset_usecase_get` | 响应体追加 `interval_returns`、`weight`、`daily_pnl`、`daily_change_pct` |
| **新增** | `application/report/usecases.c` `report_usecase_position_overview` | 汇总：当日收益、总收益、HHI、最大持仓、资产配置 |
| **新增** | `application/market/usecases.c` `market_usecase_daily_pnl`（可选） | 单资产当日收益（市值变动 + 资金流） |

编排规则：
- 聚合/算术在 `decimal_t`（128-bit），仅 JSON 边界转 double（`mf_round_to_decimals`）。
- 当日收益 = `ledger_snapshot` 昨日 checkpoint 重放差值（精确）+ 当日 `asset_balance_logs` delta（资金流），两者相加。
- 多币种场景：组合层折算用 `mf_fx_rate_table`（显式汇率表，杜绝隐式 1:1）。

### 4.4 Interfaces（控制器）

| 路由 | 控制器 | 说明 |
|------|--------|------|
| `GET /api/assets/:id` | 复用 `asset_controller` | 响应体扩充字段 |
| `GET /api/reports/position-overview` | **新增** `report_controller` | 持仓总览（当日收益 + HHI + 资产配置） |
| `GET /api/reports/asset-trend` | 复用 | 收益走势 |
| `GET /api/market/price-history?asset_id=&days=` | 复用/扩展 | 单标的曲线 |

> 控制器保持薄层：解析参数 → 调用 usecase → `respond_ok`。

---

## 5. 前端展示设计（Vue 3）

### 5.1 路由 / 视图

| 视图 | 说明 |
|------|------|
| `views/Dashboard.vue`（扩展） | 资产总览：净资产、累计收益、**当日收益卡片**、收益走势曲线、资产配置饼图、**集中度提示条** |
| `views/AssetDetail.vue`（**新增**） | 单只资产详情：份额/摊薄成本、当日涨跌、市值、浮动盈亏、已实现盈亏、**区间收益 tab（1M/3M/6M/1Y/ALL）**、**本标的净值曲线**、交易流水 |
| `views/Reports.vue`（扩展） | 资产分析 tab 追加"持仓占比 + HHI"卡片 |

### 5.2 组件

| 组件 | 来源 |
|------|------|
| `SummaryCard.vue` | 复用 |
| `AssetTrendLine.vue` | 复用（组合级曲线） |
| **`AssetPriceLine.vue`** | 新增（单标的净值曲线，ECharts） |
| **`IntervalReturnTabs.vue`** | 新增（1M/3M/6M/1Y/ALL 区间收益） |
| **`ConcentrationBadge.vue`** | 新增（HHI / 最大持仓提示，阈值配色） |
| `AssetBreakdownPie.vue` | 复用 |

### 5.3 API 层（严格 `api/` 隔离）

| 文件 | 说明 |
|------|------|
| `api/reports.ts` | 新增 `positionOverview()` |
| `api/market.ts` | 新增 `priceHistory(assetId, days)` |
| `api/assets.ts` | 复用 `get(id)`（响应含新字段） |
| `types/index.ts` | 新增 `PositionOverview` / `IntervalReturns` / `AssetDetailResponse` 类型 |

### 5.4 移动端（Capacitor + sql.js）

- 离线 schema `frontend/src/db/` 需镜像新增的 `asset_price_history` / `ledger_snapshots` 读表（**只读展示**，不要求离线写）。
- 移动端 `views-mobile/` 新增对应"持仓总览"卡片 + 单只详情页（复用桌面 API 层，失败回落到本地 sql.js，遵循 `DailyExpensesMobile.vue` 的 fallback 模式，**不掩盖成功响应**）。
- `sync.ts` 已拉取 accounts 先于 assets；本方案不新增写路径，离线只补展示。

---

## 6. 关键设计决策与权衡

1. **当日收益来源二分**（行情市值变动 + 资金流变动）而非单一"昨今市值差"：
   - 单靠昨今市值差，若当日有买入/分红，会把"资金进出"误算成"收益"。
   - 二分法保证"收益"语义纯粹：价格波动是市场行为，资金进出是用户行为。
   - 成本：需同时读 `asset_price_history` + `asset_balance_logs` + `ledger_snapshots`，一次请求聚合。可接受（索引齐全）。

2. **区间收益走价格历史而非全量重放**：
   - `asset_price_history` 日频已足够 1M/3M/6M/1Y；重放全量交易对大组合太重。
   - 二期若需"严格交易日历对齐"再引入日历；一期用"≤目标日最近价格行"近似。

3. **风险指标（夏普/回撤/波动）放二期**：
   - 需要完整净值序列 + 年化基准，一期数据量（90 天默认）不足算稳定夏普。
   - 一期先把价格历史保留期扩到 365 天（`price_history_list` 默认 limit 90→可调参），为二期铺路。

4. **HHI/最大持仓一期就暴露**：
   - 领域层已算好（`mf_portfolio.hhi`），零额外计算成本，纯展示层补一个卡片即可，性价比最高。

5. **多币种折算用显式 `mf_fx_rate_table`**：
   - 遵循现有 `mf_portfolio_aggregate` 的"缺汇率即拒绝"契约（杜绝隐式 1:1），与 `report_multi_currency_summary` 一致。

---

## 7. 分期实施路线（仅排期，不含实现）

| 期 | 范围 | 交付 |
|----|------|------|
| **一期（MVP）** | 持仓总览（净资产/累计收益/当日收益/走势/资产配置饼图/HHI+最大持仓）+ 单只详情（份额/摊薄成本/当日涨跌/市值/浮动盈亏/已实现/总回报/占比/区间收益 1M-ALL/净值曲线/交易流水） | Dashboard 升级 + AssetDetail 新页 + `report_usecase_position_overview` + `asset_usecase_get` 扩展 + 前端组件 |
| **二期** | 风险指标（夏普/最大回撤/波动率）+ 估值百分位（PE/PB）+ 同类基金对标 + 交易日历严格对齐 | `domain/portfolio/risk.c` + 外部数据源接入 |
| **三期（可选）** | 智能诊断/投顾建议（基于 HHI + 回撤 + 估值的规则引擎） | 规则引擎 + 文案模板 |

---

## 8. 风险与注意事项

- **固定点精度**：新增的聚合/百分比计算必须在用例层用 `decimal_t`，禁止 `double` 累加循环（参考 `test_financial_precision_edge` 钉住的回归）。
- **多实例汇率**：`exchange_rate_refresh_all` 是单实例内存表（AGENTS.md 已标注），本方案的当日收益/区间收益折算依赖汇率，多副本部署前需按文档标注的方向改造，否则跨实例不一致。
- **价格历史保留期**：一期默认 90 天，算 1Y 区间收益会缺数据 → 建议把保留期与同步任务保留上限统一提到 365 天（二期彻底解决）。
- **基金 T+1 确认**：区间端点取价若落在非交易日，基金净值尚未确认，前端可加"以最新确认净值为准"提示。
- **`asset_usecase_get` 响应体膨胀**：追加 `interval_returns` / `weight` 等字段对老前端**向后兼容**（新增 key 不影响已有字段），无破坏性变更，无需 `BREAKING CHANGE`。

---

## 9. 结论

现有底座（固定点核心 + 账本引擎 + 快照 checkpoint + 价格历史 + 汇率 + 行情 + 组合聚合 + 报表服务）**已覆盖对标支付宝/天天基金 80% 的数据能力**。本方案一期聚焦三件事：

1. **补全派生指标**：当日收益（行情+资金流二分）、单只区间收益（1M/3M/6M/1Y/ALL）、HII/最大持仓暴露。
2. **新增单只详情页** `AssetDetail` + 对应前端组件。
3. **编排一个 `position-overview` 用例**聚合总览数据。

风险指标 / 估值百分位 / 投顾建议作为二期能力预留，避免一期过度设计。
