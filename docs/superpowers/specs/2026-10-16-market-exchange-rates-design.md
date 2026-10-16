# 实时汇率展示中心与行情中心架构设计规范 (Market Hub & Real-time FX Rates Design)

## 1. 概述与背景 (Overview & Background)

Minefolio 作为一个支持多币种（USD、HKD、EUR、JPY、GBP 等）与多账户类型的个人财富与投资管理平台，后端已具备完善的外汇牌价同步、汇率历史快照、多币种资产损益折算能力。但此前前端缺乏一个统一、专业且交互直观的**实时汇率与外汇展示页面**。

同时，考虑到系统未来将进一步接入**全球各类指数大盘（如上证指数、标普500、纳斯达克、恒生指数、日经225、加密货币 BTC/ETH 等）**，本设计采用统一的**「行情中心 (Market Hub)」**顶层架构，使本次核心交付的「实时外汇与换算」与后续规划的「全球指数大盘」无缝融合，避免导航碎片化与接口割裂。

## 2. 总体架构与扩展性设计 (Architecture & Extensibility)

### 2.1 顶层导航与路由规范
- **路由路径**：`/market`
- **菜单名称**：`t('nav.market')`（中文：「行情中心」/ 英文：「Market Hub」）
- **侧边栏入口**：位于桌面端 `src/views/Layout.vue` 的「资产概览 (Assets Overview)」分组中，采用 Iconify 经典金融图标 `ph:chart-line-up-duotone`。
- **URL 查询参数支持**：支持 `/market?tab=rates` 与 `/market?tab=indices`，页面刷新或分享链接可直接定位当前激活 Tab。

### 2.2 行情中心 Tab 模块划分 (开闭原则)
`src/views/Market.vue` 作为顶层容器视图，提供统一样式的页头、全局状态控制栏与 Tab 切换机制：
1. **`💱 实时外汇与换算` (Tab 1 - 本次完整实现)**：
   包含主要法币实时牌价卡片阵列、双向即时外汇换算器、多周期 ECharts 历史走势图、个人多币种持仓分布透视以及手动汇率覆盖配置。
2. **`📈 全球指数大盘` (Tab 2 - 预留插槽契约)**：
   预留 `GlobalIndicesPlaceholder.vue` 组件，展示规划中全球大盘特性介绍与标准契约接口，未来实现指数大盘时零重构无缝接入。
3. **`⚙ 数据源与同步设置` (Tab 3 - 快速配置入口)**：
   复用现有行情数据源设置组件（`/api/market/settings`），支持配置 Yahoo/东方财富代理与定时同步频率。

### 2.3 交付范围边界 (Desktop First)
- **桌面端优先**：本次实现聚焦于高质量交付桌面端完整大屏体验（多列响应式卡片网格、自适应 ECharts 图表、双向换算器）。
- **移动端解耦**：移动端暂不添加该复杂页面，保持现有移动端架构稳定，未来通过独立规划实现移动端轻量版。

---

## 3. 前端组件拆分与 UI 交互设计 (Component Hierarchy & UI)

组件统一存放于 `frontend/src/components/market/`：

```
frontend/src/
├── views/
│   └── Market.vue                      # 行情中心顶层主页面（Tab 切换、全局状态、基准币选择器）
├── components/market/
│   ├── FxRateCards.vue                 # 主流法币实时牌价网格卡片
│   ├── FxConverter.vue                 # 交互式即时双向换算器
│   ├── FxTrendChart.vue                # ECharts 汇率历史走势图与区间统计
│   ├── FxHoldingsSummary.vue           # 个人外币持仓分布与汇兑损益透视
│   ├── FxRateEditDialog.vue            # 手动设定与覆盖汇率对话框
│   └── GlobalIndicesPlaceholder.vue    # 全球指数大盘（规划中插槽占位）
```

### 3.1 顶层页头与控制栏 (`Market.vue`)
- **标题与 Tab**：居左展示「行情中心」标题与 Tab 切换标签。
- **本位币选择器 (Base Currency Dropdown)**：
  - 支持动态切换本位币（`CNY`、`USD`、`EUR`、`HKD`、`JPY`、`GBP` 等），默认取 `CNY`。
  - 选择后全页面牌价、换算基准、走势图及持仓折算即刻响应重算。
- **最后更新时间**：显示 `最后同步于 HH:mm:ss`。
- **全局同步操作**：提供「🔄 同步最新行情」按钮，调用后端 `/api/market/sync`，并带有加载中动画。
- **自定义汇率入口**：提供「⚙ 自定义汇率」按钮打开编辑弹窗。

### 3.2 主流法币实时牌价卡片 (`FxRateCards.vue`)
- **支持币种**：内置主流国际货币：`USD`（美元）、`EUR`（欧元）、`HKD`（港币）、`JPY`（日元/百日元）、`GBP`（英镑）、`AUD`（澳元）、`CAD`（加元）、`SGD`（新加坡元）。
- **动态交叉盘计算**：
  $$\text{CrossRate}(\text{Target} / \text{Base}) = \frac{\text{Rate}(\text{Target} / \text{CNY})}{\text{Rate}(\text{Base} / \text{CNY})}$$
  若所选本位币即为目标币自身，汇率直接展示为 `1.0000`。
- **卡片信息**：国旗 Emoji / 货币代码 / 实时汇率（保留4位小数，低面值货币展示百单位或自适应精度）/ 24h涨跌幅徽标。
- **卡片点击交互**：点击任意币种卡片，下方的换算器与趋势图自动聚焦该币种。

