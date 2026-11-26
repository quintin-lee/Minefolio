# 移动端本地优先视图实现计划 (Mobile Local-First Views Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 ReportsMobile / HoldingsMobile / PlansMobile 从"纯在线、断网报错"改为"本地优先 + 在线刷新"，断网时展示本地计算或缓存数据并明确标注近似/缓存状态；定投与现金流写操作保持在线且离线时禁用。

**Architecture:**
1. **本地聚合**：`useLocalMonthlyReport` 从 `daily_expenses` / `categories` / `expense_tags` / `tags` 完全本地聚合 `ExpenseMonthly`；`useLocalHoldings` 从 `assets` + `categories` 派生持仓，但仅纳入 `asset_type IN ('stock','fund','bond','crypto')`，`current_value` 用 `quantity * net_value` 近似，`realized_pnl=0`。
2. **读缓存**：新增 SQLite `view_cache` 表，缓存 `dca_plans` / `dca_executions` / `cashflow_calendar` 与汇率；不进入 `SyncEngine` / `sync_queue`。
3. **统一加载模式**：先 `renderLocal()`，再 `refreshRemote()`；在线失败保留本地数据并显示缓存/近似标记，不弹阻塞错误。
4. **PlansMobile 写操作**：不在本计划离线化；离线时禁用按钮并提示。

**Tech Stack:** Vue 3, Pinia, TypeScript, sql.js, Vitest, Playwright.

**Spec:** `docs/superpowers/specs/2026-11-26-mobile-local-first-views-design.md`（spec-review 已修订）。

## Global Constraints

- 不修改 `SyncEngine` / `sync_queue` / `offline-http.ts`；`view_cache` 与汇率缓存不进入同步队列。
- 所有本地查询按 `ledger_id = ? OR ledger_id = 0` 过滤，并过滤 `__deleted = 0`（含 `expense_tags.__deleted`）。
- 报表聚合口径与 `ExpenseMonthly`（`types/index.ts:274-283`）完全一致；`pct` 保留 1 位小数。
- 持仓派生必须只纳入 `stock/fund/bond/crypto`；`current_value = quantity * net_value`；`realized_pnl = 0` 且 `approximate = true`。
- 在线数据优先：拉取成功后覆盖本地派生/缓存并清除近似标记。
- 不引入新的 `notifyMobileReadError` 弹窗风暴：有本地数据时静默保留并标记。

## Review Focus

1. 月度报表本地聚合与服务端 `expenseMonthly` 口径一致（软删、跨月、账本、按标签）。
2. 持仓页面不把现金/银行卡算入投资持仓；近似标记可见且在线成功后消失。
3. 断网时三页均有内容或明确空状态，不再白屏/报错。
4. PlansMobile 离线写按钮禁用，且不产生队列项。

---

## Task 1: view_cache 表与本地聚合 composable

**Files:**
- Modify: `frontend/src/db/schema.ts`（新增 `view_cache` 表）
- Create: `frontend/src/composables/mobile/useLocalMonthlyReport.ts`
- Create: `frontend/src/composables/mobile/useLocalHoldings.ts`
- Create: `frontend/src/composables/mobile/useViewCache.ts`
- Create: `frontend/tests/local-first-views.spec.ts`

**Details:**
1. `LOCAL_SCHEMA` 追加：
   ```sql
   CREATE TABLE IF NOT EXISTS view_cache (
     key TEXT PRIMARY KEY,
     payload TEXT NOT NULL,
     updated_at TEXT NOT NULL
   );
   ```
   不参与 `SyncEngine`；`resetLocalDb()` 时随整库清空。
2. `useViewCache`：`loadViewCache<T>(key): { data, updatedAt }`、`saveViewCache<T>(key, data): void`（upsert + `persist()`）。
3. `useLocalMonthlyReport.aggregateMonthlyReport(year, month)`：
   - 从 `daily_expenses` 取 `__deleted=0`、日期前缀 `YYYY-MM-`、账本过滤；
   - `total_income`/`total_expense`/`balance`；
   - `by_category`：join `categories`，`pct = amount / total_expense * 100` 保留 1 位；
   - `by_tag`：join `expense_tags`（`__deleted=0`）与 `tags`，`count` 为当月支出条数；
   - `daily_breakdown`：按 `expense_date` 分组。
4. `useLocalHoldings.deriveLocalHoldings()`：
   - join `categories`，仅 `asset_type IN ('stock','fund','bond','crypto')`；
   - `current_value = quantity * net_value`、`floating_pnl = current_value - cost_basis`、`floating_pct`；
   - `realized_pnl = 0`，返回 `{ report, approximate: true }`。
5. 单测：聚合口径（含软删标签、跨月、账本）、持仓投资类过滤与近似字段、cache 读写/重载保留/不进 `sync_queue`。

