# Design: AI Chat Financial Entity Linking & Micro-Cards

**Date**: 2026-11-11  
**Status**: Ready for Approval (Builder Review Completed)  
**Topic**: AI 对话金融实体高亮与悬浮微卡穿透交互 (Entity Linking & Micro-cards)  
**Selected Approach**: 方案 A（客户端资产与账户智能解析 + 全局悬浮微卡交互）

---

## 1. Intent & Context

在 Minefolio 的 AI 对话场景中，助手经常针对具体的账户（如“招商银行储蓄卡”、“微信零钱”、“币安钱包”）和资产（如“腾讯控股”、“标普500 ETF”、“贵州茅台”）提供分析和操作建议。
目前，AI 回复中的这些金融实体均以普通文本展示，存在两个主要体验短板：
1. **静态且割裂**：用户无法直观辨识哪些词是系统内已录入的实际资产/账户，阅读分析时缺乏沉淀感。
2. **缺乏穿透力**：当用户看到“建议调整你的【招商银行卡】持仓”或“【标普500】已实现盈利”时，必须退出对话界面、手动导航到资产列表翻找，割裂了“咨询-查看-决策”的操作闭环。

本项目旨在实现**金融实体自动识别、微徽标化展示、悬浮微卡（Micro-card）实时预览与一键直达穿透**，将静态对话升级为深度连接系统数据的互动金融工作台。

---

## 2. Goals, Non-Goals & Anti-Goals

### Goals
- **自动实体化识别**：基于用户当前账本的实际账户与资产字典，自动将 AI 回复 Markdown 中的资产名、账户名标记为交互式金融徽标（`.mf-entity-badge`）。
- **悬浮微卡预览 (Micro-card)**：鼠标悬停（或移动端点击）金融实体徽标时，弹出高质感微型卡片，展示实时估值/余额、所属账户/机构、最新浮盈、持仓数量等关键指标。
- **一键直达跳转**：微卡中提供「查看资产」/「查看账户」快捷入口，点击直接带参数路由跳转到系统对应页面（`/assets?asset_id=...` 或 `/assets?account_id=...`），平滑闭环。
- **财务数值排版强化**：针对金额（`¥...` / `$..`）与百分比应用等宽金融字体排版，保持视觉专业度。
- **向后兼容与高性能**：零后端侵入，立刻对所有历史会话生效；对长词优先匹配并防止短词误伤；流式生成期间不阻塞性能。

### Non-Goals
- 不修改大模型 System Prompt 与后端 API 协议。
- 不引入外部庞大 NLP 实体识别模型，完全由本地资产元数据驱动。

### Anti-Goals
- **防止误伤日常通用词**：禁止无边界匹配单字通用词（如“卡”、“房”、“车”、“金”、“币”等），必须设定最小字符阈值（>=2）与停用词黑名单过滤（例如系统级默认账户名“默认账户”）。
- **防止正负色彩误导**：正文中自然出现的百分比（例如“支出增加 +25%”、“配置 20% 现金”）不滥用红绿涨跌色，避免语义混淆；明确的盈亏红绿着色仅限定在悬浮微卡中经过验证的持仓盈亏字段。
- **流式阶段严禁重绘抖动**：流式吐字期间仅进行轻量渲染，待段落闭合或防抖完成后才执行高亮替换，确保打字机流畅性。

---

## 3. Approaches Considered & Trade-offs

### 方案 A：客户端资产字典智能解析 + 悬浮微卡交互（选中方案）
- **原理**：利用前端缓存的 `accounts` 与 `assets` 构建词典（按词长降序排列），在 Markdown 渲染输出后对文本节点进行实体包裹，由顶层统一事件代理捕获悬停与点击事件呼出 `FinancialEntityPopover`。
- **优势**：
  - 零后端侵入，无需修改 C23 后端或模型提示词。
  - 自动向后兼容所有历史会话记录。
  - 与输入框 `@` 账户资产提及功能形成完美的“输入引用-输出解析”双向闭环。
- **劣势与缓解**：短词可能存在潜在歧义，通过“最长匹配原则”+“停用词黑名单”+“同名实体标签切换”解决。

### 方案 B：Prompt 显式协议语法 (`[名称](entity:type:id)`)（未采纳）
- **原理**：修改大模型提示词，要求模型输出特定 Markdown 链接语法。
- **未采纳原因**：历史已有对话完全无法受惠；依赖外部 LLM 的指令遵循能力，遇到小模型或推理不稳定时容易漏标或格式损坏。

### 方案 C：后端 AI Runtime NER 识别流（未采纳）
- **原理**：后端在生成回复后调用 NER 抽取实体并以 metadata 下发。
- **未采纳原因**：架构过重，增加额外延迟与复杂度，且无法动态反映资产数据的即时变化。

---

## 4. Detailed Design & Architecture

### 4.1 架构分层与数据流

```
[accountsApi / assetsApi / reportsApi.holdings]
         │
         ▼ (提取实体字典: 名称、别名、ID、类别、当前估值、所属账户、浮盈)
[useFinancialEntities.ts Composable]
         │
         ├─ linkFinancialEntities(html: string): string  --> 供 ChatMessageContent.vue 使用
         │
         └─ getEntityDetails(type, id): EntityDetail     --> 供 FinancialEntityPopover.vue 使用
```

