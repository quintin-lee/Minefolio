# Frontend UI 高级感精修设计

**日期**: 2026-08-17
**状态**: 已审批
**范围**: 渐进式精修，不改 API 层与数据流

---

## 目标

在保持现有暗色赛博朋克主题不变的前提下，通过图标替换、微动效、布局精修和可视化增强，全面提升 Minefolio 前端页面的精致度与专业感。

---

## 技术方案

### 新增依赖

| 包名 | 用途 | 体积（gzip） |
|------|------|-------------|
| `@iconify/vue` | 图标渲染器 | ~4 KB |
| `@iconify-icons/ph` | Phosphor 线性图标集（Iconify 格式） | ~30 KB |

总计约 34 KB，对首屏加载影响可忽略。

---

## 升级节 1：全局基础

### 图标系统

移除所有 emoji 图标，统一替换为 Phosphor 线性图标（2px stroke，圆角端点）：

| 资产类型 | 原 emoji | Phosphor 图标 |
|----------|----------|---------------|
| bank | 🏦 | `Bank` |
| cash | 💵 | `Cash` |
| alipay | 📱 | `DeviceMobile` |
| wechat | 💬 | `ChatCircle` |
| credit_card | 💳 | `CreditCard` |
| stock/fund/bond/crypto | 📈📊🪙 | `TrendUp` |
| loan | 💸 | `ArrowDownLeft` |
| real_estate | 🏠 | `House` |
| 默认/unknown | 💼📦 | `Briefcase` |

侧边栏导航图标同样替换（DataAnalysis / Wallet / TrendCharts / List / Money / Folder / PieChart / Setting 等）。

### CSS 变量精修

在 `frontend/src/styles/index.css` 中新增：

```css
:root {
  /* 新增：细腻光晕层级 */
  --mf-glow-subtle: 0 0 8px rgba(0, 212, 255, 0.15);
  --mf-glow-medium: 0 0 16px rgba(0, 212, 255, 0.25);

  /* 新增：数字对齐 */
  --mf-font-mono: 'JetBrains Mono', 'Fira Code', monospace;
}
```

统一 transition timing：`cubic-bezier(0.4, 0, 0.2, 1)`。

### 微动效体系

| 场景 | 效果 | 时长 |
|------|------|------|
| 页面加载 | `fade-in-up`（opacity 0→1, translateY 10px→0） | 0.3s |
| 卡片 hover | `translateY(-2px)` + 光晕增强 | 0.3s |
| 表格行 hover | 左侧 2px 蓝色光条滑入 | 0.2s |
| 按钮点击 | `scale(0.97)` 回弹 | 0.15s |
| 数字变化 | count-up 动画（从 0 过渡到目标值） | 0.5s |
| Loading | 骨架屏替代 spinner | — |
| 路由切换 | 页面内容 fade-in-up | 0.3s |
| 卡片 stagger | 每张依次出现，间隔 60ms | — |

全局动画 CSS（新增至 `index.css`）：

```css
@keyframes mf-fade-in-up {
  from { opacity: 0; transform: translateY(10px); }
  to   { opacity: 1; transform: translateY(0); }
}

.mf-animate-in {
  animation: mf-fade-in-up 0.3s ease-out both;
}

.mf-stagger-1 { animation-delay: 0ms; }
.mf-stagger-2 { animation-delay: 60ms; }
.mf-stagger-3 { animation-delay: 120ms; }
.mf-stagger-4 { animation-delay: 180ms; }
```

---

## 升级节 2：仪表盘 Dashboard

### 统计卡片（4 张）

- 数字字号 `26px → 36px`，`letter-spacing: -1px`
- 右上角内嵌 SVG 迷你 sparkline（近 7 天净资产趋势）：数据直接复用现有 `/api/summary` 返回的 `trend: [{ date, net_worth }, ...]` 数组（后端已提供 30 天趋势），前端取最后 7 个点绘制
- 颜色系统：
  - 资产：青色发光 `#00d4ff`
  - 负债：红色 `#f87171`
  - 净资产：绿色 `#34d399`
  - 本月结余：琥珀色 `#fbbf24`
- "较上月 ±X%" 对比数据：当前 `/api/summary` 不返回上月值，**本期不做后端扩展**；该字段以占位符渲染（显示 `—`），后续单独 issue 扩展 API

### 图表区域

- **净资产趋势图**：高度 `280px → 320px`，加区域填充渐变（primary → transparent）
- **资产分布饼图**：标签外移加连接线，百分比直接标注在扇区上
- **年度收支柱状图**：收入=绿色，支出=红色，hover tooltip 显示精确数值

### 最近收支表格

- 简化为 3 列（日期 / 分类 / 金额）
- 金额列：右对齐，monospace，收入绿色 `+` / 支出红色 `-`
- 行高收紧，字体 `12px`

---

## 升级节 3：交易记录 Transactions

### 统计卡片（顶部汇总）

- 4 张卡片加左侧彩色竖条（2px 宽，对应青色/绿色/红色/琥珀色）
- 数字字号 `20px → 22px`，label 缩小到 `11px` 全大写 + letter-spacing

