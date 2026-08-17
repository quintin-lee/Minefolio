# Frontend UI 高级感精修 Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在保持现有暗色赛博朋克主题不变的前提下，通过图标替换、微动效、布局精修和可视化增强，全面提升 Minefolio 前端页面的精致度与专业感。

**Architecture:** 渐进式精修，不改 API 层与数据流。每页独立改动，通过新增全局 CSS 变量和动画 keyframes 统一动效体系。图标库引入 `@iconify/vue` + `@iconify-icons/ph`，Sparkline 用纯 SVG 内联实现。

**Tech Stack:** Vue 3 + TypeScript + Element Plus + ECharts 5 + @iconify/vue

---

## Chunk 1: 全局基础（CSS 变量 + 动效 + 图标依赖）

### Task 1: 安装图标依赖

**Files:**
- Modify: `frontend/package.json`
- Run: `cd frontend && npm install @iconify/vue @iconify-icons/ph`

- [ ] **Step 1: 安装依赖**

```bash
cd frontend && npm install @iconify/vue @iconify-icons/ph
```

- [ ] **Step 2: 验证安装成功**

```bash
cd frontend && npx tsc --noEmit 2>&1 | head -5
```

Expected: 无错误（或仅有已有错误）

- [ ] **Step 3: 提交**

```bash
cd frontend && git add package.json package-lock.json
git commit -m "chore(deps): add @iconify/vue and @iconify-icons/ph"
```

---

### Task 2: 新增全局 CSS 变量与动画

**Files:**
- Modify: `frontend/src/styles/index.css` (末尾追加)

在文件末尾追加以下内容：

```css
/* ── UI Refinement: glow layers ── */
:root {
  --mf-glow-subtle: 0 0 8px rgba(0, 212, 255, 0.15);
  --mf-glow-medium: 0 0 16px rgba(0, 212, 255, 0.25);
}

/* ── UI Refinement: animation keyframes ── */
@keyframes mf-fade-in-up {
  from { opacity: 0; transform: translateY(10px); }
  to   { opacity: 1; transform: translateY(0); }
}

@keyframes mf-shimmer {
  0%   { background-position: -200% 0; }
  100% { background-position: 200% 0; }
}

@keyframes mf-gradient-shift {
  0%   { background-position: 0% 50%; }
  50%  { background-position: 100% 50%; }
  100% { background-position: 0% 50%; }
}

/* ── UI Refinement: utility classes ── */
.mf-animate-in {
  animation: mf-fade-in-up 0.3s ease-out both;
}

.mf-stagger-1 { animation-delay: 0ms; }
.mf-stagger-2 { animation-delay: 60ms; }
.mf-stagger-3 { animation-delay: 120ms; }
.mf-stagger-4 { animation-delay: 180ms; }

/* ── UI Refinement: table row left-glow on hover ── */
.mf-table-row-glow:hover td:first-child::before {
  content: '';
  position: absolute;
  left: 0; top: 0; bottom: 0;
  width: 2px;
  background: var(--mf-primary);
  box-shadow: 0 0 8px rgba(0, 212, 255, 0.6);
  border-radius: 0 2px 2px 0;
}

.mf-table-row-glow .el-table__row {
  position: relative;
  transition: background 0.2s ease;
}

/* ── UI Refinement: button press scale ── */
.mf-btn-press {
  transition: transform 0.15s cubic-bezier(0.4, 0, 0.2, 1);
}
.mf-btn-press:active {
  transform: scale(0.97);
}

/* ── UI Refinement: tabular-nums utility ── */
.mf-mono {
  font-variant-numeric: tabular-nums;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
}

/* ── UI Refinement: logo text gradient ── */
.mf-logo-text {
  background: linear-gradient(135deg, #00d4ff 0%, #a78bfa 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}
```

- [ ] **Step 1: 追加 CSS**

用 `edit` 工具在 `frontend/src/styles/index.css` 末尾追加上述内容。

- [ ] **Step 2: 验证构建**

```bash
npm --prefix frontend run build 2>&1 | tail -5
```

Expected: `✓ built in ...`（无 TypeScript 错误）

- [ ] **Step 3: 提交**

```bash
git add frontend/src/styles/index.css
git commit -m "feat(ui): add global animation keyframes and utility classes"
```