### 3.3 交互式双向换算器 (`FxConverter.vue`)
- **双向金额输入**：
  - 上方：源币种金额输入框 + 源币种下拉选择（带货币图标与检索）。
  - 中间：一键互换按钮（⇅），点击平滑交换源币种与目标币种及金额。
  - 下方：目标币种计算结果框 + 目标币种下拉选择。
- **快捷金额标签**：提供 `100`、`500`、`1,000`、`5,000`、`10,000` 预设胶囊按钮，点击快速载入源金额。
- **基准比率提示条**：实时展示互为倒数的对换公式（如 $1\text{ USD} = 7.2345\text{ CNY}$，以及 $1\text{ CNY} = 0.1382\text{ USD}$）。
- **一键复制**：支持将计算结果金额一键复制到剪贴板。

### 3.4 ECharts 汇率历史趋势图 (`FxTrendChart.vue`)
- **货币对选择**：下拉选择待分析的目标外币（默认展示当前聚焦货币）。
- **周期维度切换**：提供 `7天`、`30天`、`90天`、`1年` 胶囊按钮，调用 `/api/market/fx-history` 异步载入。
- **区间统计指标栏**：图表顶部实时计算并展示：
  - 区间最高价 (Period High)
  - 区间最低价 (Period Low)
  - 区间平均价 (Period Average)
  - 区间波动幅度 / 涨跌率 (Amplitude & Change %)
- **图表视觉规范**：
  - 采用 Element Plus 主题自适应色彩的 Area 渐变面积平滑曲线。
  - 支持十字准星 (Crosshair) 与智能日期/数值 Tooltip。
  - 支持暗色模式 (Dark Mode) 与亮色模式 (Light Mode) 动态重绘。

### 3.5 个人外币资产配置与汇兑损益 (`FxHoldingsSummary.vue`)
- **多币种透视**：调用 `/api/reports/multi-currency-summary?base_currency={baseCurrency}`。
- **关键卡片**：
  - 外币持仓折合总值（本位币计价）。
  - 外币占个人总净资产比例（%）。
  - 纯外汇波动汇兑收益（FX PnL）与汇率回报率（%）。
- **持币分布明细**：表格或网格展示用户在不同外币账户下的资产余额与折合价值。

### 3.6 手动设定汇率对话框 (`FxRateEditDialog.vue`)
- 允许用户手动输入并覆盖指定外币对基准币的汇率数值，调用 `POST /api/market/exchange-rates`，便于离线断网或特殊换汇成本锁定。

---

## 4. 状态管理与数据流设计 (State Management & Data Flow)

### 4.1 Pinia Store (`src/stores/market.ts`)
```ts
interface MarketState {
  baseCurrency: string                  // 当前选定本位币 (默认 'CNY')
  rates: Record<string, number>        // 各币种对 CNY 的基准汇率表
  lastSyncAt: string | null            // 最近全网同步时间戳
  isSyncing: boolean                   // 是否正在同步中
  selectedCurrency: string             // 当前聚焦的外币 (默认 'USD')
  historyDays: number                  // 历史走势查询天数 (7, 30, 90, 365)
  historyPoints: FxHistoryPoint[]      // 当前历史走势数据
  isLoadingHistory: boolean            // 走势加载状态
}
```

### 4.2 接口协同与并发容错
- **并发初始加载**：采用 `Promise.allSettled`，分别请求 `/market/exchange-rates`、`/reports/multi-currency-summary` 与 `/market/fx-history`。任意单个接口异常不会引发白屏，各自维护错误回退。
- **请求竞态防护 (Request Sequence Guard)**：
  在切换图表币种或天数时维护递增请求序列号 `reqSeq`。当网络请求响应时，若请求发起时的 `seq` 小于当前最新 `reqSeq`，自动丢弃旧结果，杜绝慢请求数据污染界面。
- **离线持久化回退**：
  将最新的 `rates` 与 `baseCurrency` 缓存至 `localStorage`。断网或离线启动时平滑读取缓存，并展示温和提示。

---

## 5. 异常处理与边缘情况 (Edge Cases & Safety)

1. **除零与无效汇率防护**：计算交叉汇率与换算时，强制验证分母非零；若数据缺失，安全降级返回 `1.0`，严防 `NaN` 或 `Infinity`。
2. **精度与舍入**：汇率计算采用定点截断与舍入工具，显示格式规范化，避免 JavaScript 浮点数长尾（如 `0.1 + 0.2 = 0.30000000000000004`）。
3. **时区一致性**：汇率历史时间点解析遵循 `AGENTS.md` 规范，使用 `src/utils/format.ts` 本地时间函数，杜绝跨时区日期偏移 1 天的问题。

---

## 6. 测试与验证计划 (Verification Plan)

### 6.1 前端单元测试 (Vitest)
- 创建 `frontend/tests/market-rates.spec.ts`：
  - 测试交叉汇率计算逻辑（`CNY`、`USD`、`EUR` 互换）；
  - 测试即时双向换算器金额逻辑与快捷金额联动；
  - 测试除零防崩保护与异常数据回退；
  - 测试历史图表请求防竞态序列守卫。

### 6.2 编译与代码健康度门禁
- 运行 `npm --prefix frontend run build`（严格执行 `vue-tsc -b && vite build`，确保 0 TS 错误）。
- 运行 `npm --prefix frontend test`（确保全部单元测试通过）。

### 6.3 端到端功能走查
- 验证侧边栏导航跳转 `/market`。
- 验证本位币动态切换（CNY -> USD -> EUR）。
- 验证汇率卡片点击与换算器、历史走势图联动。
- 验证手动汇率覆盖与全网行情同步功能正常。
