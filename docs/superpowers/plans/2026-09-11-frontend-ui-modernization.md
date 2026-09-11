# 前端页面美化与现代化实施计划 (Frontend UI Modernization Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 按照规范全面升级 Minefolio 前端视觉系统为现代高端金融科技风（Modern FinTech / Bloomberg + Linear 质感），涵盖 Design Tokens、组件库定制、SummaryCard 原子组件、Dashboard 大盘、Holdings 看板及微动效，同时保持前端构建与单元测试 100% 通过。

**Architecture:** 采取自底向上的分层递进重构：Design Tokens 层 (`index.css`) -> 基础组件覆盖与共享原子组件 (`SummaryCard.vue`, `echarts-theme.ts`) -> 核心页面视图 (`Dashboard.vue`, `Holdings.vue`, `Layout.vue`) -> 全局过渡动效与构建验证。

**Tech Stack:** Vue 3 (Composition API, `<script setup>`), TypeScript, Element Plus, ECharts, Phosphor Icons (`@iconify/vue`), Vite, Vitest.

---

### File Structure Map

| 文件路径 | 职责 |
|---|---|
| `frontend/src/styles/index.css` | 全局 Design Tokens、深空夜色背景、发光边框、等宽数字、Element Plus 表格/按钮/输入框深度定制 |
| `frontend/src/components/SummaryCard.vue` | 核心数据指标卡：微网格光晕、趋势胶囊徽章、悬浮上浮、等宽数字 |
| `frontend/src/utils/echarts-theme.ts` | ECharts 金融配色、平滑渐变面积填充辅助函数、暗黑磨砂 Tooltip 样式 |
| `frontend/src/views/Dashboard.vue` | 资产总览大屏：4 列核心指标网格、平滑渐变折线走势图、细环甜甜圈资产配置图 |
| `frontend/src/views/Holdings.vue` | 持仓分析大屏：盈亏看板光感、双行标的代码排版、持仓权重进度条、行情状态灯 |
| `frontend/src/views/Layout.vue` | 侧边栏发光指示条、顶部突出「快速记账」CTA 按钮、页面淡入平滑切换动效 |

---

### Task 1: 升级 Design Tokens 与基础排版 (`index.css`)

**Files:**
- Modify: `frontend/src/styles/index.css`

- [ ] **Step 1: 完善暗黑夜空背景、微发光边框与金融等宽数字**

在 `frontend/src/styles/index.css` 中更新 `:root, [data-theme="dark"]` 的核心变量：
- 将 `--mf-background` 优化为 `#080c14`，并在 `body` 加入幽蓝径向渐变背景：
  ```css
  background-color: var(--mf-background);
  background-image: radial-gradient(circle at 50% 0%, rgba(59, 130, 246, 0.07) 0%, transparent 60%);
  ```
- 调整 `--mf-surface-card` 为 `rgba(15, 23, 42, 0.72)`，配合 `backdrop-filter: blur(16px)`。
- 引入全局 `.tabular-nums` 类以及针对金额展示类的等宽数字特性：
  ```css
  .tabular-nums,
  .amount-text,
  .pnl-text,
  .balance-text {
    font-variant-numeric: tabular-nums;
    font-feature-settings: "tnum" 1;
  }
  ```
- 调整盈亏色彩：
  - 收益/流入: `--mf-success: #10b981;`，配套透底色 `rgba(16, 185, 129, 0.12)`。
  - 亏损/支出: `--mf-danger: #f43f5e;`，配套透底色 `rgba(244, 63, 94, 0.12)`。

- [ ] **Step 2: 深度定制 Element Plus 表格、按钮与输入框**

在 `frontend/src/styles/index.css` 的组件覆盖部分增强：
- `el-table`:
  ```css
  .el-table {
    --el-table-header-bg-color: rgba(15, 23, 42, 0.6);
    --el-table-tr-bg-color: transparent;
    --el-table-row-hover-bg-color: rgba(59, 130, 246, 0.06);
    --el-table-border-color: var(--mf-border-subtle);
    border-radius: var(--mf-radius-md);
  }
  .el-table th.el-table__cell {
    font-size: 12px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.04em;
    color: var(--mf-text-muted);
    border-bottom: 1px solid var(--mf-border-subtle) !important;
  }
  ```
