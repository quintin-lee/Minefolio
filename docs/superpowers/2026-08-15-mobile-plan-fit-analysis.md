# Mobile 计划与当前前端代码匹配度分析

> 分析对象：`docs/superpowers/plans/2026-08-13-mobile-app-design.md`
> 对照代码：当前 `frontend/`（HEAD `19a4eb3`，工作区干净）
> 结论总览：**整体高匹配（约 90% 复用成立）**，但发现 **2 处必须修订的关键假设**（移动端登录 RSA 相对路径 fetch、http.ts 的 1001 重定向）和 **若干次要适配点**。

---

## 1. 结论摘要（TL;DR）

| 维度 | 匹配度 | 说明 |
|------|--------|------|
| API 层 | ✅ 强匹配 | 计划引用的所有 API 模块/方法均存在且签名一致 |
| 类型层 | ✅ 强匹配 | 计划引用的类型与字段均存在（且比计划假设更丰富） |
| 组件复用 | ✅ 强匹配 | `MonthlyChart.vue` / `NetWorthChart.vue` / `ExpenseCategoryPie.vue` 均存在 |
| 认证/RSA | ⚠️ 有缺口 | 内联 RSA 登录用**相对路径 fetch** `/api/auth/public-key`，Capacitor 独立 WebView 下无法命中后端 |
| http/offline 适配 | ⚠️ 部分适配 | http.ts 的 1001 → `window.location='/login'` 与 ElMessage 交互对移动端不友好，需分支处理 |
| 路由架构 | ✅ 强匹配 | 桌面为 `createWebHistory`；计划用独立移动端入口天然避免双 router 冲突 |
| Vite/构建 | ✅ 匹配 | `@` 别名、ElementPlusResolver 均存在；`vite.config.mobile.ts` 方案成立 |
| 样式主题 | ✅ 强匹配 | 计划引用的全部 CSS 变量已存在于 `styles/index.css` |
| 后端零改动 | ✅ 成立 | 计划引用的所有后端端点均存在 |

**总体判断**：计划的技术选型与当前代码高度吻合，`vite.config.mobile.ts` + 独立 `index.mobile.html` + `/m` 路由的架构完全正确。可行，但需在实施前解决下述 🔴 与 🟡 事项。

---

## 2. API 层（api/）— ✅ 强匹配

| 计划引用 | 实际文件 | 结果 |
|----------|----------|------|
| `summaryApi.get()` → GET /summary | `api/summary.ts` | ✅ |
| `dailyExpensesApi.list({page,page_size})` → PageResult | `api/daily_expenses.ts` | ✅ 还提供 create/update/delete/exportCsv/importCsv |
| `dailyExpensesApi.monthly(year,month)` → ExpenseMonthly | `api/daily_expenses.ts` | ✅ 参数为 `{year, month}` 独立参数 |
| `transactionsApi.list({page_size})` → PageResult | `api/transactions.ts` | ✅ |
| `assetsApi.list({page_size:500})` → PageResult\<Asset\> | `api/assets.ts` | ✅ |
| `categoriesApi.list({type})` → Category[] | `api/categories.ts` | ✅ 供 ExpenseQuickSheet 选分类 |
| 报表接口 | `api/reports.ts`（新增，3586B） | ✅ **计划未充分引用**：此文件含 `expenseMonthly/expenseTrend/expenseCategory/assetTrend/assetBreakdown/transactionPerformance` 等丰富接口，ReportsMobile 可直接复用，优于只调用 `dailyExpenses.monthly` |

> 🟡 **补强建议**：计划的 ReportsMobile 只提到复用 `ExpenseCategoryPie` + `MonthlyChart`。实际 `api/reports.ts` 已提供 `reportsApi.assetTrend/assetBreakdown/expenseTrend` 等，移动端报表 Tab 可直接调用，无需新增后端接口。

---

## 3. 类型层（types/index.ts，137 行）— ✅ 强匹配

计划引用的类型全部存在且字段吻合：

- **Summary**：`total_assets / total_liabilities / net_worth / breakdown / trend` ✅ 精确匹配
- **DailyExpense**：`expense_type / amount / category_name / asset_name / expense_date / note / tags` ✅
- **ExpenseMonthly**：`total_income / total_expense / balance / by_category` ✅。**且更丰富**：额外含 `by_tag`、`daily_breakdown[{date,income,expense}]` —— 计划的 DashboardMobile「本月收支」可直接用 `total_expense/total_income`；若做近期趋势甚至可直接用 `daily_breakdown`。
- **Transaction**：`transaction_type / amount / ...` ✅
- **Category**：含 `asset_type?`、`children?` ✅
- **Asset**：`name / current_value / ...` ✅

