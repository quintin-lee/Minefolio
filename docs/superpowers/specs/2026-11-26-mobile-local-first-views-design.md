# 移动端本地优先视图设计（子项目 E）

**项目**：Minefolio 移动端功能完善  
**子项目**：E — holdings / reports / plans 本地优先 + 在线刷新  
**日期**：2026-11-26  
**状态**：设计待批准 → 进入 spec-review

---

## 1. 目标

把移动端目前"纯在线、断网即报错"的三个页面改为**本地优先（local-first）+ 在线刷新**：

1. **ReportsMobile**：月度报表完全由本地 SQLite 聚合，离线可用。
2. **HoldingsMobile**：持仓数据由本地资产/分类镜像派生，离线可读（部分字段为近似值，明确标注）。
3. **PlansMobile**：定投计划/现金流日历展示上次成功的在线响应缓存，离线可读；写操作保持在线，并明确提示离线不可用。
4. 汇率等行情数据做本地缓存，标注陈旧时间，不使用过期数据静默伪装为实时。

---

## 2. 现状回顾

| 页面 | 当前数据源 | 断网表现 | 本地可派生性 |
|------|-----------|---------|-------------|
| `ReportsMobile.vue:28-45` | `dailyExpensesApi.monthly(y,m)` | `loadError` 警告，图表空白 | ✅ 完全可从 `daily_expenses` + `categories` + `expense_tags` 聚合 |
| `HoldingsMobile.vue:66-68,94-108` | `reportsApi.holdings()` + `marketApi.getExchangeRates()` | `loadError` 警告 | 🟡 部分可从 `assets` + `categories` 派生；`realized_pnl` 无法精确还原 |
| `PlansMobile.vue:208-211` | `dcaApi.listPlans`、`listPendingExecutions`、`cashflowApi.getCalendar`、`assetsApi.list` | 加载失败/空列表 | 🟡 仅能缓存上次在线响应；DCA/现金流无本地表 |
| `PlansMobile.vue:240,257,270,281,300,347` | 在线写：`confirmExecution`/`skipExecution`/`setPlanStatus`/`deletePlan`/`confirmIncome`/`createPlan` | 直接失败 | ❌ 未接入 `offlineApi`，本子项目不离线化写操作 |

**已有本地镜像（D 子项目）**：`LOCAL_SCHEMA` 覆盖 `assets`、`categories`、`daily_expenses`、`expense_tags`、`transactions` 等（`db/schema.ts:12-151`），可按 `ledger_id = ? OR ledger_id = 0` 过滤（与其他移动端视图一致）。

---

## 3. 关键设计约束

### 3.1 本地优先的统一加载模式

三个页面统一采用：

```
onMounted → renderLocal()（立即读本地 SQLite，无网络）
          → refreshRemote()（尝试在线拉取，成功后写本地缓存并重新 renderLocal）
          → 失败时保留本地数据 + 显示"离线/缓存"标记，不弹阻塞错误
```

与现有 `AssetsMobile.vue:120-161` 的 online-first-with-local-fallback 不同，E 子项目是**先本地后在线**，避免首屏空白。

### 3.2 报表本地聚合口径（必须与后端一致）

`ExpenseMonthly`（`types/index.ts:274-283`）字段口径：

- `total_income` / `total_expense`：`SUM(amount)` 按 `expense_type='income'|'expense'`；
- `balance = total_income - total_expense`；
- `by_category`：按 `category_id` 分组，名称取 `categories.name`，`pct = amount / total_expense * 100`（保留 1 位小数）；
- `by_tag`：通过 `expense_tags` 关联 `tags`，`count` 为该标签在当月的支出条数；
- `daily_breakdown`：按 `expense_date`（YYYY-MM-DD）分组的 income/expense。