- [ ] **Step 1: 新建 `view_cache` 表与三个 composable**
- [ ] **Step 2: 编写 `local-first-views.spec.ts` 单测**
- [ ] **Step 3: 运行 `npm test -- tests/local-first-views.spec.ts` 与 `npm run typecheck`**

---

## Task 2: ReportsMobile 本地优先

**Files:**
- Modify: `frontend/src/views-mobile/ReportsMobile.vue`

**Details:**
1. `load()` 改为：先 `aggregateMonthlyReport(y, m)` 渲染本地结果；再 `dailyExpensesApi.monthly(y, m)` 成功后覆盖并清除标记；失败时若有本地数据则保留并显示"离线报表（本地计算）"标记，无本地数据才走 `notifyMobileReadError`。
2. 保留 `loadReqSeq` 竞态保护与月份切换逻辑（`ReportsMobile.vue:37-45`）。
3. `ExpenseCategoryPie` 父容器仍需固定高度（`ReportsMobile.vue` 的 `.pie-wrap{height:300px}`）。

- [ ] **Step 1: 改造 `load()` 为本地优先 + 在线刷新**
- [ ] **Step 2: 添加离线/本地计算标记**
- [ ] **Step 3: 运行 typecheck 与相关单测**

---

## Task 3: HoldingsMobile 本地优先

**Files:**
- Modify: `frontend/src/views-mobile/HoldingsMobile.vue`

**Details:**
1. `onMounted` 改为先 `deriveLocalHoldings()` 渲染；再 `Promise.allSettled([reportsApi.holdings(), marketApi.getExchangeRates()])`。
2. 持仓在线成功 → 用服务端 `HoldingsReport` 覆盖并清除近似标记；失败 → 保留本地派生结果并显示"离线近似，已实现盈亏未更新"。
3. 汇率在线成功 → `saveViewCache('exchange_rates', rates)`；离线 → `loadViewCache` 读取并在 UI 展示缓存时间（key `exchange_rates`）。
4. 保留现有 `pnlClass`/`typeLabel`/汇总卡片与错误提示行为。

- [ ] **Step 1: 改造持仓加载为本地优先 + 在线覆盖**
- [ ] **Step 2: 接入汇率缓存与缓存时间显示**
- [ ] **Step 3: 运行 typecheck 与相关单测**

---

## Task 4: PlansMobile 读缓存与离线写禁用

**Files:**
- Modify: `frontend/src/views-mobile/PlansMobile.vue`

**Details:**
1. `load()` 先 `loadViewCache()` 渲染 `dca_plans` / `dca_executions` / `cashflow_calendar`；再并行拉取 4 个在线接口，成功后 `saveViewCache()`；失败保留缓存并显示"缓存于 <时间>"。
2. `assetsApi.list()`（`PlansMobile.vue:211`）仅用于资产名展示：在线时照常拉取；离线时从本地 `assets` 表按 `__deleted=0 AND (ledger_id=? OR ledger_id=0)` 读取。
3. 写按钮（confirm/skip/status/delete/confirmIncome/createPlan，`PlansMobile.vue:240,257,270,281,300,347`）统一在 `!navigator.onLine` 时禁用并提示"定投/现金流操作需联网"；**不调用 `offlineApi`**，不产生队列项。
4. 保留现有 UI/交互（Tab、弹窗、确认框）。

- [ ] **Step 1: 改造 `load()` 为缓存优先 + 在线刷新**
- [ ] **Step 2: 资产名离线读本地镜像**
- [ ] **Step 3: 写按钮离线禁用与提示**
- [ ] **Step 4: 运行 typecheck 与相关单测**

---

## Task 5: E2E 与全量验证

**Files:**
- Create: `frontend/e2e/mobile-local-first-views.spec.ts`

**Details:**
1. E2E（Playwright + 请求拦截）：断网 → 打开 `/m/reports`、`/m/holdings`、`/m/plans` → 三页均有数据且缓存/近似标记可见 → 恢复网络 → 在线数据覆盖本地、标记消失。
2. 回归：`npm test`、`npm run typecheck`、`npm run build:mobile`、`npx playwright test`。

- [ ] **Step 1: 编写 E2E 用例**
- [ ] **Step 2: 全量单测、typecheck、移动端构建与 E2E 通过**

---

## Verification Checklist

- [ ] 报表聚合口径与 `ExpenseMonthly` 一致；软删标签/跨月/账本过滤正确
- [ ] 持仓仅含投资类资产；现金/银行卡不出现；近似标记在线成功后消失
- [ ] 汇率缓存展示缓存时间
- [ ] 断网时三页不白屏、不弹阻塞错误
- [ ] PlansMobile 离线写按钮禁用且不产生队列项
- [ ] `view_cache` / 汇率缓存不进入 `sync_queue`
- [ ] `npm test` / `npm run typecheck` / `npm run build:mobile` / `npx playwright test` 全绿
