# 流式生成与思考/工具链阅读体验重构实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 重构 `ChatMessageContent.vue` 中的深度思考卡片与工具调用卡片，消除长思考过程遮挡正文与多工具堆叠引起的页面剧烈跳动，实现思考过程微滚动跟随、正文吐字后自动平滑收敛，以及工具调用链升级为紧凑的时间轴步骤指示器。

**Architecture:** 
1. 深度思考卡片（Thinking Card）高度限制为舒适的 220px，在思考流式输出阶段容器内部平滑跟随滚动；在正文开始吐字输出时（且用户未手动干预固定），自动平滑收起为紧凑状态栏，将可视空间留给正文。
2. 工具调用卡片（Tool Card）升级为步骤时间轴指示器（Steps Timeline），建立工具名中文语义化与专属图标映射，头部展示紧凑步骤胶囊预览，单项卡片支持独立折叠/展开，不再一次性平铺数百行原始 JSON。

**Tech Stack:** Vue 3, TypeScript, Pinia, Element Plus, Iconify Icons, Vitest.

**Spec:** [`docs/superpowers/specs/2026-11-10-chat-reading-flow-design.md`](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-11-10-chat-reading-flow-design.md)

## Global Constraints

- **保持现有功能完全兼容**：复制思考过程、复制工具参数/结果、工作流与图表卡片抽离等原有交互必须 100% 保持兼容。
- **构建零错误**：`npm --prefix frontend run build`（vue-tsc -b && vite build）必须 0 错误。
- **测试通过率 100%**：全量测试套件全部通过。

## Review Focus

1. **思考过程流式跟随滚动不打扰用户**：当用户在思考框内手动向上滚动查阅历史思考时，不要强制重置回最底部。
2. **正文吐字时的优雅收敛**：只有当用户从未手动点击过展开/收起时，正文开始生成才触发自动收拢；若用户手动点击了展开，则尊重用户的固定选择。
3. **未定义工具名的优雅降级**：出现外部或未知 MCP 工具时，自动转为首字母大写可读格式，不报错不缺失。
4. **单步详情折叠的独立性**：一个工具卡片的展开与折叠不影响相邻工具的展示状态。
5. **暗色与亮色主题兼容**：时间轴节点、状态标签和滚动条在暗色与亮色模式下均对比度清晰。

---

### Task 1: 更新深度思考卡片与工具步骤测试契约 (Test Contract)

**Files:**
- Modify: `frontend/tests/chat-message-thinking.spec.ts`
- Modify: `frontend/tests/chat-message-tool-card.spec.ts`

**Interfaces:**
- Consumes: `ChatMessageContent.vue` 模板与组件行为
- Produces: 
  - 针对思考卡片正文输出后自动收敛、220px 高度上限约束的测试用例
  - 针对工具语义化名称映射（如 `web_search` -> `全网实时检索`）、单步时间轴与步骤折叠的测试用例

- [ ] **Step 1: 编写思考卡片新特性的测试用例**

在 `frontend/tests/chat-message-thinking.spec.ts` 中增加测试：
1. 验证在流式思考阶段思考卡片展开，但当 `content` 正文开始输出时（未手动干预）自动收敛；
2. 验证主体渲染带有微滚动结构与样式类。

- [ ] **Step 2: 编写工具链时间轴步骤特性的测试用例**

在 `frontend/tests/chat-message-tool-card.spec.ts` 中增加测试：
1. 验证工具名称被友好语义化格式化（例如 `web_search` 呈现为“全网实时检索”）；
2. 验证渲染步骤时间轴结构（`.tool-timeline-step`），并在单个步骤内支持点击切换展开入参与出参。

- [ ] **Step 3: 运行测试以确认其正确失败**

运行: `npm --prefix frontend test -- tests/chat-message-thinking.spec.ts tests/chat-message-tool-card.spec.ts --run`  
预期: FAIL（新断言失败）

- [ ] **Step 4: 提交测试改动**

```bash
git add frontend/tests/chat-message-thinking.spec.ts frontend/tests/chat-message-tool-card.spec.ts
git commit -m "test(chat): 🧪 update test contract for thinking auto-collapse and tool timeline steps"
```

---

### Task 2: 实现思考过程微滚动跟随与正文就绪自动收敛

**Files:**
- Modify: `frontend/src/components/ChatMessageContent.vue`