查询过滤：`daily_expenses.__deleted = 0`、`expense_date LIKE 'YYYY-MM-%'`、`ledger_id = ? OR ledger_id = 0`；`by_tag` 关联时**必须**同时过滤 `expense_tags.__deleted = 0`（该表带软删列，见 `db/schema.ts:110-115`），否则已删除的标签关联会被计入统计。

### 3.3 持仓本地派生的边界（必须诚实标注）

服务端持仓只取 `c.asset_type IN ('stock','fund','bond','crypto')` 的资产（`backend/src/infrastructure/repositories/portfolio_repo_impl.c:32-38`），并由 `mf_portfolio_rule_apply_trade_events` 重放交易事实计算 `realized_pnl` 与 `current_value`（`backend/src/application/portfolio/usecases.c:197-200`），汇总还按 `?currency=` 基准币用汇率表跨币种折算（`:202-210`）。本地无法完整重放该逻辑，因此：

- **必须过滤**：join `categories` 后只保留 `asset_type IN ('stock','fund','bond','crypto')`，**排除现金/银行卡等其他资产**；
- 可精确派生：`name`、`asset_type`、`currency`、`quantity`、`net_value`、`cost_basis`；
- 近似派生：`current_value = quantity * net_value`（不使用可能滞后的本地 `assets.current_value`）、`floating_pnl`、`floating_pct`；多币种汇总直接按原币累加，未做服务端折算；
- `realized_pnl` 离线时置 0 并在 UI 显示"离线近似，已实现盈亏未更新"；
- 汇率：`marketApi.getExchangeRates()` 成功时缓存到 `sync_meta`（key `exchange_rates_json` + `exchange_rates_at`），离线时读取缓存并展示缓存时间；
- 在线拉取成功后用服务端 `HoldingsReport` 覆盖本地派生值（服务端口径优先），并清除近似标记。

### 3.4 定投/现金流仅做读缓存

- 本地新增轻量表 `view_cache`（`key TEXT PRIMARY KEY`、`payload TEXT`、`updated_at TEXT`），存三个键：`dca_plans`、`dca_executions`、`cashflow_calendar`；
- 在线成功时 `INSERT OR REPLACE` 缓存；离线时从缓存读取并展示"缓存于 <时间>"；
- **写操作不在本子项目离线化**：`offlineApi` 未注册 `/dca`、`/cashflow` 路径，`SyncEngine` 也未注册对应表；离线时点击写按钮给出明确提示，不产生队列项。

### 3.5 不新增同步表

`view_cache` 与 `sync_meta` 中的汇率缓存**不进入 `sync_queue`**、不参与 `SyncEngine`，仅作为 UI 读缓存；登出时随 `resetLocalDb()` 清空。

---

## 4. 架构设计

### 4.1 新增模块

| 模块 | 路径 | 职责 |
|------|------|------|
| `useLocalMonthlyReport` | `frontend/src/composables/mobile/useLocalMonthlyReport.ts` | 从本地 SQLite 聚合 `ExpenseMonthly` |
| `useLocalHoldings` | `frontend/src/composables/mobile/useLocalHoldings.ts` | 从本地资产/分类派生 `HoldingsReport`（近似） |
| `useViewCache` | `frontend/src/composables/mobile/useViewCache.ts` | `view_cache` 读写与陈旧标记 |

**目录说明**：`frontend/src/composables/mobile/` 当前不存在（现有 `composables/` 下只有 `chat/` 与若干单文件），实施时需新建该目录。

### 4.2 本地聚合函数签名

```ts
export function aggregateMonthlyReport(year: number, month: number): ExpenseMonthly
export function deriveLocalHoldings(): { report: HoldingsReport; approximate: boolean }
export function loadViewCache<T>(key: string): { data: T | null; updatedAt: string | null }
export function saveViewCache<T>(key: string, data: T): void
```

### 4.3 页面改造