### 表格

- 行高 `44px`，首列图标+名称加粗
- 金额列：`font-variant-numeric: tabular-nums`，收入绿色 `/` 支出红色
- 操作列：改为 icon-only 按钮（编辑/删除），hover 显示 tooltip
- 首行加左侧蓝色光条（活跃行指示）

### 筛选面板

- 改为可折叠（默认收起，点击"筛选"展开）
- 折叠状态节省垂直空间约 80px

### 导出/导入按钮

- 改为 icon-only 小按钮组，放在新增交易按钮右侧，减少视觉噪音

---

## 升级节 4：资产列表 Assets + 持仓 Holdings

### Assets 页面

- 所有 emoji 替换为 Phosphor 图标（见节 1 映射表）
- 图标左侧加浅底色圆角矩形（`bg: rgba(0,212,255,0.06)`）
- 资产名称加粗，分类/币种列精简
- 金额列：统一右对齐 monospace，负值红色
- 操作列：icon-only 按钮组

### Holdings 页面

- **汇总卡片**：浮动盈亏卡片加进度条可视化（盈亏比例 → 绿/红进度条）
- **成本 vs 市值柱状图**：加宽，hover tooltip 显示精确数值
- **持仓表格**：
  - 份额/净值/市值三列右对齐 monospace
  - 盈亏列用颜色+箭头（↑绿 / ↓红）
  - 类型标签改为自定义 pill：`font-size: 11px; padding: 2px 8px; border-radius: 9999px; background: rgba(0,212,255,0.08); color: #00d4ff; border: 1px solid rgba(0,212,255,0.15);`
    - stock/fund/bond：青色 pill
    - crypto：紫色 pill（`background: rgba(124,58,237,0.08); color: #a78bfa; border-color: rgba(124,58,237,0.15)`）

---

## 升级节 5：报表 Reports

### 月度收支

- **收支趋势图**：高度 `280px`，收入/支出双色柱状（绿/红），hover 显示数值
- **分类饼图**：标签外移加连接线，百分比标注在扇区

### 投资回报

- **PnL 趋势线**：改用面积图（area fill），视觉重量更大
- **关键指标**：ROI、最大回撤等数据用 card 展示，数字大字号突出

### 表格类数据

- 统一 `tabular-nums` 数字对齐
- 分页器右下角，与表格间距加大

### 空状态

- 替换 `el-empty` 为 CSS 渐变圆 + 文字描述，与暗色主题融合

---

## 升级节 6：Layout 全局 + 其他

### 侧边栏

- Logo：💰 emoji 替换为 `Wallet` 图标 + 文字渐变
- 活跃项：左侧蓝色竖条高亮（替代背景色块）
- 图标尺寸：统一 `18px`

### 页面标题区

- `.title-accent` 竖条加宽到 `4px`，增强辉光

### 路由切换

- 页面内容加 `fade-in-up` 0.3s 进场动画
- 卡片 staggered 动画（每张间隔 60ms）

### 其他页面（Settings / AuditLogs / Setup / Login）

- 不做大改，仅统一按钮/输入框样式（沿用全局 CSS 变量与 transition 时长）
- Login 页增加 CSS gradient 缓慢移动的微光背景动画（`background-size: 400% 400%`，`animation: gradient-shift 15s ease infinite`）

---

## 文件变更清单

| 文件 | 变更类型 |
|------|----------|
| `frontend/src/styles/index.css` | 新增变量、动画 keyframes、全局样式精修 |
| `frontend/src/App.vue` | 路由切换 `<Transition>` wrapper，注入 fade-in-up |
| `frontend/src/views/Layout.vue` | Logo 图标、活跃项竖条高亮、侧边栏样式精修 |
| `frontend/src/views/Dashboard.vue` | 数字卡片放大、sparkline（复用 summary.trend）、图表配置 |
| `frontend/src/views/Transactions.vue` | 表格样式、筛选面板折叠、统计卡片竖条 |
| `frontend/src/views/Assets.vue` | emoji→Phosphor 图标、行样式精修 |
| `frontend/src/views/Holdings.vue` | 盈亏进度条、表格数字对齐、类型 pill 样式 |
| `frontend/src/views/Reports.vue` | 图表配置增强、空状态 CSS 渐变圆 |
| `frontend/src/views/Login.vue` | 背景渐变动画 |
| `frontend/package.json` | 新增 `@iconify/vue`、`@iconify-icons/ph` |

---

## 验收标准

1. 所有 emoji 图标已替换为 Phosphor 图标
2. 页面加载时有 fade-in-up 动画
3. 卡片 hover 有 translateY + 光晕效果
4. 表格行 hover 有左侧光条
5. 数字使用 monospace 字体，tabular-nums 对齐
6. Dashboard 统计卡片有迷你 sparkline
7. Holdings 盈亏有进度条可视化
8. 构建无 TypeScript 错误，backend 测试通过（`bash backend/tests/test_link.sh`）