> 🟡 计划若假设 `ExpenseMonthly.by_category` 为 `[{name,amount}]`，实际为 `[{name,type,amount,pct}]`——更完整，实现时按此结构取用即可，无需改动。

---

## 4. 组件复用（components/）— ✅ 强匹配（1 处使用注意）

| 计划引用 | 实际 | props | 结果 |
|----------|------|-------|------|
| `MonthlyChart.vue` | ✅ 存在 | `{total_income,total_expense}` \| null；**内高 280px 固定** | ✅ |
| `NetWorthChart.vue` | ✅ 存在 | `{date,net_worth}[]`；**内高 300px 固定** | ✅ |
| `ExpenseCategoryPie.vue` | ✅ 存在 | `{name,amount,pct}[]`；**内 div `height:100%`** | ✅ 但见下 |

> 🟡 **`ExpenseCategoryPie` 特殊**：其图表根 div 当前为 `height: 100%`（其余图表为固定 px）。在 ReportsMobile 复用时**必须给它一个有确定高度的父容器**（如外层 `.chart-wrap{height:280px}`），否则 ECharts 初始化会因 `clientHeight=0` 直接跳过。这一使用约束计划未注明。

另可复用：`AssetCard.vue`、`SummaryCard.vue`、`TransactionTable.vue`、`DailyExpenseForm.vue`、`TagPicker.vue`（记账 Sheet 选 Tag 可用）。

---

## 5. 认证 / RSA — ⚠️ 关键缺口（🔴 必须修订）

计划正确识别：`stores/auth.ts` 的 `encryptPassword` 是模块私有函数（`async function encryptPassword` 未 export），故 LoginMobile 需内联一份拷贝。**该判断成立**（已在 [stores/auth.ts](../frontend/src/stores/auth.ts) 核实：`fetchRsaJwk/encryptPassword` 均为模块级私有）。

**但存在 2 处计划未覆盖的移动端适配问题：**

🔴 **RSA public-key 相对路径 fetch**
```ts
// stores/auth.ts 内联拷贝来源：
const r = await fetch('/api/auth/public-key')
```
桌面端经 Vite proxy 转发到 :8080。但 **Capacitor 独立 WebView 没有 dev proxy**，`/api/...` 相对路径不会命中后端。这意味着：
- 计划描述的「内联 RSA 登录」在**真实打包的移动端上会失败**（public-key 404 / fetch 失败）。
- **必须**：通过 Capacitor 环境/`import.meta.env.VITE_API_URL` 配置**绝对后端 base URL**，让 public-key fetch 与 Axios baseURL 指向同一远端。`http.ts` 已用 `import.meta.env.VITE_API_URL` 作 baseURL，内联的 `fetchRsaJwk` 同样应使用该变量拼接，而非写死相对路径。

🔴 **http.ts 的 1001 重定向**
```ts
// utils/http.ts 响应拦截器
if (code === 1001) { useAuthStore().logout(); window.location.href = '/login' }
```
- 移动端应为 **`/m/login`**，且 `window.location.href` 在 WebView 中会把页面导航离开 App 壳。
- `offlineApi` 若直接包 `http`，鉴权失败仍会走这段桌面逻辑。
- **建议**：`offlineApi`/移动端 auth 失败处理需与桌面分支，或把 baseURL + loginPath 参数化。

---

## 6. HTTP 层（utils/http.ts）— ⚠️ 部分适配（🟡）

- 基于 **Axios**（非原生 fetch）✅，符合计划「`http.get` + blob 导出」与 `offlineApi` 包装假设。
- 请求拦截器注入 `Bearer token` + 从 `document.cookie` 读 CSRF ✅（WebView 中若 cookie 正常即可）。
- 响应拦截器解包 `{code,message,data}` ✅。
- 🟡 **offline 交互冲突**：网络失败分支无条件 `ElMessage.error('网络错误')`。离线记账时每次失败都会弹桌面式 toast，随后 `offlineApi` 才落本地。移动端应改为"静默落本地 + 状态提示"，需在 offlineApi 层拦截/抑制该 toast。

---

## 7. 路由架构 — ✅ 强匹配

