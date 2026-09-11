# Chat UI Modern Studio 交互页面美化设计规范

- **日期**: 2026-09-11
- **状态**: Approved
- **设计主题**: Modern Studio 浮岛式一体化升级 (Modern AI Studio Floating Island Architecture)
- **影响范围**: `frontend/src/views/Chat.vue`, `frontend/src/components/ChatMessageContent.vue`, `frontend/src/components/PromptStarters.vue`, `frontend/src/components/ActionCard.vue`, `frontend/src/components/WorkflowBar.vue`

---

## 1. 背景与目标 (Background & Goals)

### 1.1 现状分析
Minefolio 是一款自托管个人金融与投资管理平台。现有 AI 对话模块具备强大的大模型与财务数据联动能力（支持流式响应、自然语言快捷记账、收支结构分析、Mermaid 流程图、工作流管线等）。然而，当前界面在视觉质感与现代交互体验上存在提升空间：
1. **输入区域沉闷**：底部采用传统的全贯通边框表单条，缺少呼吸感与沉浸感；
2. **消息流视线发散**：大屏下未充分限宽居中，长文阅读和图表查看容易产生横向拉伸感；
3. **视觉细节粗放**：消息气泡、Thinking 思考中动效、操作工具栏、Markdown 财务表格与引用块缺少现代轻奢与微交互细节；
4. **侧边栏与顶栏层次平淡**：选中态提示与折叠过渡可进一步打磨。

### 1.2 改造目标
结合主流现代 AI Studio（类似 Claude 3.5 / Perplexity / Linear）的设计语言，打造具有**金融专业感**、**舒适阅读视距**与**沉浸输入交互**的现代化 AI 交互界面，与 Minefolio 全局 Bento 设计语言保持统一。

---

## 2. 核心设计规范 (Core Design Specifications)

### 2.1 页面宏观布局与视窗结构 (Global Layout & Viewport)
1. **全局限宽居中视窗**：
   - 消息流容器 `messages` 内部限制最大宽度为 **`860px`**，居中对齐（`max-width: 860px; margin: 0 auto; width: 100%;`）；
   - 大屏下视线自然聚焦，给用户提供类似阅读精美研报的沉浸视距；
   - 底部保留足够的安全滚动间距（`padding-bottom: 140px`），确保内容在滚动到底部时绝不被悬浮输入岛遮挡。
2. **轻量化磨砂顶栏 (Header)**：
   - 高度收紧为 `52px`，采用 `backdrop-filter: blur(16px)` 与细边框；
   - 左侧保留渐变品牌色指示条与“AI 财务助手”标题，右侧模型徽章支持在线绿点指示；
   - 动作按钮（新对话、导出 Markdown、复制记录、清空）采用轻量幽灵按钮（Ghost button），附带优雅悬停微交互。
3. **悬浮“回到底部”药丸指示器**：
   - 用户上滑离底超过 `300px` 时，平滑淡入悬浮于输入岛正上方中央；
   - 胶囊造型（箭头 + 文字），点击带有弹性平滑滚动。

### 2.2 悬浮胶囊输入舱 (Floating Input Island)
1. **悬浮卡片定位**：
   - 摆脱全宽死板底栏，采用居中悬浮岛结构：
     ```css
     position: absolute;
     bottom: 20px;
     left: 50%;
     transform: translateX(-50%);
     width: 100%;
     max-width: 860px;
     padding: 0 20px;
     pointer-events: none;
     z-index: 10;
     ```
   - 内部卡片开启 `pointer-events: auto;`，具有圆角 `20px`、`backdrop-filter: blur(20px)`、多层立体柔和阴影以及细边框；
   - 输入框获取焦点（`focus-within`）时，边框过渡为品牌主色并叠加柔和外发光（`box-shadow: 0 8px 32px rgba(0,0,0,0.16), 0 0 0 2px var(--mf-primary-light)`）。
2. **工作流快捷胶囊条 (WorkflowBar)**：
   - 轻盈附着于输入框上方，支持横向平滑滚动；
   - 快捷芯片（财务体检、快捷记账、收支洞察、定投测算等）采用微渐变半透明设计，Hover 具有微上浮与边框提亮。
3. **输入自适应与底栏按钮**：
   - `textarea` 支持自动计算高度，单行起始，最高扩展至 `200px`，滚动条精致化；
   - 占位符提示更清晰友善；
   - 底栏左侧为字数统计与快捷键提示（`↵ 发送 · ⇧↵ 换行`）；
   - 底栏右侧为发送/停止按钮：
     - 普通态：品牌色渐变发送圆角按钮，无内容时半透明禁用，输入后点亮；
     - 流式生成态：一键转为带红晕呼吸脉冲的“停止生成 (Esc)”按钮。