| 页面 | 改动 |
|------|------|
| `ReportsMobile.vue` | `load()` 先 `aggregateMonthlyReport()` 渲染；再 `dailyExpensesApi.monthly()` 成功后缓存并刷新；失败保留本地数据并显示"离线报表（本地计算）"标记。保留 `reqId` 竞态保护（`ReportsMobile.vue:37-45`） |
| `HoldingsMobile.vue` | 先 `deriveLocalHoldings()` 渲染；再 `Promise.allSettled([reportsApi.holdings(), marketApi.getExchangeRates()])` 刷新；离线时保留派生结果并显示近似标记与汇率缓存时间 |
| `PlansMobile.vue` | `load()` 先读 `view_cache`；再并行拉取 4 个在线接口，成功后写缓存；离线时保留缓存数据；`assetsApi.list()`（`PlansMobile.vue:211`）仅用于资产名展示，离线时改为读本地 `assets` 镜像；所有写按钮在 `!navigator.onLine` 时禁用并提示"定投/现金流操作需联网" |

### 4.4 UI 标记

统一使用一个轻量标记组件或在页面 header 内 inline `<span class="cache-pill">`：

- `离线报表（本地计算）`
- `离线近似 · 已实现盈亏未更新`
- `缓存于 2026-11-26 13:20`

不得用 `net-status-pill` 的样式（那是 `SyncStatusBanner` 的职责），避免视觉语义混淆。

---

## 5. 错误处理

| 场景 | 处理 |
|------|------|
| 本地库未初始化 | `initLocalDb()` 由 bootstrap 保证；读取异常时返回空数据而非抛错 |
| 在线拉取失败但有本地数据 | 保留本地数据 + 显示缓存/近似标记，不弹 `notifyMobileReadError` 阻塞提示 |
| 在线拉取失败且无本地数据 | 显示空状态 + "当前离线且无本地缓存"，调用 `notifyMobileReadError` 一次 |
| 汇率缓存过期 | 使用缓存值但展示缓存时间；不静默当作实时汇率 |
| 定投/现金流离线写 | 按钮禁用 + 提示，不入队、不产生冲突项 |

---

## 6. 测试计划

### 6.1 单元测试（vitest）

- `aggregateMonthlyReport`：构造当月 daily_expenses（含 income/expense、多分类、多标签、跨月数据），断言 totals/by_category/by_tag/daily_breakdown 与预期一致；被软删/其他账本行被排除；`expense_tags.__deleted=1` 的关联不计入 `by_tag`；
- `deriveLocalHoldings`：构造投资类（stock/fund/bond/crypto）与现金类资产行，断言仅投资类被纳入、`current_value=quantity*net_value`、floating_pnl/pct、realized_pnl=0 且 `approximate=true`；
- `useViewCache`：读写/覆盖/重载后保留；不进入 `sync_queue`。
- 回归：E2E（Playwright）断网 → 三页仍有数据且标记可见 → 恢复网络 → 在线数据覆盖本地。

### 6.2 回归命令

`npm test`、`npm run typecheck`、`npm run build:mobile`、`npx playwright test`。

---

## 7. 实施顺序

1. **阶段一**：新增 `view_cache` 表与三个 composable，补单测。
2. **阶段二**：改造 `ReportsMobile`、`HoldingsMobile`（本地优先 + 在线刷新 + 标记）。
3. **阶段三**：改造 `PlansMobile`（读缓存 + 写按钮离线禁用），补 E2E 与全量回归，逐项提交。

---

## 8. 与后续子项目的关系

- **A/B**：本子项目依赖其已落地的本地资产/交易/分类镜像；A/B 的写入口产生的本地变更会立即反映在 Reports/Holdings 的本地聚合中。
- **C**：聊天页草稿与本子项目的 `view_cache` 相互独立。
- **D**：沿用 D 的本地库与账本隔离规则；`view_cache` 不进入 `SyncEngine`。

**范围外（需另立子项目）**：DCA/现金流的离线写入（需新增本地表 + `offlineApi` 路径注册 + `SyncEngine` 表注册）、服务端持仓盈亏的精确离线还原、行情实时推送。