---

## Chunk 2: Layout 侧边栏 Logo + 活跃项样式

### Task 3: Logo emoji → Phosphor Wallet 图标

**Files:**
- Modify: `frontend/src/views/Layout.vue` (lines 1-5, 101+)

- [ ] **Step 1: 替换 Logo emoji**

将第 5 行：
```html
<div class="logo-icon-wrapper">💰</div>
```
改为：
```html
<el-icon class="logo-icon"><Wallet /></el-icon>
```

- [ ] **Step 2: 新增 Wallet 图标导入**

在 `import { Grid } from '@element-plus/icons-vue'` 附近新增：
```ts
import { Wallet } from '@iconify-icons/ph'
import { Icon } from '@iconify/vue'
```

- [ ] **Step 3: 使用 Iconify 渲染 Phosphor 图标**

模板中使用：
```html
<Icon icon="ph:wallet" class="logo-icon" />
```

- [ ] **Step 4: 添加 Logo 图标样式**

在 `<style scoped>` 中追加：
```css
.logo-icon {
  font-size: 22px;
  color: var(--mf-primary);
  filter: drop-shadow(0 0 6px rgba(0, 212, 255, 0.5));
}
.logo-text {
  background: linear-gradient(135deg, #00d4ff 0%, #a78bfa 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}
```

- [ ] **Step 5: 标题竖条加宽**

将 `.title-accent` 的 `width: 3px` 改为 `width: 4px`，`box-shadow` 增强为 `0 0 10px rgba(0,212,255,0.6)`。

- [ ] **Step 6: 验证构建**

```bash
npm --prefix frontend run build 2>&1 | tail -5
```

Expected: `✓ built in ...`

- [ ] **Step 7: 提交**

```bash
git add frontend/src/views/Layout.vue frontend/src/styles/index.css
git commit -m "feat(ui): replace logo emoji with Phosphor Wallet icon + gradient text"
```

---

## Chunk 3: Dashboard 数字卡片 + Sparkline

### Task 4: Dashboard 统计卡片升级

**Files:**
- Modify: `frontend/src/views/Dashboard.vue`
- Modify: `frontend/src/components/SummaryCard.vue`

- [ ] **Step 1: SummaryCard 支持 sparkline 数据**

在 `SummaryCard.vue` 中新增 props：
```ts
const props = defineProps<{
  label: string
  value: string | number
  type?: 'neutral' | 'income' | 'expense' | 'highlight'
  extraClass?: string
  sparkline?: number[]  // 新增：迷你趋势数据
}>()
```

在 template 的 `.summary-value` 后插入 sparkline SVG（仅当 `sparkline` 有数据时渲染）：
```html
<div v-if="props.sparkline && props.sparkline.length > 1" class="sparkline-wrap">
  <svg class="sparkline" :viewBox="`0 0 60 20`" preserveAspectRatio="none">
    <polyline
      :points="sparklinePoints"
      fill="none"
      stroke="currentColor"
      stroke-width="1.5"
      stroke-linecap="round"
      stroke-linejoin="round"
    />
  </svg>
</div>
```

在 `<script setup>` 中新增 computed：
```ts
import { computed } from 'vue'

const sparklinePoints = computed(() => {
  const data = props.sparkline ?? []
  if (data.length < 2) return ''
  const max = Math.max(...data)
  const min = Math.min(...data)
  const range = max - min || 1
  const w = 60
  const h = 20
  return data.map((v, i) => {
    const x = (i / (data.length - 1)) * w
    const y = h - ((v - min) / range) * h
    return `${x},${y}`
  }).join(' ')
})
```

在 `<style scoped>` 中追加：
```css
.sparkline-wrap {
  position: absolute;
  top: 10px;
  right: 12px;
  width: 60px;
  height: 20px;
  opacity: 0.7;
}
.sparkline {
  width: 100%;
  height: 100%;
}
```

- [ ] **Step 2: Dashboard 传入 sparkline 数据**

