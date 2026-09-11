# 仪表盘 Bento Grid 重构与核心流水页面质感升级实施计划 (Bento Dashboard & Ledger Modernization Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 按照设计规范全面重塑 Minefolio 的核心数据看板与流水交互，引入 Bento Grid 模块化网格、Hero 净资产大卡（含一体化多币种微条与发光 Sparkline）、ECharts 高阶渐变图表（带时间切片与 Donut 中心指标联动）、以及无界流式卡片流水行，打造媲美 Linear / Raycast 的顶尖金融科技体验。

**Architecture:** 采取自底向上的模块化实现：样式系统与动效扩展 (`index.css`) -> ECharts 金融可视化组件升级 (`NetWorthChart.vue`, `AssetBreakdownPie.vue`, `YearlyChart.vue`) -> Dashboard Bento 大盘重构 (`Dashboard.vue`) -> 流水与交易页面流式卡片化 (`Transactions.vue`, `DailyExpenses.vue`) -> 全面静态类型构建与测试套件验证。

**Tech Stack:** Vue 3 (Composition API, `<script setup>`), TypeScript, Element Plus, ECharts 5, Phosphor Icons (`@iconify/vue`), Vite, Vitest.

---

### File Structure Map

| 文件路径 | 职责 |
|---|---|
| `frontend/src/styles/index.css` | 全局 Bento 卡片边框高光、微网格光晕、流式卡片行样式、脉冲发光动画、Hero 大字号输入框 |
| `frontend/src/components/NetWorthChart.vue` | 净资产折线图：蓝紫科技发光渐变、毛玻璃悬浮窗、30D/90D/1Y/全部 时间切片过滤 |
| `frontend/src/components/AssetBreakdownPie.vue` | 资产分布 Donut 环形图：6px 扇区圆角、中心动态指标联动（总资产 / 选中分类与占比）、科技渐变色盘 |
| `frontend/src/components/YearlyChart.vue` | 年度收支柱状图：圆角双色渐变柱（翡翠绿 / 珊瑚红）、单月 Hover 发光阴影 |
| `frontend/src/views/Dashboard.vue` | 仪表盘大盘：Bento Grid 不对称布局、Hero 净资产主卡（Sparkline 脉冲点、多币种微型分段条）、次级指标卡 |
| `frontend/src/views/Transactions.vue` | 交易流水页面：无界流式悬浮行、左侧科技蓝激活条、等宽金融金额、胶囊徽章 |
| `frontend/src/views/DailyExpenses.vue` | 日常收支页面：流式行表格、大字号金额记账弹窗、月度统计微卡 |

---

### Task 1: 扩展基础样式系统与微动效 (`index.css`)

**Files:**
- Modify: `frontend/src/styles/index.css`

- [ ] **Step 1: 添加 Bento Grid 材质、微光边框与脉冲动画**

在 `frontend/src/styles/index.css` 中增加 Bento 卡片高光、脉冲发光动画与流式表格行样式：
```css
/* =========================================================================
   Bento Grid & High-Craft Micro-Interactions
   ========================================================================= */

@keyframes pulse-glow {
  0%, 100% {
    opacity: 0.7;
    transform: scale(1);
    box-shadow: 0 0 6px rgba(59, 130, 246, 0.6);
  }
  50% {
    opacity: 1;
    transform: scale(1.25);
    box-shadow: 0 0 14px rgba(99, 102, 241, 0.9);
  }
}

.bento-hero-card {
  position: relative;
  overflow: hidden;
  border-radius: var(--mf-radius-xl);
  border: 1px solid var(--mf-border-hover);
  background: linear-gradient(135deg, rgba(59, 130, 246, 0.12) 0%, rgba(15, 23, 42, 0.85) 100%);
  backdrop-filter: blur(24px);
  -webkit-backdrop-filter: blur(24px);
  box-shadow: var(--mf-shadow-glow), var(--mf-shadow-md);
  transition: var(--mf-transition);
}

.bento-hero-card::before {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 2px;
  background: linear-gradient(90deg, #3b82f6, #6366f1, #10b981);
}

.bento-hero-card:hover {
  transform: translateY(-2px);
  box-shadow: var(--mf-shadow-glow-strong), var(--mf-shadow-lg);
  border-color: rgba(99, 102, 241, 0.5);
}

/* 流式卡片表格行 */
.stream-table {
  --el-table-border-color: transparent !important;
  --el-table-header-bg-color: rgba(15, 23, 42, 0.6) !important;
  --el-table-tr-bg-color: transparent !important;
  background: transparent !important;
}

.stream-table .el-table__row {
  transition: all 0.18s cubic-bezier(0.4, 0, 0.2, 1);
  position: relative;
}

.stream-table .el-table__row:hover > td.el-table__cell {
  background-color: rgba(30, 41, 59, 0.65) !important;
}

.stream-table .el-table__row td.el-table__cell {
  border-bottom: 1px solid rgba(255, 255, 255, 0.05) !important;
  padding: 12px 0;
}

/* 记账大字号金额输入 */
.hero-amount-input .el-input__wrapper {
  background-color: rgba(15, 23, 42, 0.8) !important;
  border: 1px solid var(--mf-border-hover) !important;
  box-shadow: var(--mf-shadow-glow) !important;
  padding: 8px 16px !important;
  border-radius: var(--mf-radius-lg) !important;
}

.hero-amount-input input {
  font-size: 28px !important;
  font-weight: 700 !important;
  font-family: var(--mf-font-mono) !important;
  color: var(--mf-text-main) !important;
  text-align: center;
}
```