- `el-button--primary`:
  ```css
  .el-button--primary {
    background: linear-gradient(135deg, #3b82f6 0%, #2563eb 100%) !important;
    border: none !important;
    box-shadow: 0 2px 10px rgba(59, 130, 246, 0.3) !important;
    transition: var(--mf-transition) !important;
  }
  .el-button--primary:hover {
    box-shadow: 0 4px 16px rgba(59, 130, 246, 0.45) !important;
    transform: translateY(-1px);
  }
  .el-button--primary:active {
    transform: translateY(0) scale(0.98);
  }
  ```
- `el-input__wrapper`, `el-select__wrapper`:
  ```css
  .el-input__wrapper,
  .el-select__wrapper {
    background-color: rgba(15, 23, 42, 0.6) !important;
    border: 1px solid var(--mf-border-subtle) !important;
    box-shadow: none !important;
    transition: var(--mf-transition) !important;
  }
  .el-input__wrapper.is-focus,
  .el-select__wrapper.is-focused {
    border-color: var(--mf-primary) !important;
    box-shadow: 0 0 0 2px rgba(59, 130, 246, 0.25) !important;
  }
  ```

- [ ] **Step 3: 运行验证命令**

运行：`npm --prefix frontend test && npm --prefix frontend run build`
预期：全部通过，0 编译错误。

- [ ] **Step 4: 提交代码**

```bash
git add frontend/src/styles/index.css
git commit -m "style: modernize design tokens, surfaces, tabular numbers and element-plus styles"
```

---

### Task 2: 升级指标卡原子组件 (`SummaryCard.vue`)

**Files:**
- Modify: `frontend/src/components/SummaryCard.vue`

- [ ] **Step 1: 增加微网格光晕、趋势胶囊 Badge 与悬浮动效**

在 `frontend/src/components/SummaryCard.vue` 中：
- 增加卡片类型支持（`variant?: 'primary' | 'success' | 'danger' | 'warning' | 'default'`，或者根据 props 智能渲染发光色）。
- 在卡片右上角增加环境光晕节点 `<div class="card-glow-mesh" :class="variantClass" />`。
- 将原本的 `subtext` / `badge` 升级为带上/下箭头胶囊徽章（`<div class="trend-capsule">`），如果是正向（如 `+12.5%`）使用翠绿色高亮，负向使用玫瑰红高亮。
- 在金额数值外层容器添加 `.tabular-nums`，确保数字等宽。
- 添加 CSS 上浮与微光阴影交互：
  ```css
  .summary-card {
    position: relative;
    overflow: hidden;
    transition: transform 0.22s cubic-bezier(0.4, 0, 0.2, 1),
                box-shadow 0.22s cubic-bezier(0.4, 0, 0.2, 1),
                border-color 0.22s ease;
  }
  .summary-card:hover {
    transform: translateY(-2px);
    border-color: var(--mf-border-hover);
    box-shadow: var(--mf-shadow-glow);
  }
  .card-glow-mesh {
    position: absolute;
    top: -20px;
    right: -20px;
    width: 100px;
    height: 100px;
    border-radius: 50%;
    filter: blur(40px);
    pointer-events: none;
    opacity: 0.25;
  }
  .card-glow-mesh.is-primary { background: var(--mf-primary); }
  .card-glow-mesh.is-success { background: var(--mf-success); }
  .card-glow-mesh.is-danger  { background: var(--mf-danger); }
  ```

- [ ] **Step 2: 运行测试与构建检查**

运行：`npm --prefix frontend test && npm --prefix frontend run build`
预期：PASS，0 报错。

- [ ] **Step 3: 提交代码**

```bash
git add frontend/src/components/SummaryCard.vue
git commit -m "feat(ui): upgrade SummaryCard with subtle glow, trend capsule and hover lift"
```

---

### Task 3: 升级 ECharts 金融配色与辅助工具 (`echarts-theme.ts`)

**Files:**
- Modify: `frontend/src/utils/echarts-theme.ts`

- [ ] **Step 1: 提供现代金融面积渐变与毛玻璃 Tooltip 生成器**

