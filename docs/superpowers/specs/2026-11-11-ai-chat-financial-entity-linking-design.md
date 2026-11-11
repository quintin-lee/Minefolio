# Design: AI Chat Financial Entity Linking & Micro-Cards

**Date**: 2026-11-11  
**Status**: Draft (Awaiting Approval)  
**Topic**: AI 对话金融实体高亮与悬浮微卡穿透交互 (Entity Linking & Micro-cards)  
**Selected Approach**: 方案 A（客户端资产与账户智能解析 + 全局悬浮微卡交互）

---

## 1. Intent & Context

在 Minefolio 的 AI 对话场景中，助手经常针对具体的账户（如“招商银行储蓄卡”、“微信零钱”、“币安钱包”）和资产（如“腾讯控股”、“标普500 ETF”、“贵州茅台”）提供分析和操作建议。
目前，AI 回复中的这些金融实体均以普通文本展示，存在两个主要体验短板：
1. **静态且割裂**：用户无法直观辨识哪些词是系统内已录入的实际资产/账户，阅读分析时缺乏沉淀感。
2. **缺乏穿透力**：当用户看到“建议调整你的【招商银行卡】持仓”或“【标普500】已实现盈利”时，必须退出对话界面、手动导航到资产或账户列表翻找，割裂了“咨询-查看-决策”的操作闭环。

本项目旨在实现**金融实体自动识别、微徽标化展示、悬浮微卡（Micro-card）实时预览与一键直达穿透**，将静态对话升级为深度连接系统数据的互动金融工作台。

---

## 2. Goals, Non-Goals & Anti-Goals

### Goals
- **自动实体化识别**：基于用户当前账本的实际账户与资产字典，自动将 AI 回复 Markdown 中的资产名、账户名标记为交互式金融徽标（`.mf-entity-badge`）。
- **悬浮微卡预览 (Micro-card)**：鼠标悬停（或移动端点击）金融实体徽标时，弹出高质感微型卡片，展示实时估值/余额、所属账户/机构、最新浮盈、持仓占比等关键指标。
- **一键直达跳转**：微卡中提供「查看资产」/「查看账户」快捷入口，点击直接带参数路由跳转到系统对应页面，平滑闭环。
- **财务数值排版强化**：针对金额（`¥...` / `$..`）与百分比涨跌幅（`+X.X%` / `-X.X%`）增加轻量财务数字排版与语义着色（盈利红/绿、亏损绿/红，遵从系统配置）。
- **向后兼容与高性能**：零后端侵入，立刻对所有历史会话生效；对长词优先匹配并防止短词误伤；流式生成期间不阻塞性能。

### Non-Goals
- 不修改大模型 System Prompt 与后端 API 协议。
- 不引入外部庞大 NLP 实体识别模型，完全由本地资产元数据驱动。

### Anti-Goals
- **防止误伤日常通用词**：禁止无边界匹配单字通用词（如“卡”、“房”、“车”、“金”、“币”等），必须设定最小字符阈值（>=2）与停用词黑名单过滤。
- **流式阶段严禁重绘抖动**：流式吐字期间仅进行轻量渲染，待段落闭合或防抖完成后才执行高亮替换，确保打字机流畅性。

---

## 3. Approaches Considered & Trade-offs

### 方案 A：客户端资产字典智能解析 + 悬浮微卡交互（选中方案）
- **原理**：利用前端缓存的 `accounts` 与 `assets` 构建前缀树或正则词典（按词长降序排列），在 Markdown 渲染输出后对文本节点进行实体包裹，由顶层统一事件代理捕获悬停与点击事件呼出 `FinancialEntityPopover`。
- **优势**：
  - 零后端侵入，无需修改 C23 后端或模型提示词。
  - 自动向后兼容所有历史会话记录。
  - 与输入框 `@` 账户资产提及功能形成完美的“输入引用-输出解析”双向闭环。
- **劣势与缓解**：短词可能存在潜在歧义，通过“最长匹配原则”+“停用词过滤”+“账户/资产专属命名权重”解决。

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
[accountsApi / assetsApi]
         │
         ▼ (提取实体字典: 名称、别名、ID、类别、当前估值、所属账户)
