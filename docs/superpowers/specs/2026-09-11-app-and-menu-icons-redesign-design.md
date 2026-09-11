# 全新应用主图标与菜单图标视觉重构设计规范

- **日期**: 2026-09-11
- **状态**: Approved
- **设计主题**: 现代金融科技风 (FinTech Cyber) 应用品牌标识与跨端统一菜单图标体系
- **影响范围**:
  - 应用图标组件与静态资源: `frontend/src/components/AppLogo.vue`, `frontend/public/favicon.svg`, `frontend/public/site.webmanifest`
  - 桌面端主框架: `frontend/src/views/Layout.vue`
  - 移动端主框架: `frontend/src/views-mobile/MobileLayout.vue`
  - 认证与初始化页面: `frontend/src/views/Login.vue`, `frontend/src/views/Setup.vue`

---

## 1. 背景与目标 (Background & Goals)

### 1.1 现状与问题分析
1. **应用主图标 (App Logo / Favicon) 视觉脱节**：
   - 现行 `favicon.svg` 为早期基础图素（传统圆金币 + 简单折线），风格偏写实拟物，与当前现代金融科技（FinTech Bento & Cyber）全局视觉语言不一致；
   - 桌面端侧边栏左上角暂用通用的 Phosphor 钱包图标（`ph:wallet`）充当临时 Logo；
   - 登录页与安装引导页标题仅展示纯渐变文字，缺乏统一的品牌视觉锚点。
2. **菜单图标 (Menu Icons) 跨端割裂与语义粗放**：
   - 桌面端（`Layout.vue`）使用 Phosphor 单线图标，部分菜单语义过于宽泛（如：用普通的折线代表总览监控看板，用简单列表代表交易资金流水，用日历代表周期投资计划）；
   - 移动端（`MobileLayout.vue`）底部 Tab 栏直接使用 Element Plus 内置图标库（`DataAnalysis`, `Plus`, `Wallet`, `Calendar` 等），与桌面端图标库、线条粗细、视觉重量存在割裂；
   - 现有单线图标在深色模式与暗色卡片上视觉重心较弱，缺乏双色透明衬底的层次感。

### 1.2 改造目标
1. **打造高辨识度专属 Brand Logo**：
   - 融合 **“M”**（Mine + Portfolio）几何立体折面与**昂扬冲天的金融趋势折线箭头**，象征资产积累与复利增值；
   - 适配深色/浅色模式与全尺寸缩放（16px~512px）；
   - 封装高复用 `<AppLogo>` 组件，贯通侧边栏、登录页与初始化页。
2. **构建跨端统一的 Phosphor Duotone 双色微质感菜单系统**：
   - 桌面端与移动端全面统一采用 `@iconify/vue` 的 Phosphor Duotone 图标族；
   - 重塑 12 个核心功能路由的图标语义，赋予机构级资产管理质感；
   - 激活态注入光晕与主题色联动微动效。

---

## 2. 应用主图标 (App Logo) 详细设计规范

### 2.1 矢量构图与几何结构
- **画布规范**: `viewBox="0 0 512 512"` 标准正方形矢量空间；
- **底板设计**:
  - 外轮廓为科技圆角方盾（`rx="112"`，尺寸 `480x480`，居中留白 `16px`）；
  - 渐变底色：深空渐变底版（`#060B18` 至 `#0C1A32`），边缘搭配 `1.5px` 细微霓虹微光描边（`rgba(0, 212, 255, 0.25)`）；
- **核心多面体“M”与增长折线动量**:
  - **左翼基石柱（Stability）**: 坚固平直的三维立柱，注入沉稳科技紫蓝渐变（`#4F46E5` 至 `#7C3AED`）；
  - **中心 V 槽立体折面（Depth）**: 背光侧切面与高光折面形成三维纵深立体感（`#1E1B4B` 与 `#6366F1`）；
  - **右翼飞升折线（Growth & Momentum）**: 从中心谷底昂扬升起，突破几何边界向上延伸为 45° 冲天趋势线，末端收于锐利的几何趋势箭头（`#00D4FF` 至 `#38BDF8`）；
  - **顶峰星芒辉光**: 箭头右上峰值处点缀微光，强化高光视觉焦点。

### 2.2 组件封装 (`frontend/src/components/AppLogo.vue`)
- **属性定义 (Props)**:
  - `size`: 基础像素尺寸，默认 `32`（数字或字符串，如 `24`, `32`, `40`, `48`）；
  - `withText`: 是否显示品牌名称 `Minefolio`（默认 `false`）；
  - `animated`: 悬停时是否触发轻微上升与呼吸发光动效（默认 `true`）；
- **主题适配**:
  - 暗色主题下：展现深空蓝底板与极光霓虹渐变；
  - 亮色主题下：外框微调为通透浅底与高对比度渐变色，保证白底背景下的清晰轮廓。

### 2.3 静态文件矩阵与 PWA 支持
- `frontend/public/favicon.svg`: 纯 SVG 矢量格式，作为现代浏览器 Tab 栏与书签图标；
- `frontend/public/site.webmanifest`: 保持对应用图标的尺寸配置；
- 利用现有点阵脚本或轻量生成器更新对应分辨率的 PNG 资源（`favicon-16x16.png`, `favicon-32x32.png`, `apple-touch-icon.png`）。

---

