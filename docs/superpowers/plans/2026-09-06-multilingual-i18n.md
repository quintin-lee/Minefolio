# 前端全量多语言（i18n）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 全量迁移前端（桌面 + 移动）用户可见静态文案到 vue-i18n（zh-CN/en-US），语言切换时应用文案与 Element Plus 组件文案一致生效。

**Architecture:** 以既有 `composables/useI18n.ts` 为唯一翻译源；`App.vue` 用 `<el-config-provider>` 让 EP 跟随语言；`utils/locale.ts` 改为 vue-i18n 薄封装；按「外壳 → 桌面视图 → 共享组件 → 移动端」分批把 `.vue` 中文硬编码替换为 `$t('ns.key')`，每批同步扩充 `zh-CN.ts`/`en-US.ts` 词典。

**Tech Stack:** Vue 3 `<script setup>`、vue-i18n（composition mode）、Element Plus（`el-config-provider`）、vitest。

**规格文档：** `docs/superpowers/specs/2026-09-06-multilingual-i18n-design.md`

---

## 全局约定（所有任务适用）

- 工作目录：`frontend/`。校验命令统一：
  - 类型检查：`npx vue-tsc -b`
  - 单测：`npm test`
  - 残留中文检查（某文件应清空）：`grep -nP "[\x{4e00}-\x{9fff}]" <file>`，期望 **退出码 1（无输出）**
  - i18n 对齐测试：`npx vitest run tests/i18n.keys.spec.ts`
- 词典文件：`src/locales/zh-CN.ts`（zhCN 导出）、`src/locales/en-US.ts`（enUS 导出）。命名空间与现有风格一致（`nav/common/login/settings` 等顶层 camelCase），新增顶层命名空间：`dashboard / assets / holdings / transactions / dailyExpenses / plans / categories / transfer / reports / audit / aiTrace / aiTraceDetail / chat / workflow / setup / oauth / mobile / components`。
- **每替换一个中文字面量，必须在两个词典的对应命名空间加入同名 key（zh 值=原文，en 值=英文译文），插值参数两边一致**（用 `{name}` 形式）。`npm test` 中的 `tests/i18n.keys.spec.ts` 会强制 zh/en key 树完全一致——先跑它，红了说明漏了某一侧。
- **不翻译**：品牌名 `Minefolio`、用户数据（分类/账户/账本名、备注、导入内容）、AI 生成内容、后端接口返回的 message（toast 保持原样）、金额/数字本身、`components.d.ts` 等生成文件。
- **ElMessage 等脚本内文案**：用 `const { t } = useI18n()` 或 `import { t } from '@/utils/locale'`，样式同模板 `$t`。
- 每条任务提交信息遵循仓库 `type(scope): 🎯 subject` 规范（gitmoji，见 AGENTS）。

---

## Phase A — 基础设施（含完整代码）

### Task A1: 修复词典结构损坏 + 新增 key 对齐测试

**Files:**
- Modify: `frontend/src/locales/zh-CN.ts`
- Modify: `frontend/src/locales/en-US.ts`
- Create: `frontend/tests/i18n.keys.spec.ts`

- [ ] **Step 1: 修正 en-US.ts 顶层结构，使其与 zh-CN.ts 完全同构**

`en-US.ts` 目前把 `navGroups` 与 `common` 的键错误嵌套在 `nav` 内（zh 中它们是顶层）。重排 en-US.ts：`nav` 只含导航项；`navGroups`、`common` 提升为顶层；随后 `login/assets/transactions/dailyExpenses/reports/categories/settings` 顺序与 zh 相同，且**每个已存在 key 与 zh 同名**。修正后用 `npx vue-tsc -b` 确认类型通过。

- [ ] **Step 2: 编写 i18n key 对齐测试**

创建 `frontend/tests/i18n.keys.spec.ts`：