- [ ] **Step 2: 验证样式文件无语法错误**

Run: `npm --prefix frontend run build`
Expected: 编译通过，0 错误。

- [ ] **Step 3: 提交代码**

Run:
```bash
git add frontend/src/styles/index.css
git commit -m "style: add bento hero cards, stream table and pulse animations"
```

---

### Task 2: ECharts 高阶可视化组件升级 (`NetWorthChart.vue`, `AssetBreakdownPie.vue`, `YearlyChart.vue`)

**Files:**
- Modify: `frontend/src/components/NetWorthChart.vue`
- Modify: `frontend/src/components/AssetBreakdownPie.vue`
- Modify: `frontend/src/components/YearlyChart.vue`

- [ ] **Step 1: 升级 `NetWorthChart.vue` 引入发光双色折线与时间切片胶囊**

在 `frontend/src/components/NetWorthChart.vue` 中：
1. 增加时间切片状态 `timeRange = ref<'30D' | '90D' | '1Y' | 'ALL'>('ALL')`，通过顶部胶囊单选组过滤传入 `props.data`。
2. 配置 `echarts.graphic.LinearGradient` 科技蓝紫双色渐变描边线，并配置发光阴影：`shadowBlur: 14, shadowColor: 'rgba(99, 102, 241, 0.45)'`。
3. Tooltip 增加日期变动与较首日涨跌幅百分比展示。

- [ ] **Step 2: 升级 `AssetBreakdownPie.vue` 为圆角 Donut 环形图与中心指标动态联动**

在 `frontend/src/components/AssetBreakdownPie.vue` 中：
1. 调整 radius 为 `['56%', '76%']`，`itemStyle.borderRadius = 6`。
2. 监听 ECharts `mouseover` 与 `mouseout` 事件，在图表中心（使用 `graphic` 或绝对定位 DOM 覆盖层）动态显示总资产（默认）或鼠标悬停项的分类名、数值与占比。
3. 调色盘配置电光蓝、霓虹紫、翡翠绿、暖琥珀、珊瑚粉、天青蓝。

- [ ] **Step 3: 升级 `YearlyChart.vue` 为顶部圆角双色渐变柱**

在 `frontend/src/components/YearlyChart.vue` 中：
1. `series` 柱条设置 `itemStyle.borderRadius = [4, 4, 0, 0]`。
2. 收入系列使用 `#10b981` 到 `#059669` 的垂直线性渐变。
3. 支出系列使用 `#f43f5e` 到 `#e11d48` 的垂直线性渐变。
4. 悬停状态 `emphasis` 增加高亮阴影。

- [ ] **Step 4: 运行前端测试与构建验证**

Run: `npm --prefix frontend test && npm --prefix frontend run build`
Expected: 45 tests pass, Vite build passes.

- [ ] **Step 5: 提交代码**

Run:
```bash
git add frontend/src/components/NetWorthChart.vue frontend/src/components/AssetBreakdownPie.vue frontend/src/components/YearlyChart.vue
git commit -m "feat(charts): upgrade net worth trend, donut ring with center metric and yearly rounded gradient bars"
```

---

### Task 3: Dashboard Bento Grid 布局与 Hero 资产卡重塑 (`Dashboard.vue`)