在 `Dashboard.vue` 中，将 4 张 SummaryCard 改为传入 sparkline：
```html
<el-col :xs="12" :sm="12" :md="6" v-for="(card, i) in statCards" :key="i" :class="'mf-stagger-' + (i+1)">
  <el-card shadow="hover" class="stat-card" :class="card.cls">
    <div class="stat-content">
      <div class="stat-label">{{ card.label }}</div>
      <div class="stat-value">{{ card.value }}</div>
    </div>
  </el-card>
</el-col>
```

在 script 中新增 computed：
```ts
const last7 = computed(() => {
  const t = summary.value.trend ?? []
  return t.slice(-7).map(d => d.net_worth)
})

const statCards = computed(() => [
  { label: '总资产', value: formatCurrency(summary.value.total_assets), cls: 'assets' },
  { label: '总负债', value: formatCurrency(summary.value.total_liabilities), cls: 'liabilities' },
  { label: '净资产', value: formatCurrency(summary.value.net_worth), cls: 'networth' },
  { label: '本月结余', value: formatCurrency(currentMonthBalance?.balance ?? 0), cls: 'monthly' },
])
```

- [ ] **Step 3: 数字字号放大**

在 Dashboard.vue scoped style 中：
```css
.stat-value {
  font-size: 36px;   /* 原 26px */
  letter-spacing: -1px;  /* 原 -0.5px */
}
```

- [ ] **Step 4: 卡片入场动画**

给 `.dashboard` 内每张卡片加 `mf-animate-in mf-stagger-N` class（见 Step 2）。

- [ ] **Step 5: 验证构建**

```bash
npm --prefix frontend run build 2>&1 | tail -5
```

Expected: `✓ built in ...`

- [ ] **Step 6: 提交**

```bash
git add frontend/src/components/SummaryCard.vue frontend/src/views/Dashboard.vue
git commit -m "feat(ui): add sparkline to SummaryCard + enlarge Dashboard stat numbers"
```

---

### Task 5: Dashboard 图表区域精修

**Files:**
- Modify: `frontend/src/components/NetWorthChart.vue`（如有必要调整高度）
- Modify: `frontend/src/components/AssetBreakdownPie.vue`（标签外移）

- [ ] **Step 1: 净资产趋势图高度增加**

在 `Dashboard.vue` 的 `<NetWorthChart>` 外层 div 加 style：
```html
<div class="nw-chart-wrap">
  <NetWorthChart :data="summary.trend" />
</div>
```

```css
.nw-chart-wrap {
  height: 320px;  /* 原约 280px */
}
```

- [ ] **Step 2: 年度收支柱状图 tooltip 精度**

检查 `YearlyChart.vue` 的 tooltip formatter，确保显示完整金额（不加单位缩写）。

- [ ] **Step 3: 最近收支表格精简**

将当前 4 列表格（日期/分类/类型/金额）改为 3 列，隐藏"类型"列（通过 `display: none` 或移除 column），行高收紧：
```css
:deep(.el-table .el-table__row) {
  height: 40px;
}
:deep(.el-table .el-table__cell) {
  padding: 8px 0;
  font-size: 12px;
}
```

- [ ] **Step 4: 验证构建**

```bash
npm --prefix frontend run build 2>&1 | tail -5
```

Expected: `✓ built in ...`

- [ ] **Step 5: 提交**

```bash
git add frontend/src/views/Dashboard.vue
git commit -m "feat(ui): refine Dashboard charts + recent expenses table density"
```

---

## Chunk 4: Transactions 表格与筛选优化

### Task 6: Transactions 统计卡片加左侧竖条

**Files:**
- Modify: `frontend/src/views/Transactions.vue` (lines 22-35)

- [ ] **Step 1: 统计卡片加左侧彩色竖条**

在 `.summary-cards` 的每张 SummaryCard 外层包一个 div：
```html
<el-col :xs="12" :sm="6" v-for="(card, i) in summaryCards" :key="i">
  <div class="summary-card-with-bar" :class="card.barClass">
    <SummaryCard ... />
  </div>
</el-col>
```

在 script 中定义：
```ts
const summaryCards = [
  { label: '本月交易总额', value: ..., barClass: 'bar-cyan' },
  { label: '买入/存入合计', value: ..., barClass: 'bar-green' },
  { label: '卖出/取出合计', value: ..., barClass: 'bar-red' },
  { label: '本月交易笔数', value: ..., barClass: 'bar-amber' },
]
```