```ts
import { describe, it, expect } from 'vitest'
import { zhCN } from '@/locales/zh-CN'
import { enUS } from '@/locales/en-US'

type Node = { [k: string]: Node | string }
function assertKeysEqual(name: string, a: Node, b: Node, path = ''): void {
  const ak = Object.keys(a).sort()
  const bk = Object.keys(b).sort()
  expect(bk, `en-US 缺少/多余 key: ${path}`).toEqual(ak)
  for (const k of ak) {
    const p = path ? `${path}.${k}` : k
    const av = a[k]
    const bv = b[k]
    if (typeof av === 'string') {
      expect(typeof bv, `en-US 非字符串: ${p}`).toBe('string')
      const paramsA = [...av.matchAll(/\{(\w+)\}/g)].map(m => m[1]).sort()
      const paramsB = [...(bv as string).matchAll(/\{(\w+)\}/g)].map(m => m[1]).sort()
      expect(paramsB, `插值参数不一致: ${p}`).toEqual(paramsA)
    } else {
      assertKeysEqual(p, av as Node, bv as Node, p)
    }
  }
}

describe('i18n dictionaries', () => {
  it('en-US key tree matches zh-CN exactly (incl. interpolation params)', () => {
    assertKeysEqual('root', zhCN as unknown as Node, enUS as unknown as Node)
  })
})
```

- [ ] **Step 3: 跑测试确认失败（结构修复前的真实差异）**

Run: `npx vitest run tests/i18n.keys.spec.ts`
Expected: FAIL（`nav`/`common` 嵌套差异或已有 key 名不同步）。

- [ ] **Step 4: 修复到测试通过**

以 zh-CN.ts 为基准，逐层补/删 en-US.ts 的 key 使两树一致（en 译文可先沿用现有英文或直译；本阶段只要结构一致即可）。`settings` 等已迁移段的词条也须同名。
Expected: PASS。

- [ ] **Step 5: 提交**

```bash
git add frontend/src/locales/zh-CN.ts frontend/src/locales/en-US.ts frontend/tests/i18n.keys.spec.ts
git commit -m "fix(frontend): 🐛 align i18n dictionaries and add key-parity test"
```

### Task A2: Element Plus 语言随界面切换（el-config-provider）

**Files:**
- Modify: `frontend/src/App.vue`

- [ ] **Step 1: App.vue 包一层 config-provider**

`App.vue` 同时被桌面/移动入口挂载，改造模板与脚本：

```vue
<template>
  <el-config-provider :locale="epLocale">
    <router-view />
  </el-config-provider>
</template>

<script setup lang="ts">
import { computed, onMounted, watch } from 'vue'
import zhCn from 'element-plus/es/locale/lang/zh-cn'
import en from 'element-plus/es/locale/lang/en'
import { useI18n } from '@/composables/useI18n'
import { useAuthStore } from '@/stores/auth'
import { useThemeStore } from '@/stores/theme'

const auth = useAuthStore()
const theme = useThemeStore()
const { locale } = useI18n()

const epLocale = computed(() => (locale.value === 'en-US' ? en : zhCn))

watch(
  locale,
  (l) => {
    document.documentElement.lang = l
  },
  { immediate: true },
)

onMounted(() => {
  theme.applyTheme()
  if (auth.token) {
    auth.fetchUser()
  }
})
</script>
```

注意：`i18n` default 导入保留（main.ts 用它）；`document.documentElement.lang` 初始化即设置。

- [ ] **Step 2: 校验**

Run: `npx vue-tsc -b`（期望通过）；浏览器 dev 栈切到 English 后打开任意含 `el-pagination`/日期选择器页（如交易列表）确认 EP 文案变英文。

- [ ] **Step 3: 提交**

```bash
git add frontend/src/App.vue
git commit -m "feat(frontend): ✨ make element-plus locale follow ui language"
```

### Task A3: 移动端注册 i18n

**Files:**
- Modify: `frontend/src/main-mobile.ts`

- [ ] **Step 1: 引入 i18n 并在 mount 前 init**