在 `frontend/src/utils/echarts-theme.ts` 中扩展工具函数：
- 导出 `makeAreaGradient(colorRgb: string, topAlpha = 0.28, bottomAlpha = 0.0)`：
  ```ts
  export function makeAreaGradient(colorRgb: string, topAlpha = 0.28, bottomAlpha = 0.0) {
    return {
      type: 'linear' as const,
      x: 0, y: 0, x2: 0, y2: 1,
      colorStops: [
        { offset: 0, color: `rgba(${colorRgb}, ${topAlpha})` },
        { offset: 1, color: `rgba(${colorRgb}, ${bottomAlpha})` },
      ],
    }
  }
  ```
- 导出通用的 `modernTooltipConfig(palette: MfChartPalette)`：
  ```ts
  export function modernTooltipConfig(palette: MfChartPalette) {
    return {
      trigger: 'axis' as const,
      backgroundColor: palette.isLight ? 'rgba(255, 255, 255, 0.95)' : 'rgba(15, 23, 42, 0.92)',
      borderColor: palette.borderSubtle,
      borderWidth: 1,
      textStyle: { color: palette.textMain, fontSize: 13 },
      padding: [10, 14],
      extraCssText: 'box-shadow: 0 8px 32px rgba(0, 0, 0, 0.36); backdrop-filter: blur(8px); border-radius: 8px;',
    }
  }
  ```

- [ ] **Step 2: 运行单元测试与构建验证**

运行：`npm --prefix frontend test && npm --prefix frontend run build`
预期：PASS。

- [ ] **Step 3: 提交代码**

```bash
git add frontend/src/utils/echarts-theme.ts
git commit -m "feat(charts): add modern area gradient and glassmorphism tooltip helper"
```

---

### Task 4: 重构仪表盘大屏 (`Dashboard.vue`)

**Files:**
- Modify: `frontend/src/views/Dashboard.vue`

- [ ] **Step 1: 升级顶部 KPI 指标行与高亮样式**

在 `frontend/src/views/Dashboard.vue` 中：
- 将「净资产」卡片设置为主要光晕卡片（`variant="primary"`），数值字号强化（加粗大字），右侧放置本月增减微型徽章。
- 其余 3 张卡片分别配置对应光晕（总资产配置 `variant="primary"`，总负债配置 `variant="warning"`，本月净收益配置 `variant="success"` 或 `variant="danger"`）。
- 确保金额数字绑定 `.tabular-nums`。

- [ ] **Step 2: 优化净资产走势曲线与资产甜甜圈图**

- 净资产走势图配置：
  - 曲线启用 `smooth: 0.35` 平滑曲线，线宽 `3`，颜色为 `--mf-primary`。
  - 下方填充调用 `makeAreaGradient('59, 130, 246', 0.28, 0.0)`。
  - 坐标网格网线设置为微弱虚线：`splitLine: { lineStyle: { color: palette.borderSubtle, type: 'dashed' } }`。
  - 启用 `modernTooltipConfig(palette)`。
- 资产分布甜甜圈图配置：
  - 调整半径 `radius: ['60%', '82%']`，圆角扇区 `itemStyle: { borderRadius: 4, borderColor: palette.surfaceCard, borderWidth: 2 }`。
  - 中心展示总资产汇总与类别数量。

- [ ] **Step 3: 运行验证命令**

运行：`npm --prefix frontend test && npm --prefix frontend run build`
预期：PASS。

- [ ] **Step 4: 提交代码**

```bash
git add frontend/src/views/Dashboard.vue
git commit -m "feat(dashboard): modernize net worth trend chart and asset breakdown donut"
```

---

### Task 5: 重构持仓分析大屏 (`Holdings.vue`)

**Files:**
- Modify: `frontend/src/views/Holdings.vue`

- [ ] **Step 1: 升级顶部投资盈亏统计面板**

在 `frontend/src/views/Holdings.vue` 中：
- 顶部汇总条（总市值、持仓成本、浮动盈亏、已实现盈亏）升级为现代指标卡布局。
- 浮动盈亏卡片根据数值正负动态展示发光光斑与专属色彩（正数高光翡翠绿、负数暗玫瑰红）。

