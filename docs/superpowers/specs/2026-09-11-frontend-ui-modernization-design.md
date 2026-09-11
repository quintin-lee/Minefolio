# 前端页面美化与现代化设计规范 (Frontend UI Modernization Design Spec)

- **日期**: 2026-09-11
- **状态**: Approved by User
- **目标**: 将 Minefolio 前端打造为现代高端金融科技风（Modern FinTech / Bloomberg + Linear 质感），提升整体视觉质感、数据可读性与操作流畅度。

---

## 1. 目标与背景 (Goals & Background)

Minefolio 现已具备完整的个人财务与投资追踪能力（收支、持仓、多币种、DCA 定投、AI 助手）。前端基于 Vue 3 + TypeScript + Element Plus + ECharts。
当前视觉风格虽然已有暗色主题，但在现代金融质感、边框光效、等宽数字排版、图表渐变与信息层级上仍有显著的提升空间。

本次美化遵循**渐进式分层系统升级（Approach A）**，确保在不破坏现有功能逻辑和 API 调用的前提下，实现全站视觉体验的质的飞跃。

---

## 2. 设计规范与系统架构 (Design System Architecture)

```
┌─────────────────────────────────────────────────────────────┐
│                      用户界面 (Views)                       │
│   Dashboard  │  Holdings  │  Transactions  │  Reports ...   │
├─────────────────────────────────────────────────────────────┤
│                    共享原子组件 (Atoms)                     │
│   SummaryCard  │  ECharts Wrappers  │  TrendBadges ...      │
├─────────────────────────────────────────────────────────────┤
│            组件库深度定制 (Component Overrides)             │
│   el-table  │  el-button  │  el-dialog  │  el-input ...     │
├─────────────────────────────────────────────────────────────┤
│                底层设计令牌 (Design Tokens)                 │
│   Surfaces  │  Borders/Glow  │  Colors  │  Tabular Numbers  │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. 详细设计 (Detailed Specifications)

### 3.1 底层设计令牌 (Design Tokens: `index.css`)
- **深空夜色背景**:
  - `--mf-background`: `#080c14`（深邃且具科技感的冷黑，顶端附带微妙幽蓝径向渐变环境光）。
  - `--mf-surface-card`: `rgba(15, 23, 42, 0.72)`（配合 `backdrop-filter: blur(16px)` 半透磨砂毛玻璃）。
- **微发光边框与光影**:
  - `--mf-border-subtle`: `rgba(255, 255, 255, 0.07)`（细致低饱和边框）。
  - `--mf-border-hover`: `rgba(99, 102, 241, 0.4)`（悬停时激活幽兰微光）。
  - `--mf-shadow-glow`: `0 0 20px rgba(59, 130, 246, 0.18)`。
- **金融色彩体系**:
  - 核心主色: `--mf-primary: #3b82f6`，辅助强调色: `--mf-accent: #6366f1`。
  - 盈利/流入: `--mf-success: #10b981`，微透底色 `rgba(16, 185, 129, 0.12)`。
  - 亏损/支出: `--mf-danger: #f43f5e`，微透底色 `rgba(244, 63, 94, 0.12)`。
  - 警示/待办: `--mf-warning: #f59e0b`。
- **排版与金融数字**:
  - 针对所有金额、收益率、净值和数量，启用 `font-variant-numeric: tabular-nums`，确保小数点与各列数字垂直对齐，杜绝数字跳动。
  - 主数字字号采用 28px/32px 加粗（字重 600-700），副币种符号弱化。

### 3.2 共享原子组件与组件库重塑
- **指标卡片 (`SummaryCard.vue`)**:
  - 右上角加入匹配卡片语义的微网格光晕（Subtle Radial Glow）。
  - 环比/涨跌幅采用现代圆角胶囊（Trend Pill），包含微型方向箭头与半透明背景。
  - 卡片 Hover 上浮微动效（`translateY(-2px)`），边框由暗灰平滑过渡至科技蓝发光。
- **Element Plus 组件重塑**:
  - `el-table`: 半透明低对比度表头、行悬停柔和玻璃高亮、微圆角外框、精简多余边线。
  - `el-button`: 主按钮科技感电光蓝微渐变、点击缩放 `scale(0.98)`；幽灵按钮透明悬浮发光。
  - `el-dialog` / `el-drawer`: 16px 圆角深色毛玻璃，遮罩层 `backdrop-filter: blur(8px)`。
  - `el-input` / `el-select`: 去除生硬白边框，聚焦时呈现精致的蓝紫微光聚焦环（Focus Ring）。

### 3.3 核心看板与数据可视化
- **仪表盘 (`Dashboard.vue`)**:
  - 顶部指标行清晰呈现「净资产（重点发光）」、「总资产」、「总负债」、「本月净现金流」。
  - 净资产走势大图采用平滑曲线（Smooth Spline），下方填充垂直半透明线性渐变。
  - 资产大类分布采用高质感镂空甜甜圈（Donut Chart），中心浮显配置总览，右侧配置彩色进度条图例。
- **持仓报表 (`Holdings.vue`)**:
  - 顶部投资盈亏看板，根据浮动盈亏正负自动应用红绿光效。
  - 专业双行标的排版（主标题标的名 + 副标题等宽代码）。
  - 新增标的持仓权重微型进度条（Weight Bar）。
  - 行情同步状态微呼吸灯。

### 3.4 交互流与体验细节
- **骨架屏（Skeleton）**: 切换账本或刷新数据时使用暗暗波动的骨架屏平滑占位，消除布局抖动与白屏。
- **平滑过渡**:
  - 路由切换: `opacity: 0 -> 1; translateY(4px -> 0px)`（0.18s）。
  - 主题切换: 平滑颜色渐变过渡，明暗模式色彩精准互映射。
- **全局布局 (`Layout.vue`)**:
  - 侧边栏折叠弹性平滑动效，激活菜单项配置右侧微发光指示条。
  - 顶部「快速记账」按钮设计为突出发光主操作按钮，优化高频录入体验。

---

## 4. 验证与测试策略 (Verification Strategy)
- **静态类型与构建检查**:
  - `npm --prefix frontend run build`（`vue-tsc -b && vite build`，必须 0 错误）。
  - `npm --prefix frontend run build:mobile`（移动端适配打包，必须 0 错误）。
- **自动化测试回归**:
  - `npm --prefix frontend test`（Vitest 45 项单元测试 100% 通过）。
  - 后端 CTest 与集成测试保持全绿通过。
- **视觉一致性走查**:
  - 检查深色模式（Dark）与明亮模式（Light）下的对比度、边框清晰度与字号可读性。