```ts
import i18n, { useI18n } from '@/composables/useI18n'

// 在 createApp 之后、mount 之前追加：
app.use(i18n)
useI18n().initLocale()
```

- [ ] **Step 2: 校验** `npm run build:mobile`
- [ ] **Step 3: 提交** `git commit -m "feat(frontend): ✨ enable i18n in mobile entry"`

### Task A4: 统一 utils/locale.ts 到 vue-i18n

**Files:**
- Modify: `frontend/src/utils/locale.ts`

- [ ] **Step 1: 重写为薄封装（保留导出签名，调用方零改动）**

`src/utils/locale.ts` 现有 8 个导入方（Login/Layout/Settings/5 个 settings 组件），保留 `t` 与 `getCurrentLocale`：

```ts
import i18n from '@/composables/useI18n'

export type SupportedLocale = 'zh-CN' | 'en-US'

export function getCurrentLocale(): SupportedLocale {
  try {
    const saved = localStorage.getItem('minefolio_lang')
    if (saved === 'zh-CN' || saved === 'en-US') return saved
  } catch {
    // ignore
  }
  return 'zh-CN'
}

export const t = (key: string): string => i18n.global.t(key)
```

删除原有字典查找逻辑与 `createTranslator`/`LocaleDictionary`（先 `grep -rn "createTranslator\|LocaleDictionary" src` 确认无外部使用再删）。`utils/format.ts` 需要 locale 时从 `getCurrentLocale` 取。

- [ ] **Step 2: 校验** `npx vue-tsc -b` + `npm test`（format/http 相关单测仍应通过）
- [ ] **Step 3: 提交** `git commit -m "refactor(frontend): ♻️ delegate utils/locale to vue-i18n"`

### Task A5: format.ts 数字分组跟随语言

**Files:**
- Modify: `frontend/src/utils/format.ts`

- [ ] **Step 1: locale 感知**

```ts
import { getCurrentLocale } from '@/utils/locale'

function numberLocale(): string {
  return getCurrentLocale() === 'en-US' ? 'en-US' : 'zh-CN'
}

function currencyFormatter(currency: string): Intl.NumberFormat {
  const code = (currency || DEFAULT_CURRENCY).toUpperCase()
  const loc = numberLocale()
  const key = `${code}:${loc}`
  let formatter = formatterCache.get(key)
  if (!formatter) {
    formatter = new Intl.NumberFormat(loc, { style: 'currency', currency: code })
    formatterCache.set(key, formatter)
  }
  return formatter
}
```

`formatPlainCurrency` 的 `toLocaleString('zh-CN', …)` 同样改用 `numberLocale()`。日期/其余保持不动。

- [ ] **Step 2: 校验** `npm test`（`tests/format.spec.ts` 默认 zh 断言不受影响）+ `npx vue-tsc -b`
- [ ] **Step 3: 提交** `git commit -m "feat(frontend): ✨ group numbers by active ui locale"`

### Task A6: 移动端页头语言切换

**Files:**
- Modify: `frontend/src/views-mobile/MobileLayout.vue`
- Create: `frontend/src/components/LocaleToggle.vue`（桌面/移动通用小组件）

- [ ] **Step 1: 新建 LocaleToggle.vue**

```vue
<template>
  <button class="locale-toggle" @click="toggle">
    {{ locale === 'en-US' ? '中' : 'EN' }}
  </button>
</template>

<script setup lang="ts">
import { useI18n } from '@/composables/useI18n'

const { locale, setLocale } = useI18n()
function toggle() {
  setLocale(locale.value === 'en-US' ? 'zh-CN' : 'en-US')
}
</script>

<style scoped>
.locale-toggle {
  background: transparent;
  border: 1px solid var(--mf-border);
  color: var(--mf-text-muted);
  border-radius: 6px;
  font-size: 11px;
  padding: 2px 8px;
  cursor: pointer;
}
</style>
```

- [ ] **Step 2: 接入 MobileLayout 头部（header 右侧操作区）**