- 桌面 [router/index.ts](../frontend/src/router/index.ts) 用 **`createWebHistory`**（非 hash），计划对 `/m/*` 用 `createWebHistory` 与代码习惯一致 ✅。
- 计划采用**独立移动端入口**（`main-mobile.ts` + `vite.config.mobile.ts` + `index.mobile.html`），每次构建只挂载一套 router，天然规避与桌面 `/`、`/dashboard` 等路由的 `name`/`path` 冲突 ✅。这是**正确且与代码契合**的架构。
- `requiresAuth` 守卫：桌面 `beforeEach` 里检查 `isInitialized` + `token`。移动端 `mobile.ts` 计划用 `useAuthStore().token` 判空✅，但**注意**：桌面守卫还处理「未初始化 → /setup」流程，移动端需决定是否支持首次 setup（计划只提 `/m/login`，建议明确处理未初始化情形或直接要求已初始化的后端）。

---

## 8. Vite / 构建 — ✅ 匹配（🟡 1 处）

- [vite.config.ts](../frontend/vite.config.ts)：`@`→`src` 别名 ✅、`AutoImport` + `Components` + `ElementPlusResolver` ✅。计划新增 `vite.config.mobile.ts` 可复用同样配置。
- `index.html` 位于 **frontend 根目录**（非 src/）。计划提出的 `index.mobile.html` 应放**同根目录**，与计划"输入 index.mobile.html"的假设一致 ✅。
- 无 `vitest.config` 与 `vite.config.mobile` 现存文件，计划新增无冲突 ✅。（确认：`ls vitest.config.* vite.config.mobile.*` → 无。）
- 🟡 **dts 碰撞**：`AutoImport('src/auto-imports.d.ts')` 与 `Components('src/components.d.ts')` 在两套 config 都指向同一 dts 文件。先后跑 `build` 与 `build:mobile` 会互相覆盖生成——因两者 resolver 相同，内容一致，**实际无害**，但若想干净可给移动端用独立 dts 路径。

---

## 9. 样式主题（styles/index.css）— ✅ 强匹配

计划 scoped 样式引用的 CSS 变量**全部已存在**（[styles/index.css](../frontend/src/styles/index.css)）：
`--mf-primary(#00d4ff)`、`--mf-accent`、`--mf-surface`、`--mf-surface-muted`、`--mf-background(#060b18)`、`--mf-border`、`--mf-text-main`、`--mf-text-muted`、`--mf-shadow-glow`、`--mf-radius-*`、`--mf-spacing-*` 等 ✅。

> 移动端可无缝复用统一暗色金融主题；底部 Tab 栏与卡片直接以这些变量着色。**无新增样式基建**。

`main-native` 需与桌面一样注册全量 Element Plus icon（`main.ts` 中 `for...Object.entries(ElementPlusIconsVue)` 全局注册），计划 MobileLayout 的图标（DataAnalysis/Plus/Wallet/PieChart/Setting）才能按名解析 ✅。

---

## 10. 后端零改动假设 — ✅ 成立

计划所有端点均存在，无需后端改动，与计划「backend zero changes + 21 tests PASS」一致。涉及端点：`/summary`、`/daily-expenses(/monthly)`、`/transactions`、`/assets`、`/categories`、`/reports/*`、`/auth/*`。

---

## 11. 需在实施前修订/确认的清单

**🔴 高优先级（会直接导致打包后不可用）：**
1. **移动端 API base URL**：public-key fetch 与 Axios 必须指向绝对后端地址（经 `VITE_API_URL` / Capacitor 配置），不能依赖 dev proxy 的相对路径。
2. **登录鉴权失败处理**：避免 `window.location='/login'` 导航离壳；改为 `/m/login` 或 Vue Router 导航。

**🟡 中优先级（体验/正确性）：**
3. `ExpenseCategoryPie` 复用时需给定高父容器（`height:100%` 图表）。
4. http.ts 网络失败 toast 在离线记账时需抑制，改静默落本地。
5. 明确移动端"未初始化/首次 setup"流程（桌面有 `/setup`）。
6. ReportsMobile 建议引用 `api/reports.ts` 丰富接口，优于仅 `dailyExpenses.monthly`。
7. （可选）移动端 AutoImport/Components dts 用独立路径避免覆盖。

**✅ 无需改动的部分（计划已正确）：** API 模块/类型/组件引用、`createWebHistory`、独立入口架构、CSS 主题复用、`vite.config.mobile.ts` 方向、后端零改动。

---

*生成时间：2026-08-15* 
*依据：`frontend/` HEAD `19a4eb3` 实际源码；工作区干净。*