- [ ] **Step 2: 优化持仓列表表格（双行标的代码、持仓权重进度条、行情状态灯）**

- 标的列排版：
  ```html
  <div class="symbol-cell">
    <div class="symbol-name">{{ row.name }}</div>
    <div class="symbol-code font-mono text-muted">{{ row.symbol || '-' }}</div>
  </div>
  ```
- 市值列旁加入持仓占比进度条（Weight Bar）：
  ```html
  <div class="weight-cell">
    <span class="weight-pct tabular-nums">{{ (row.weight * 100).toFixed(1) }}%</span>
    <div class="weight-bar-track">
      <div class="weight-bar-fill" :style="{ width: `${Math.min(row.weight * 100, 100)}%` }" />
    </div>
  </div>
  ```
- 盈亏列与收益率使用 `.tabular-nums`，统一带符号显示（`+1,200.00` / `-350.00`）。
- 增加行情同步状态微型呼吸灯（彩色小点加淡光晕）。

- [ ] **Step 3: 运行测试与构建验证**

运行：`npm --prefix frontend test && npm --prefix frontend run build`
预期：PASS。

- [ ] **Step 4: 提交代码**

```bash
git add frontend/src/views/Holdings.vue
git commit -m "feat(holdings): add Bloomberg-style symbol cell, weight bars and pnl badges"
```

---

### Task 6: 优化全局布局、快速记账与过渡动效 (`Layout.vue`)

**Files:**
- Modify: `frontend/src/views/Layout.vue`

- [ ] **Step 1: 侧边栏选中态指示胶囊与折叠动效**

在 `frontend/src/views/Layout.vue` 中：
- 侧边栏激活项增加右侧（或左侧）微发光指示条：
  ```css
  .el-menu-item.is-active::after {
    content: '';
    position: absolute;
    right: 0;
    top: 50%;
    transform: translateY(-50%);
    width: 3px;
    height: 18px;
    background: var(--mf-primary);
    border-radius: 3px 0 0 3px;
    box-shadow: 0 0 8px var(--mf-primary);
  }
  ```
- 导航图标在激活时增加微发光效果。

- [ ] **Step 2: 顶部「快速记账」突出化与页面路由平滑切换动效**

- 优化顶部 Header 中的「快速记账」按钮样式，使其成为主视觉焦点（Primary Glow CTA）。
- 在 `<router-view>` 外层包裹 `<transition name="fade-slide" mode="out-in">`：
  ```css
  .fade-slide-enter-active,
  .fade-slide-leave-active {
    transition: opacity 0.18s cubic-bezier(0.4, 0, 0.2, 1),
                transform 0.18s cubic-bezier(0.4, 0, 0.2, 1);
  }
  .fade-slide-enter-from {
    opacity: 0;
    transform: translateY(4px);
  }
  .fade-slide-leave-to {
    opacity: 0;
    transform: translateY(-4px);
  }
  ```

- [ ] **Step 3: 运行测试与全量构建检查**

运行：
```bash
npm --prefix frontend test
npm --prefix frontend run build
npm --prefix frontend run build:mobile
```
预期：全部通过，0 错误。

- [ ] **Step 4: 提交代码**

```bash
git add frontend/src/views/Layout.vue
git commit -m "feat(layout): add active menu indicator bar, glowing quick-add CTA and page fade transition"
```

---

### Task 7: 完整回归与全端验证 (Full End-to-End Verification)

**Files:**
- All modified files

- [ ] **Step 1: 运行全套后端测试确保未受任何影响**

```bash
cd backend/build && ctest --output-on-failure && cd ../..
./backend/tests/test_link.sh
```
预期：28 CTest 全部通过，test_link 147 项断言全部通过。

- [ ] **Step 2: 运行全套前端测试与多端打包**

```bash
npm --prefix frontend test
npm --prefix frontend run build
npm --prefix frontend run build:mobile
```
预期：45 项前端测试全部通过，桌面端与移动端生产打包均成功。

- [ ] **Step 3: 检查 Git 工作区状态**

```bash
git status -s
```
预期：干净的工作区。