在 `MobileLayout.vue` 模板头部合适位置加入 `<LocaleToggle />`（放在现有主题/设置按钮旁）；如头部已拥挤则放设置图标左侧。若移动页头需 EP 弹层相关，则继续用 A2 的 config-provider（App.vue 已包，自动覆盖）。

- [ ] **Step 3: 校验** `npm run build:mobile`；dev 中手动切一次验证持久化（刷新保留）。
- [ ] **Step 4: 提交** `git commit -m "feat(frontend): ✨ add locale toggle to mobile header"`

---

## Phase B — 外壳收尾（Layout/Login/Settings 系列）

> 以下任务执行同一条**迁移协议（MP）**：
> 1) 打开文件，列出所有用户可见中文字面量（模板文本、`placeholder`、`:label`、按钮、`title`、ElMessage/ElMessageBox/confirm 文案、script 中提示串、`v-if` 空态文案）。
> 2) 逐个替换：模板 → `{{ $t('ns.key') }}` / 属性 → `:$t('ns.key')`；脚本 → `t('ns.key')`（脚本顶 `const { t } = useI18n()`）。
> 3) 每加一个 key 同时在 `zh-CN.ts`（原文）与 `en-US.ts`（英文译文）对应命名空间补全，**同一命名空间按字母/出现顺序集中放置**，两文件同步提交。
> 4) 命名空间 `ns` 由任务指定；文件内局部 map/状态 chip 也映射到 key（值存 key 字符串，模板 `$t` 渲染）。
> 5) 动态数量用 `{n}` 插值（如 `您有 {n} 项待执行`）；金额用既有 `formatCurrency` 不参与翻译。
> 6) 完成校验：该文件 `grep -nP "[\x{4e00}-\x{9fff}]" file` **无输出**；`npx vue-tsc -b`；`npx vitest run tests/i18n.keys.spec.ts` 绿。
> 7) 提交信息按任务给出。

### Task B1: Layout.vue 导航/用户菜单收尾
- **Files:** `frontend/src/views/Layout.vue`
- 命名空间：`nav` / `navGroups` / `common` / `components`
- MP。特别注意：面包屑、用户下拉（个人中心/退出/修改密码）、账本选择器相关标签、未登录态按钮。
- Commit: `feat(frontend): ✨ localize layout nav and user menu`

### Task B2: Login.vue / OAuthCallback.vue
- **Files:** `frontend/src/views/Login.vue`、`frontend/src/views/OAuthCallback.vue`
- 命名空间：`login` / `oauth`
- MP。Login 校验规则已用 `t`，改为统一 `useI18n` 导入或保留 `utils/locale.t`（薄封装已支持）；状态机文案（`REGISTRATION_MODE`/`AUTHENTICATION` 显示文本、2FA 提示）一并迁移。
- Commit: `feat(frontend): ✨ localize login and oauth callback`

### Task B3: Settings.vue + 7 个 settings 组件
- **Files:** `views/Settings.vue`、`components/settings/PasswordSettings.vue`、`DataExport.vue`、`TwoFactorSettings.vue`、`MarketSyncSettings.vue`、`AiProviderManager.vue`、`ImportRulesManager.vue`（若存在）、`AppearanceSettings.vue`
- 命名空间：`settings`（细分 `settings.password/export/twoFactor/market/ai/appearance` 子段，按需）
- MP。注意 `ImportRulesManager.vue` 是否在列表中（`ls components/settings/` 实查，缺失则把该文件并入本任务）。
- Commit: `feat(frontend): ✨ localize settings pages fully`

---

## Phase C — 桌面视图（每文件一任务，全部走 MP）