[useFinancialEntities.ts Composable]
         │
         ├─ linkFinancialEntities(html: string): string  --> 供 ChatMessageContent.vue 使用
         │
         └─ getEntityDetails(type, id): EntityDetail     --> 供 FinancialEntityPopover.vue 使用
```

### 4.2 实体匹配算法（最长前缀优先 & 边界防误伤）
1. **字典构建**：
   - 账户字典：`{ id, name, institution, icon, asset_count, total_value }`
   - 资产字典：`{ id, name, code, asset_type, current_value, cost_basis, account_name, pnl }`
2. **过滤规则**：
   - 过滤掉纯数字（如年份 `2024`）、长度小于 2 的通用字（如“房”、“车”、“卡”）。
   - 按词长从长到短排序（例如优先匹配“招商银行经典白金卡”，再匹配“招商银行”）。
3. **安全替换**：
   - 仅对 HTML 文本节点进行匹配替换，避免破坏已有的 `<a href>`、`<code>` 或 `<pre>` 标签。
   - 包装为结构化标签：
     `<span class="mf-entity-badge" data-entity-type="asset" data-entity-id="12">腾讯控股</span>`

### 4.3 交互体验：`FinancialEntityPopover.vue`
- **悬浮延迟**：`mouseenter` 延迟 180ms 触发，防止用户快速划过鼠标时光标抖动闪烁；`mouseleave` 延迟 200ms 关闭，留足平移到卡片上的时间。
- **卡片展示内容**：
  - 实体类型小图标（如股票、基金、银行卡、加密货币）。
  - 实体全称与分类标签（如“港股 · 腾讯控股” / “银行卡 · 招商银行”）。
  - 当前财务快照：实时最新估值、持仓数量、所属父账户。
  - 盈亏信息（资产专属）：最新累计浮盈金额及盈亏百分比（带红绿配色）。
  - 操作按钮：「前往资产详情 →」点击后通过 Vue Router 跳转至 `/assets?highlight=12` 或 `/accounts`。
- **移动端适配**：
  - 在移动端（Touch 设备）上，点击实体徽标触发微型底部浮层或轻量 Popover，再次点击外部区域收起。

### 4.4 财务数值排版优化
- 金额正则识别：匹配 `¥\s*\d+(?:,\d{3})*(?:\.\d+)?` 或 `\$\s*\d+(?:,\d{3})*(?:\.\d+)?`，应用等宽财务字体与微调字距样式。
- 涨跌幅识别：匹配 `[+-]\d+(?:\.\d+)?%`，识别为 `.mf-trend-up`（涨/红）或 `.mf-trend-down`（跌/绿）。

---

## 5. File Changes & Modularity

| 文件 | 改动性质 | 职责说明 |
| :--- | :--- | :--- |
| `frontend/src/composables/useFinancialEntities.ts` | **新建** | 账户与资产实体字典提取、最长匹配分词算法、停用词过滤、HTML 注入渲染管道 |
| `frontend/src/components/FinancialEntityPopover.vue` | **新建** | 浮动微型卡片组件：展示资产/账户实时数据快照、浮盈收益、以及路由直达跳转按钮 |
| `frontend/src/components/ChatMessageContent.vue` | **修改** | 集成实体与财务数值后处理，使用事件委托监听实体徽标的 hover 与 click 事件 |
| `frontend/src/views/Chat.vue` | **修改** | 提供挂载容器或配合路由跳转状态保持 |

---

## 6. Verification & Quality Gates
- **类型安全**：`npm run build` 确保 `vue-tsc -b && vite build` 0 errors。
- **移动端打包**：`npm run build:mobile` 0 errors。
- **性能与安全**：
  - 实体高亮后处理经过 `DOMPurify` 或在文本节点替换，杜绝 XSS。
  - 代码块（`<code>`、`<pre>`）与 Mermaid 图表内部的内容完全排除在实体匹配之外。
  - 字典加载单例缓存，多轮对话不重复请求 API。