### 2.3 消息气泡与微交互 (Message Stream & Micro-interactions)
1. **用户提问气泡 (User Bubble)**：
   - 右侧对齐，最大宽度 `82%`；
   - 背景使用品牌主色渐变（深色浅色双模式对比度保障），圆角 `18px`（右下角微收 `4px`）；
   - 鼠标移入时底部淡出微操作条（“复制”、“编辑并重发”）；
   - 原地快速编辑支持单行/多行编辑框，回车即发，Esc 取消。
2. **AI 助手回复卡片 (Assistant Card)**：
   - 左侧对齐，居中阅读列内自适应展开；
   - 背景为通透卡片（`var(--mf-surface)` + 细边框 + 轻投影），圆角 `18px`（左下角微收 `4px`）；
   - **机器人头像动效**：生成中（`isStreaming`）时外圈激活环形呼吸光晕（Pulse Ring）；
   - **Thinking 深度思考态**：首字到达前展示微发光 Shimmer 呼吸卡片：“正在调取财务账本并进行深度分析...”，带有波浪弹跳微点；
   - **流式光标**：胶囊式圆角竖条，平滑呼吸闪烁；
   - **回复操作工具条**：回复完成后底部淡入“复制回答”、“重新生成”、“重试”小药丸，点击复制有打勾即时反馈。

### 2.4 金融富文本与数据卡片 (Financial Markdown & Widgets)
1. **财务数据表格 (Data Tables)**：
   - 统一圆角边框、柔和表头背景、斑马纹交替底色；
   - 包含货币符号（`¥`, `$`, `€`, `%`）及数字的列自动居右对齐，使用等宽字体 `var(--mf-font-mono)`；
   - 超宽表格支持水平自适应滑动，两端带微妙阴影指示。
2. **财务诊断结论引用块 (Callout)**：
   - 左侧 `3px` 品牌渐变竖条，浅色柔和底色，适合呈现 AI 财务核心结论与警示。
3. **自然语言记账确认卡 (`ActionCard.vue`)**：
   - 升级卡片阴影与边框，突出大号金额数字（支出红/收入绿），胶囊标签化展示账户与分类；
   - 确认入账按钮增加微渐变交互。
4. **空状态启程卡片 (`PromptStarters.vue`)**：
   - Bento 网格卡片阵列，半透明鲜明图标背景；
   - 鼠标悬停微上浮 `translateY(-3px)` 与边界高光，支持一键发送。

### 2.5 侧边栏、主题与移动端响应式
1. **会话侧边栏**：
   - 折叠/展开采用优雅缓动曲线（`cubic-bezier(0.4, 0, 0.2, 1)`）；
   - 时间分组小胶囊标签；
   - 当前选中项（Active）增加左侧 `3px` 品牌色高光条，标题加粗；
   - 悬停操作按钮（重命名、删除）平滑淡入。
2. **主题适配**：
   - 严格兼容 `data-theme="light"` 和 `data-theme="dark"`；
   - 保证浅色模式对比度充足，深色模式发光通透。
3. **移动端适配 (< 768px)**：
   - 侧边栏自动变为带毛玻璃背景的抽屉（Drawer）；
   - 输入岛自适应满宽，留出左右安全间距，底部支持 `env(safe-area-inset-bottom)`；
   - 消息卡片最大宽度扩展至 `92%`。

---

## 3. 业务逻辑与兼容性保障 (Guarantees & Constraints)

1. **会话切换竞态保护 (Session Switch Race Guard)**：
   - 严格保留 `useChatStore` 中的 `sessionSwitchSeq` 逻辑与取消机制，UI 重塑绝不破坏会话异步隔离。
2. **打字机平滑流式控制**：
   - 保持 `SmoothStreamWriter` 与 `ChatMessageContent` 的平滑吐字与防抖机制，不因样式更新导致流式卡顿或重复重排。
3. **安全与依赖约束**：
   - 遵循规范：前端纯依赖 Vue 3 + TypeScript + Pinia + Element Plus + Iconify，不引入不可控的第三方厚重 UI 库。

---

## 4. 验证计划 (Verification Plan)

1. **类型检查与构建验证**：
   - 运行 `npm --prefix frontend run build`，确保 `vue-tsc -b` 严格类型校验与 Vite 构建 0 错误。
2. **单元测试与测试套件**：
   - 运行 `npm --prefix frontend test`，确保现有前端 Vitest 测试全部通过。
3. **交互与视觉功能验证**：
   - 验证会话新建、重命名、删除与时间分组；
   - 验证空状态 `PromptStarters` 点击发送；
   - 验证输入岛悬浮定位、自动增高、快捷键发送/换行、字数统计；
   - 验证流式生成中状态（Thinking 动画、Pulse 头像光晕、停止生成按钮响应）；
   - 验证 Markdown 渲染（表格斑马纹、数字等宽对齐、引用块、代码块、Mermaid 图表、ActionCard）；
   - 验证深色与浅色主题切换下的对比度与视觉一致性；
   - 验证移动端视口宽度下的抽屉与输入卡片响应。