| 任务 | 文件 | 命名空间 | Commit |
|---|---|---|---|
| C1 | `views/Dashboard.vue` | `dashboard` | `feat(frontend): ✨ localize dashboard` |
| C2 | `views/Assets.vue` | `assets` | 同上 assets |
| C3 | `views/Holdings.vue` | `holdings` | 同上 holdings |
| C4 | `views/Transactions.vue` | `transactions` | 同上 transactions |
| C5 | `views/DailyExpenses.vue` | `dailyExpenses` | 同上 daily expenses |
| C6 | `views/Categories.vue` | `categories` | 同上 categories |
| C7 | `views/Transfer.vue` | `transfer` | 同上 transfer（若无此文件则跳过并在提交说明注明） |
| C8 | `views/Plans.vue` | `plans` | 同上 plans |
| C9 | `views/Reports.vue` | `reports` | 同上 reports |
| C10 | `views/AuditLogs.vue` | `audit` | 同上 audit logs |
| C11 | `views/AiTraces.vue` | `aiTrace` | 同上 ai traces |
| C12 | `views/AiTraceDetail.vue` | `aiTraceDetail` | 同上 ai trace detail |
| C13 | `views/Setup.vue` | `setup` | 同上 setup |
| C14 | `views/Chat.vue` | `chat` | 同上 chat（侧栏会话/时间分组/输入区/空态等；AI 回复正文不动） |

示例（C1 Dashboard 前几处）：`<h2>仪表盘</h2>` → `<h2>{{ $t('dashboard.title') }}</h2>`；`折算基准:` → `{{ $t('dashboard.baseCurrencyLabel') }}:`；`您有 <strong>{{ n }}</strong> 项定投计划待执行` → `{{ $t('dashboard.dcaPending', { n: pendingDcaTasks.length }) }}`；`总资产` → `{{ $t('reports.totalAssets') }}`（若 reports 已有该词条则复用，避免重复 key）。

## Phase D — 共享组件扫尾（每文件一任务，全部走 MP）

| 任务 | 文件 | 命名空间 |
|---|---|---|
| D1 | `components/AssetCard.vue`、`components/SummaryCard.vue`、`components/AssetBreakdownPie.vue`、`components/HoldingsTypePie.vue`、`components/HoldingsCostBar.vue`、`components/AssetTrendLine.vue`、`components/NetWorthChart.vue`、`components/ExpenseCategoryPie.vue`、`components/ExpenseTrendBar.vue`、`components/MonthlyChart.vue`、`components/YearlyChart.vue`、`components/PriceHistoryChart.vue` | `components.chart.*` |
| D2 | `components/TransactionTable.vue`、`components/CategoryTree.vue`、`components/TagPicker.vue`、`components/SymbolSelect.vue` | `components.*` |
| D3 | `components/QuickRecordDialog.vue`、`components/DailyExpenseForm.vue`、`components/DcaPlanDialog.vue`、`components/CashflowScheduleDialog.vue`、`components/CashflowCalendar.vue` | `components.dialog.*` |
| D4 | `components/LedgerSelector.vue`、`components/LedgerDialog.vue`、`components/LedgerMembersDialog.vue`、`components/JoinLedgerDialog.vue` | `components.ledger.*` |
| D5 | `components/ReceiptScannerModal.vue` | `components.receipt.*` |
| D6 | `components/PromptStarters.vue`、`components/WorkflowBar.vue`、`components/WorkflowSlashMenu.vue`、`components/WorkflowConfigCard.vue`、`components/WorkflowProgressCard.vue`、`components/ChatMessageContent.vue`、`components/ActionCard.vue`、`components/CodeBlock.vue`、`components/MermaidBlock.vue` | `chat.*` / `workflow.*`（文案取自各工作流标题/描述、菜单项、按钮；卡片 **data** 内的 `title/description`（workflow defs 来自 store）保持数据不动） |

每个 D 任务 = 一个 commit：`feat(frontend): ✨ localize shared components (charts/dialogs/ledger/receipt/chat-area)`（按 D1–D6 各自 scope 写）。

## Phase E — 移动端视图（每文件一任务，全部走 MP）