在 scoped style 中追加：
```css
.summary-card-with-bar {
  position: relative;
  padding-left: 3px;
}
.summary-card-with-bar::before {
  content: '';
  position: absolute;
  left: 0; top: 0; bottom: 0;
  width: 3px;
  border-radius: 2px 0 0 2px;
}
.bar-cyan::before  { background: #00d4ff; box-shadow: 0 0 8px rgba(0,212,255,0.5); }
.bar-green::before { background: #10b981; box-shadow: 0 0 8px rgba(16,185,129,0.5); }
.bar-red::before   { background: #ef4444; box-shadow: 0 0 8px rgba(239,68,68,0.5); }
.bar-amber::before { background: #f59e0b; box-shadow: 0 0 8px rgba(245,158,11,0.5); }

.summary-card-with-bar .summary-value {
  font-size: 22px;
  font-variant-numeric: tabular-nums;
}
.summary-card-with-bar .summary-label {
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}
```

- [ ] **Step 2: 导出/导入按钮改为 icon-only**

将"导出 CSV"和"导入 CSV"按钮改为仅显示图标的 `<el-tooltip>` + `<el-button :icon="Download"/:icon="Upload">` 形式，去除文字。

- [ ] **Step 3: 验证构建**

```bash
npm --prefix frontend run build 2>&1 | tail -5
```

Expected: `✓ built in ...`

- [ ] **Step 4: 提交**

```bash
git add frontend/src/views/Transactions.vue
git commit -m "feat(ui): add colored bars to Transactions summary cards + icon-only export buttons"
```

---

### Task 7: Transactions 表格行优化

**Files:**
- Modify: `frontend/src/views/Transactions.vue`

- [ ] **Step 1: 表格行加左侧光条 hover 效果**

在 `<style scoped>` 中追加：
```css
.premium-table .el-table__row {
  transition: background 0.2s ease;
}
.premium-table .el-table__row:hover td.el-table__cell:first-child::before {
  content: '';
  position: absolute;
  left: 0; top: 0; bottom: 0;
  width: 2px;
  background: var(--mf-primary);
  box-shadow: 0 0 8px rgba(0, 212, 255, 0.6);
  border-radius: 0 2px 2px 0;
}
.premium-table .el-table__row {
  position: relative;
}
```

- [ ] **Step 2: 金额列 tabular-nums**

确保金额列的 `<span>` 加 `class="mf-mono"`（全局工具类已在 Task 2 定义）。

- [ ] **Step 3: 操作列改为 icon-only 按钮**

将编辑/删除的文字按钮改为 `<el-button :icon="Edit" circle size="small">` 和 `<el-button :icon="Delete" circle size="small">`，每个包在 `<el-tooltip>` 中。

- [ ] **Step 4: 验证构建**

```bash
npm --prefix frontend run build 2>&1 | tail -5
```

Expected: `✓ built in ...`

- [ ] **Step 5: 提交**

```bash
git add frontend/src/views/Transactions.vue
git commit -m "feat(ui): add left-glow row hover + icon-only action buttons in Transactions"
```

---

## Chunk 5: Assets 页面 emoji → Phosphor 图标

### Task 8: Assets 页面图标替换

**Files:**
- Modify: `frontend/src/views/Assets.vue` (line 34)

- [ ] **Step 1: 提取资产图标映射**

将 inline emoji 对象 `{'bank': '🏦', ...}` 提取为常量，替换为 Iconify 图标名：

```ts
const ASSET_TYPE_ICONS: Record<string, string> = {
  bank: 'ph:bank',
  cash: 'ph:cash',
  alipay: 'ph:device-mobile',
  wechat: 'ph:chat-circle',
  credit_card: 'ph:credit-card',
  stock: 'ph:trend-up',
  fund: 'ph:trend-up',
  bond: 'ph:trend-up',
  crypto: 'ph:crypto',
  loan: 'ph:arrow-down-left',
  real_estate: 'ph:house',
}
```

- [ ] **Step 2: 替换模板中的 emoji 渲染**