### 4.2 实体匹配算法与歧义处理策略

1. **词典构建与清洗**：
   - 账户字典：`{ id, name, institution, icon, asset_count, total_value }`
   - 资产字典：`{ id, name, code, asset_type, current_value, cost_basis, account_name }`
   - 持仓快照合并：从 `reportsApi.holdings()` 提取各投资标的的 `floating_pnl` 与 `floating_pct`，作为投资资产的实时盈亏数据源。
2. **停用词黑名单 (Stopwords)**：
   - 系统级自动生成名称：“默认账户”、“现金账户”、“总资产”、“总负债”、“净资产”。
   - 过滤长度 < 2 的通用字（如“房”、“车”、“卡”、“币”）。
3. **同名歧义解决 (Collision Policy)**：
   - 当账户与资产同名（例如都叫“招商银行”）：`EntityDetail` 聚合包含两类元数据，在 `FinancialEntityPopover` 顶端展示微型切换器（`[资产] · [账户]`），默认优先展示资产维度，并允许一键切换。
4. **最长前缀优先匹配**：
   - 按名称长度降序构建正则表达式，确保优先匹配完整复合词（如“招商银行经典白金卡”优先于“招商银行”）。
5. **安全文本节点替换**：
   - 仅对 HTML 文本节点执行匹配替换，跳过 `<code>`、`<pre>`、`<a>` 标签及属性字符串，杜绝 HTML 结构破坏与 XSS。
   - 包装后的 DOM 标签结构：
     `<span class="mf-entity-badge" data-entity-type="asset" data-entity-id="12">腾讯控股</span>`

### 4.3 悬浮微卡与穿透跳转规范

- **交互时序**：
  - `mouseenter` 延迟 180ms 触发弹出，避免快速扫过光标时抖动。
  - `mouseleave` 延迟 220ms 关闭，预留充足平移时间。
  - 移动端点击直接呼出/切换，点击背景关闭。
- **展示内容**：
  - **资产类型微卡**：
    - 图标 + 资产全称 + 资产类型（股票/基金/虚拟币等）
    - 所属账户名称（如 `招商银行储蓄卡`）
    - 当前总市值 / 估值
    - 投资品独有：持仓成本、浮动盈亏（金额 + 百分比，遵从系统红涨绿跌/绿涨红跌配置）
    - 底部动作：`查看资产详情 →`
  - **账户容器微卡**：
    - 机构图标 + 账户名 + 机构类别（银行卡/证券券商/数字钱包等）
    - 下辖资产项数（如 `3 项资产`）与子汇总估值
    - 底部动作：`查看账户下资产 →`
- **穿透路由导航契约**：
  - 资产跳转：路由至 `/assets?asset_id=<id>`。
  - 账户跳转：路由至 `/assets?account_id=<id>`。
  - 在 `Assets.vue` 中支持监听 `route.query`：自动展开目标账户折叠组（`collapsedGroups.delete(key)`），并执行 `scrollIntoView({ behavior: 'smooth' })` 与 2 秒的高亮淡出动画 (`highlight-pulse`)。

### 4.4 财务数值排版

- 金额正则识别：匹配 `¥\s*\d+(?:,\d{3})*(?:\.\d+)?` 或 `\$\s*\d+(?:,\d{3})*(?:\.\d+)?`，应用专用的财务等宽字体（`font-family: var(--mf-font-mono)`）与微调字距。
- 文本百分比：仅应用等宽字体样式，不擅自添加红绿盈亏色彩，避免将“支出超支 +25%”误染为盈利色彩。

---

## 5. File Changes & Modularity

| 文件 | 改动性质 | 职责说明 |
| :--- | :--- | :--- |
| `frontend/src/composables/useFinancialEntities.ts` | **新建** | 账户与资产实体字典提取、最长匹配分词算法、停用词过滤、HTML 注入管道 |
| `frontend/src/components/FinancialEntityPopover.vue` | **新建** | 浮动微型卡片组件：展示资产/账户实时数据快照、浮盈收益、以及路由直达跳转按钮 |
| `frontend/src/components/ChatMessageContent.vue` | **修改** | 集成实体与财务数值后处理，使用事件委托监听实体徽标的 hover 与 click 事件 |
| `frontend/src/views/Assets.vue` | **修改** | 支持根据路由参数 `?asset_id=` 或 `?account_id=` 自动展开账户组、定位滚动并高亮 |
| `frontend/src/views/Chat.vue` | **修改** | 挂载全局 Popover 或传递实体联动上下文 |

---

## 6. Verification & Quality Gates
- **类型安全**：`npm run build` 确保 `vue-tsc -b && vite build` 0 errors。
- **移动端打包**：`npm run build:mobile` 0 errors。
- **行为验证**：
  1. 在对话消息中出现真实存在的资产（如“腾讯控股”）或账户（如“招商银行”）时，自动渲染为 `.mf-entity-badge`。
  2. 鼠标悬停显示带有实时估值、持仓数量与盈亏的微型卡片。
  3. 点击微卡按钮平滑跳转至 `/assets`，对应账户组自动展开并精准定位高亮。
  4. 系统级“默认账户”等通用词不触发实体徽标化。
  5. 代码块（`<code>`、`<pre>`）与 Mermaid 图表内部内容严格排除替换。