**Files:**
- Modify: `frontend/src/views/Dashboard.vue`

- [ ] **Step 1: 重构 `Dashboard.vue` 模板为 Bento Grid 结构**

1. 顶部指标行重塑为不对称网格：
   - 左侧 `:xs="24" :md="14"` 容纳 **Hero 净资产大卡**：
     - 大字号净资产数值（`font-size: 34px`，货币符号弱化半透明）。
     - 右侧展示 7 日/30 日变动胶囊（如 `+¥3,450 (+1.25%)`）与带有脉冲圆点的平滑 SVG Sparkline。
     - 卡片底部内嵌**微型多币种配比条（Multi-currency Strip）**，取代原本生硬的独立大横幅。
   - 右侧 `:xs="24" :md="10"` 纵向排列 3 张次级指标卡：
     - **总资产**（科技蓝图标与数值）。
     - **总负债**（珊瑚红图标，计算并显示资产负债率微标 `负债率: xx%`）。
     - **本月结余**（根据正负显示翡翠绿或琥珀橙）。
2. 待办提醒横条升级为灵动微胶囊（Floating Capsule），紧凑悬浮在标题栏下方。
3. 中部与下部图表区域自适应适配 Bento 网格（净资产走势 `:md="15"`，资产分布 `:md="9"`；年度对比 `:md="12"`，近期流水 `:md="12"`）。
4. 近期流水列表采用 `.stream-table` 流式无界设计，配合分类与金额等宽展示。

- [ ] **Step 2: 编译与构建走查**

Run: `npm --prefix frontend run build`
Expected: 0 warnings, 0 errors.

- [ ] **Step 3: 提交代码**

Run:
```bash
git add frontend/src/views/Dashboard.vue
git commit -m "feat(dashboard): implement bento grid layout, hero net-worth card and integrated multi-currency strip"
```

---

### Task 4: 流水与交易记录轻量流式卡片化 (`Transactions.vue`, `DailyExpenses.vue`)

**Files:**
- Modify: `frontend/src/views/Transactions.vue`
- Modify: `frontend/src/views/DailyExpenses.vue`

- [ ] **Step 1: 升级 `Transactions.vue` 流水列表为流式卡片设计**

1. 将 `el-table` 样式升级为 `.stream-table`，消除粗重内边框。
2. 交易分类与扣款账户使用柔和色系小圆角胶囊（Pill Badges），取代粗糙纯文本。
3. 交易金额应用 `.mono-amount`，收入绿色微光加号，支出红色微光减号。
4. 顶部汇总卡与操作栏微调，主按钮与玻璃幽灵按钮明确分层。

- [ ] **Step 2: 升级 `DailyExpenses.vue` 与记账弹窗**

1. 收支记录列表应用 `.stream-table` 流式卡片行。
2. 记账弹窗（Dialog）中的金额输入框应用 `.hero-amount-input`，实现 28px 现代大字号居中输入体验。
3. 弹窗边框与背景适配深色毛玻璃。

- [ ] **Step 3: 运行自动化测试与前端编译**

Run: `npm --prefix frontend test && npm --prefix frontend run build`
Expected: 45 tests pass, Vite build passes.

- [ ] **Step 4: 提交代码**

Run:
```bash
git add frontend/src/views/Transactions.vue frontend/src/views/DailyExpenses.vue
git commit -m "feat(ledger): modernize transactions and daily expenses with stream tables and hero amount inputs"
```

---

### Task 5: 全面验证与构建走查 (Full Verification)

**Files:**
- Verify: Full frontend test suite
- Verify: Desktop & mobile production builds
- Verify: Backend test suite

- [ ] **Step 1: 运行前端 Vitest 单元测试**

Run: `npm --prefix frontend test`
Expected: 11 test files passed, 45 tests passed.

- [ ] **Step 2: 运行前端桌面端与移动端构建检查**

Run:
```bash
npm --prefix frontend run build
npm --prefix frontend run build:mobile
```
Expected: 两个构建任务均输出 `built in ...s`，0 报错。

- [ ] **Step 3: 运行后端回归测试**

Run:
```bash
cd backend/build && ctest --output-on-failure && cd ../..
```
Expected: 28 CTest 单元测试全绿通过。

- [ ] **Step 4: 提交最终工程改动并收尾**

Run:
```bash
git status
```
Expected: 工作区干净。