| 任务 | 文件 | 命名空间 |
|---|---|---|
| E1 | `views-mobile/MobileLayout.vue`、`views-mobile/LoginMobile.vue` | `mobile` / 复用 `login` |
| E2 | `views-mobile/DashboardMobile.vue` | `mobile.dashboard` |
| E3 | `views-mobile/AssetsMobile.vue`、`views-mobile/HoldingsMobile.vue` | `mobile.assets`/`mobile.holdings` |
| E4 | `views-mobile/TransactionsMobile.vue`、`views-mobile/DailyExpensesMobile.vue`、`views-mobile/ExpenseQuickSheet.vue` | `mobile.transactions`/`mobile.dailyExpenses` |
| E5 | `views-mobile/PlansMobile.vue`、`views-mobile/ReportsMobile.vue` | `mobile.plans`/`mobile.reports` |
| E6 | `views-mobile/SettingsMobile.vue` | 复用 `settings` / `mobile.settings` |

E 系列 Commit：`feat(frontend): ✨ localize mobile views (mobile layout/dashboard/assets/… )`（分批按任务写）。

---

## Phase F — 收尾对齐

### Task F1: 全量残留扫描
- [ ] 扫描全部 `.vue/.ts`（排除 `views-mobile` 已清 + 生成文件）：对 68 个源文件逐个跑
  `grep -nP "[\x{4e00}-\x{9fff}]" <file>`，将仍有输出的文件按「数据/品牌/注释」人工确认：
  - 模板文本（非注释非数据）仍含中文 → 补迁移（回 Phase C/D/E 对应任务修复，不新增任务）。
  - 允许残留：`<!-- 注释 -->`、JS 对象里的用户数据/种子（如 category 默认名）、`import`/`type` 注释、日志字符串。
- [ ] 结果记录到 commit message 或任务说明。

### Task F2: 词典对齐 + 全量校验
- [ ] `npx vitest run tests/i18n.keys.spec.ts`（PASS）
- [ ] `npm test`（全部 PASS）
- [ ] `npm run build`（桌面）
- [ ] `npm run build:mobile`（移动）
- [ ] `git commit -m "feat(frontend): ✨ finish full i18n coverage and parity"`（若 F1 有补丁并入）

### Task F3: 英文抽检（dev 栈）
- [ ] Playwright 脚本：注册临时用户 → localStorage 置 `minefolio_lang=en-US` → 逐个打开 `/dashboard`、`/transactions`、`/settings`、`/login`、聊天页抽屉（WorkflowBar「全部」抽屉 + 一个 `el-drawer`）截图；
- [ ] 断言：页面关键文本（导航「Dashboard」「Assets」、按钮「Save」「Cancel」、表格列头）为英文；`html[lang]="en-US"`；EP 分页/弹层英文；
- [ ] 截图存 `/tmp/i18n_en_pass/` 供人工核对；发现漏翻的中文 chrome → 回对应任务修复。

### Task F4: 文档更新
- [ ] `README.md`（功能/设置说明处加「界面语言：简体中文 / English（设置 → 外观）」）
- [ ] `AGENTS.md` Frontend 段落补充：i18n 约定（新文案必须双语 key + 跑 `tests/i18n.keys.spec.ts`）
- [ ] Commit：`docs(frontend): 📝 document i18n conventions`

---

## Self-Review 备注（执行前确认）

- 规格中的非目标（后端/AI/用户数据/币种本地化）在 MP 约定中已显式排除。
- `views/Transfer.vue` 若实际不存在（桌面转账入口可能在别处），任务 C7 跳过并在该批提交说明注明——执行 Task C 前先 `ls views/` 复核本计划文件清单与目录一致，不一致处按目录实测调整（计划允许的最小偏差）。
- 移动端 EP 是否全量引入（main-mobile 现有 `app.use(ElementPlus)`）不受 A2 影响。
- `components/settings/` 实际文件以目录为准（本计划未穷举 ImportRulesManager 等，Task B3 先 `ls` 再并）。