将：
```html
<span class="asset-icon">{{ {'bank': '🏦', ...}[row.asset_type as string] || '💼' }}</span>
```
改为：
```html
<span class="asset-icon">
  <Icon icon="ph:wallet" v-if="false" />  <!-- 占位：实际使用下面的方式 -->
  <Icon :icon="ASSET_TYPE_ICONS[row.asset_type] ?? 'ph:briefcase'" />
</span>
```

在 script 顶部添加：
```ts
import { Icon } from '@iconify/vue'
```

- [ ] **Step 3: 图标加浅底色圆角矩形**

在 `<style scoped>` 中追加：
```css
.asset-icon {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 28px;
  height: 28px;
  border-radius: 6px;
  background: rgba(0, 212, 255, 0.06);
  margin-right: 8px;
  flex-shrink: 0;
  color: var(--mf-primary);
  font-size: 16px;
}
```

- [ ] **Step 4: 验证构建**

```bash
npm --prefix frontend run build 2>&1 | tail -5
```

Expected: `✓ built in ...`

- [ ] **Step 5: 提交**

```bash
git add frontend/src/views/Assets.vue
git commit -m "feat(ui): replace emoji icons with Phosphor icons in Assets page"
```

---

## Chunk 6: Holdings 投资数据增强

### Task 9: Holdings 图标替换 + 进度条 + 表格精修

**Files:**
- Modify: `frontend/src/views/Holdings.vue`

- [ ] **Step 1: ASSET_ICONS 替换为 Iconify 图标名**

将：
```ts
const ASSET_ICONS: Record<string, string> = {
  stock: '📈', fund: '📊', bond: '📉', crypto: '🪙',
}
```
改为：
```ts
const ASSET_ICONS: Record<string, string> = {
  stock: 'ph:trend-up', fund: 'ph:trend-up',
  bond: 'ph:trend-up', crypto: 'ph:crypto',
}
```

模板中持仓表格名称列改为：
```html
<span class="asset-name">
  <Icon :icon="ASSET_ICONS[row.asset_type] ?? 'ph:briefcase'" />
  {{ row.name }}
</span>
```

- [ ] **Step 2: 浮动盈亏进度条**

在浮动盈亏 SummaryCard 中，在数值下方加 `<el-progress>` ：
```html
<el-progress
  :percentage="Math.abs(floatingPct)"
  :color="floatingPnl > 0 ? '#10b981' : '#ef4444'"
  :show-text="false"
  :stroke-width="4"
  class="pnl-progress"
/>
<div class="summary-sub">({{ floatingPct.toFixed(2) }}%)</div>
```

在 script 中新增 computed：
```ts
const floatingPct = computed(() => report.value?.summary?.floating_pct ?? 0)
const floatingPnl = computed(() => report.value?.summary?.total_floating_pnl ?? 0)
```

- [ ] **Step 3: 类型 pill 样式**

将持仓表格中的 `<el-tag>` 替换为自定义 pill span：
```html
<span class="type-pill" :class="typePillClass(row.asset_type)">
  {{ typeLabel(row.asset_type) }}
</span>
```

```ts
function typePillClass(t: string): string {
  return t === 'crypto' ? 'pill-crypto' : 'pill-default'
}
```

```css
.type-pill {
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 9999px;
  background: rgba(0, 212, 255, 0.08);
  color: #00d4ff;
  border: 1px solid rgba(0, 212, 255, 0.15);
  font-weight: 500;
}
.type-pill.pill-crypto {
  background: rgba(124, 58, 237, 0.08);
  color: #a78bfa;
  border-color: rgba(124, 58, 237, 0.15);
}
```

- [ ] **Step 4: 表格数字 monospace + 盈亏箭头**

在盈亏率列中，用 Unicode 箭头替代文字：
```html
<span :class="pnlClass(row.floating_pnl)">
  {{ row.floating_pnl > 0 ? '↑' : row.floating_pnl < 0 ? '↓' : '—' }}
  {{ row.floating_pct.toFixed(2) }}%
</span>
```

金额列（成本/市值）加 `class="mf-mono"`。

- [ ] **Step 5: 验证构建**

```bash
npm --prefix frontend run build 2>&1 | tail -5
```

Expected: `✓ built in ...`

- [ ] **Step 6: 提交**

```bash
git add frontend/src/views/Holdings.vue
git commit -m "feat(ui): replace Holdings emoji icons, add PnL progress bar + type pills"
```