**Interfaces:**
- Consumes: `props.thinkingContent`, `props.content`, `props.isStreaming`
- Produces:
  - 动态计算属性 `isThinkingExpanded`（支持正文到来时自动收起）
  - 思考框容器 ref `thinkingBodyRef` 及流式自动向下滚动逻辑
  - CSS 样式类限制 `max-height: 220px; overflow-y: auto;` 及细滚动条

- [ ] **Step 1: 在 `ChatMessageContent.vue` 中增强思考展开状态机**

1. 改造 `isThinkingExpanded` 计算属性与 `manualExpanded` 状态：
   - 当 `manualExpanded.value !== null` 时，遵循用户手动设置；
   - 否则，若正文 `props.content` 已有内容，自动收拢为 `false`；
   - 若正文尚未输出且处于思考阶段，保持展开 `true`。
2. 增加 `thinkingBodyRef` 并在 `watch(thinkingContent)` 时检测：若处于 `isThinkingStreaming` 且距离底部较近，平滑滚动至容器底端。

- [ ] **Step 2: 优化思考卡片 CSS 样式**

1. 将 `.thinking-content-body` 的最大高度优化为 `220px`；
2. 配置 `scrollbar-width: thin;` 与平滑滚动效果。

- [ ] **Step 3: 运行单测验证 Task 2**

运行: `npm --prefix frontend test -- tests/chat-message-thinking.spec.ts --run`  
预期: PASS。

- [ ] **Step 4: 提交 Task 2 改动**

```bash
git add frontend/src/components/ChatMessageContent.vue
git commit -m "feat(chat): ⚡ add thinking auto-collapse on answer stream and smooth micro-scroll"
```

---

### Task 3: 实现工具链时间轴步骤指示器与语义化映射

**Files:**
- Modify: `frontend/src/components/ChatMessageContent.vue`

**Interfaces:**
- Consumes: `props.toolCalls`
- Produces:
  - 语义化映射表 `TOOL_META_MAP` 与辅助方法 `getToolLabel(name)`, `getToolIcon(name)`
  - 展开单步状态 `expandedStepIndices: Ref<Set<number>>` 与方法 `toggleStep(idx)`
  - 模板中的步骤预览胶囊 `.tool-steps-preview` 与时间轴列表 `.tool-timeline-step`
  - 步骤折叠与详情展示样式

- [ ] **Step 1: 在 `ChatMessageContent.vue` 脚本中定义工具元数据字典与单步展开状态**

1. 定义 `TOOL_META_MAP`，包含 `query_assets`, `list_assets`, `sync_quotes`, `fetch_market_index`, `propose_daily_expense`, `propose_transfer`, `analyze_financial_health`, `web_search`, `execute_workflow_step` 等核心工具名称映射；
2. 实现 `getToolLabel(name: string)` 与 `getToolIcon(name: string)`；
3. 定义 `expandedStepIndices = ref(new Set<number>())`，实现 `toggleStep(idx: number)` 与 `isStepExpanded(idx: number)`。

- [ ] **Step 2: 重构 `.tool-card` 模板结构**

1. 在 `.tool-card-header` 中增加步骤胶囊概览 `.tool-steps-preview`；
2. 在 `.tool-card-body` 中以时间轴步骤卡片 `.tool-timeline-step` 渲染每个工具：
   - 包含步骤序号、专属图标、语义化名称、状态指示器及“查看参数/结果”折叠开关；
   - 点击具体步骤才展开其对应的 `pre.tool-call-args` 与 `pre.tool-call-result`。

- [ ] **Step 3: 补充时间轴样式与过渡动效**

编写 `.tool-steps-preview`, `.tool-timeline-step`, `.tool-step-header`, `.tool-step-toggle` 等 CSS 规则。

- [ ] **Step 4: 运行单测验证 Task 3**

运行: `npm --prefix frontend test -- tests/chat-message-tool-card.spec.ts --run`  
预期: PASS。

- [ ] **Step 5: 提交 Task 3 改动**

```bash
git add frontend/src/components/ChatMessageContent.vue
git commit -m "feat(chat): ⚡ upgrade tool calls to timeline steps with friendly semantic labels"
```

---

### Task 4: 全量回归测试与生产环境打包验证

**Files:**
- Test: `frontend/tests/` 全部测试集

- [ ] **Step 1: 执行所有前端测试套件**

运行: `npm --prefix frontend test -- --run`  
预期: 全部测试用例 PASS。

- [ ] **Step 2: 执行前端生产环境打包**

运行: `npm --prefix frontend run build`  
预期: 成功生成 `dist/`，0 TS 报错。

- [ ] **Step 3: 检查 Git 工作区状态**

运行: `git status`  
预期: 工作区干净无残留。