## 3. 菜单图标 (Menu Icons) 语义升级与跨端映射规范

### 3.1 桌面端菜单（`Layout.vue`）图标精细化映射

全量采用 `@iconify/vue` + `ph:*-duotone`：

| 模块分组 | 路由地址 | 菜单名称 | 全新 Phosphor Duotone 图标 | 原图标 | 升级语义说明 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **资产全景** | `/dashboard` | 仪表盘 | `ph:squares-four-duotone` | `ph:chart-line` | 四象限全局资产大盘总览，替代单根折线 |
| | `/assets` | 资产账户 | `ph:vault-duotone` | `ph:wallet` | 机构级安全资产金库，涵盖银行/证券/加密多账户 |
| | `/holdings` | 投资持仓 | `ph:trend-up-duotone` | `ph:chart-bar` | 持仓表现与多空收益动量，强化投资属性 |
| | `/reports` | 财务报告 | `ph:presentation-chart-duotone` | `ph:chart-pie` | 交互式数据看板与多维财务研报 |
| **收支管理** | `/transactions` | 交易流水 | `ph:arrows-left-right-duotone` | `ph:list` | 双向资金调拨、对账与跨账户资金流 |
| | `/daily-expenses` | 日常记账 | `ph:receipt-duotone` | `ph:currency-cny` | 收据凭证与日常消费流水，具备国际化通用性 |
| | `/plans` | 财务计划 | `ph:target-duotone` | `ph:calendar-check` | 财富定投目标与周期计划，突显目标感 |
| | `/categories` | 分类管理 | `ph:tree-structure-duotone` | `ph:folder` | 树状收支分类系统与标签网络 |
| **AI 空间** | `/chat` | AI 财务顾问 | `ph:sparkle-duotone` | `ph:chat-circle-text` | 灵动智能星芒，主流现代 AI 核心标识 |
| | `/ai-traces` | AI 调用追踪 | `ph:activity-duotone` | `ph:scan` | 算力脉冲与调用链实时心跳监控 |
| **系统安全** | `/audit-logs` | 审计日志 | `ph:shield-check-duotone` | `ph:scroll` | 安全防护与合规操作留痕 |
| | `/settings` | 系统设置 | `ph:sliders-horizontal-duotone` | `ph:gear` | 参数精密调谐控制台，替代机械齿轮 |

### 3.2 移动端底部导航（`MobileLayout.vue`）统一

淘汰原有 Element Plus 孤立图标，统一集成 `@iconify/vue`：

| 底部 Tab 项 | 路由路径 | 原 Element 图标 | 全新 Phosphor Duotone 图标 | 尺寸与展示规格 |
| :--- | :--- | :--- | :--- | :--- |
| **大盘看板** | `/m/dashboard` | `DataAnalysis` | `ph:squares-four-duotone` | 22px，双色微质感 |
| **日常记账** | `/m/expenses` | `Plus` | `ph:receipt-duotone` | 22px，突出记账凭证属性 |
| **账户资产** | `/m/assets` | `Wallet` | `ph:vault-duotone` | 22px，与桌面端完全统一 |
| **定投计划** | `/m/plans` | `Calendar` | `ph:target-duotone` | 22px，财富目标符号 |
| **财务报告** | `/m/reports` | `PieChart` | `ph:presentation-chart-duotone` | 22px，专业数据看板 |
| **偏好设置** | `/m/settings` | `Setting` | `ph:sliders-horizontal-duotone` | 22px，精密参数控制台 |

### 3.3 交互动效与高亮光晕 (Micro-interactions)
- **非激活态 (Default)**：
  - Duotone 次路径继承低透明度（~0.32），主轮廓与前景色协调一致；
- **悬停态 (Hover)**：
  - 图标微幅缩放 `transform: scale(1.08)`，主路径提亮为 `var(--mf-primary)`；
- **激活选中态 (Active)**：
  - 图标主色全面点亮为 `var(--mf-primary)`；
  - 附加光晕投影：`filter: drop-shadow(0 0 6px var(--mf-primary-light))`；
  - 侧边栏菜单文字字重加粗，左侧辅以发光高亮条。

---

## 4. 实施路径 (Implementation Steps)

1. **创建全新应用 Logo 矢量组件与静态资源**：
   - 编写 `frontend/src/components/AppLogo.vue`；
   - 更新 `frontend/public/favicon.svg`。
2. **重构桌面端主导航与页面品牌标识**：
   - 在 `frontend/src/views/Layout.vue` 中引入 `<AppLogo>`；
   - 替换所有 12 个菜单项为 Phosphor Duotone 图标体系，并配置微光动效样式。
3. **改造移动端底部导航栏**：
   - 在 `frontend/src/views-mobile/MobileLayout.vue` 中引入 `@iconify/vue` 的 `<Icon />`；
   - 替换 6 个 Tab 项图标为 Phosphor Duotone 体系，确保移动端打包与运行无误。
4. **统一登录与首次部署页面品牌标识**：
   - 在 `frontend/src/views/Login.vue` 与 `frontend/src/views/Setup.vue` 标题区植入 `<AppLogo>`，完成全站品牌闭环。
5. **测试与跨端验证**：
   - 运行前端单元测试 `npm test`；
   - 执行桌面端构建 `npm run build` 和移动端构建 `npm run build:mobile`，确保无任何打包与类型错误。