---

## Chunk 7: Reports 空状态 + App.vue 路由过渡

### Task 10: Reports 空状态 CSS 渐变圆

**Files:**
- Modify: `frontend/src/views/Reports.vue`

- [ ] **Step 1: 替换 el-empty 为 CSS 渐变空状态**

找到三处 `<el-empty>` （line 12, 93, 152），替换为：
```html
<div v-if="!monthly" class="mf-empty-state">
  <div class="mf-empty-circle"></div>
  <p>暂无收支数据，完成第一笔交易后这里会出现图表</p>
</div>
```

- [ ] **Step 2: 追加空状态样式**

在 `<style scoped>` 中追加：
```css
.mf-empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 48px 24px;
  gap: 16px;
}
.mf-empty-circle {
  width: 64px;
  height: 64px;
  border-radius: 50%;
  background: radial-gradient(circle at 30% 30%, rgba(0,212,255,0.15), rgba(124,58,237,0.08));
  border: 1px solid rgba(0, 212, 255, 0.12);
  box-shadow: 0 0 24px rgba(0, 212, 255, 0.08);
}
.mf-empty-state p {
  color: var(--mf-text-muted);
  font-size: 13px;
  text-align: center;
}
```

- [ ] **Step 3: 验证构建**

```bash
npm --prefix frontend run build 2>&1 | tail -5
```

Expected: `✓ built in ...`

- [ ] **Step 4: 提交**

```bash
git add frontend/src/views/Reports.vue
git commit -m "feat(ui): replace el-empty with CSS gradient empty state in Reports"
```

---

### Task 11: App.vue 路由切换 fade-in-up 动画

**Files:**
- Modify: `frontend/src/App.vue`

- [ ] **Step 1: 添加 Transition wrapper**

将：
```html
<template>
  <router-view />
</template>
```
改为：
```html
<template>
  <Transition name="mf-route" mode="out-in">
    <router-view />
  </Transition>
</template>
```

- [ ] **Step 2: 追加路由过渡样式**

在 `<style scoped>` 中追加：
```css
.mf-route-enter-active,
.mf-route-leave-active {
  transition: opacity 0.25s ease, transform 0.25s ease;
}
.mf-route-enter-from {
  opacity: 0;
  transform: translateY(8px);
}
.mf-route-leave-to {
  opacity: 0;
  transform: translateY(-4px);
}
```

- [ ] **Step 3: 验证构建**

```bash
npm --prefix frontend run build 2>&1 | tail -5
```

Expected: `✓ built in ...`

- [ ] **Step 4: 提交**

```bash
git add frontend/src/App.vue
git commit -m "feat(ui): add route transition fade-in-up in App.vue"
```

---

## Chunk 8: 最终验证

### Task 12: 全量构建与测试

- [ ] **Step 1: 前端 TypeScript 构建**

```bash
npm --prefix frontend run build
```

Expected: `✓ built in ...` 且无 error

- [ ] **Step 2: Backend 构建**

```bash
cmake --build backend/build --parallel
```

Expected: 无编译错误

- [ ] **Step 3: 后端集成测试**

```bash
bash backend/tests/test_link.sh
```

Expected: `103 PASS`

- [ ] **Step 4: 手动验证清单**

逐项检查（浏览器打开开发服务器）：
- [ ] 侧边栏 Logo 为 Phosphor Wallet 图标，Minefolio 文字有渐变
- [ ] 仪表盘数字字号更大，右上角有迷你 sparkline
- [ ] 资产交易统计卡片左侧有彩色竖条
- [ ] 交易表格行 hover 时左侧有蓝色光条
- [ ] 资产列表图标为 Phosphor 图标（非 emoji）
- [ ] Holdings 持仓图标为 Phosphor 图标，盈亏有进度条
- [ ] Holdings 类型标签为精致 pill
- [ ] Reports 空状态为 CSS 渐变圆
- [ ] 路由切换时有 fade-in 过渡效果
- [ ] 构建无 TypeScript 错误

- [ ] **Step 5: 最终提交**

```bash
git add -A
git commit -m "feat(ui): complete frontend UI refinement — icons, animations, charts"
```
