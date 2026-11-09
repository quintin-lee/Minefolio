# AI 对话页面顶部工具栏瘦身与信息聚合实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 重构 `Chat.vue` 顶部工具栏，移除重复的思考模式开关与模型徽标，将次级操作收纳至 `···` 更多菜单，并聚合当前会话标题（支持原地重命名）与整场会话 Token/开销度量指标，打造专业清爽的信息中枢工作台。

**Architecture:** 
1. 将 `.chat-toolbar` 重构为三段式弹性结构：左侧保留侧栏切换、模型选择胶囊与流式生成状态；中间新增 `.toolbar-center` 会话标题与内联重命名；右侧新增 `.session-stat-pill` 度量指标胶囊、分析面板快捷触发器与 `el-dropdown` 更多操作下拉菜单。
2. 彻底移除顶部冗余的 `.toolbar-thinking-btn`（输入框底部已有唯一定义源）和 `.current-model-badge`。
3. 遵循 Vue 3 Composition API 与 Element Plus 组件规范，在响应式断点（>1024px, 768~1024px, <768px）下自适应平滑降级。

**Tech Stack:** Vue 3, TypeScript, Pinia (`useChatStore`), Element Plus (`el-dropdown`, `el-select`, `el-tooltip`), Iconify Icons (`@iconify/vue`), Vitest.

**Spec:** [`docs/superpowers/specs/2026-11-09-chat-toolbar-redesign-design.md`](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-11-09-chat-toolbar-redesign-design.md)

## Global Constraints

- **保留全部既有功能**：导出 Markdown、复制全文、清空会话、流式停止、侧栏切换、新建对话等功能必须完整保留且逻辑完全兼容。
- **不得引入内存泄漏**：计算属性与事件监听器严格按生命周期绑定与释放。
- **构建零错误**：`npm --prefix frontend run build`（vue-tsc -b && vite build）必须通过且 0 错误。
- **测试通过率 100%**：`npm --prefix frontend test -- --run` 所有测试必须全部通过。

## Review Focus

1. **会话标题为空或处于新建状态时的展示与编辑**：无标题或空会话时应优雅显示“新对话”，重命名空字符串时应自动取消而非提交空标题。
2. **多轮流式生成中的 Token 与费用统计实时累加**：消息列表追加或收到使用量后，`.session-stat-pill` 能够响应式更新，无 token 的空会话自动隐藏度量胶囊。
3. **窄屏下标题与操作区的弹性避让**：在 768px ~ 1024px 平板或分屏宽度下，度量胶囊应自动隐藏，标题应截断（text-overflow: ellipsis），不产生横向滚动条。
4. **移动端适配 (<768px)**：隐藏中间标题与度量胶囊，仅保留模型选择、新建按钮与 `···` 菜单，保持移动端触摸体验。
5. **更多操作菜单中的路由与弹窗联动**：点击“AI 审计追踪”能正常跳转至 `/ai-traces`；点击“清空本场记录”触发 Element Plus 确认弹窗。

---

### Task 1: 更新工具栏契约测试 (Test Contract)

**Files:**
- Modify: `frontend/tests/chat-toolbar.spec.ts`

**Interfaces:**
- Consumes: `frontend/src/views/Chat.vue` 模板源码
- Produces: 针对无冗余思考按钮、无重复模型徽标、具备 `.toolbar-center` 标题与原地编辑、具备 `.session-stat-pill`、具备 `el-dropdown` 更多操作的测试用例

- [ ] **Step 1: 编写失败的测试用例**

在 `frontend/tests/chat-toolbar.spec.ts` 中更新/增加测试用例：
1. 验证 `.chat-toolbar` 不再包含 `.toolbar-thinking-btn`（已瘦身，由输入框控制）；
2. 验证 `.chat-toolbar` 不再包含 `.current-model-badge`；
3. 验证存在 `.toolbar-center` 且支持会话标题展示与重命名交互（`editingTopTitle` / `startTopRename`）；
4. 验证存在 `.session-stat-pill` 统计 `total_tokens` 与 `cost_usd`；
5. 验证 `.toolbar-right` 中包含包含 `el-dropdown` 且包含导出、复制、清空与 trace 审计选项。

```ts
it('removes duplicate toolbar-thinking-btn from .chat-toolbar', () => {
  expect(chatSource).not.toMatch(/<div class="chat-toolbar"[\s\S]*?class="toolbar-thinking-btn"/)
})

it('renders .toolbar-center with session title and rename capabilities', () => {
  expect(chatSource).toMatch(/class="toolbar-center"/)
  expect(chatSource).toMatch(/currentSessionTitle/)
  expect(chatSource).toMatch(/saveTopRename/)
})

it('renders .session-stat-pill for aggregated session metrics', () => {
  expect(chatSource).toMatch(/class="session-stat-pill"/)
  expect(chatSource).toMatch(/sessionMetrics/)
})

it('renders el-dropdown in .toolbar-right with export, copy, clear and trace actions', () => {
  expect(chatSource).toMatch(/class="toolbar-right"[\s\S]*?<el-dropdown/)
  expect(chatSource).toMatch(/exportToMarkdown/)
  expect(chatSource).toMatch(/copyAllChat/)
  expect(chatSource).toMatch(/handleClearChat/)
  expect(chatSource).toMatch(/\/ai-traces/)
})
```

- [ ] **Step 2: 运行测试以确认其正确失败**

运行: `npm --prefix frontend test -- tests/chat-toolbar.spec.ts --run`  
预期: FAIL（当前 `Chat.vue` 仍包含 `toolbar-thinking-btn`，且缺少 `.toolbar-center` 和 `.session-stat-pill`）

- [ ] **Step 3: 提交测试改动**

```bash
git add frontend/tests/chat-toolbar.spec.ts
git commit -m "test(chat): 🧪 update chat toolbar test contract for context hub redesign"
```

---

### Task 2: 重构 `Chat.vue` 顶部工具栏模板与逻辑

**Files:**
- Modify: `frontend/src/views/Chat.vue:85-195` (模板) 与 `script` 逻辑区

**Interfaces:**
- Consumes: `chat.currentSessionId`, `chat.sessions`, `chat.messages`, `chat.renameSession`, `chat.clearMessages`
- Produces:
  - 计算属性 `currentSessionTitle: ComputedRef<string>`
  - 响应式变量 `editingTopTitle: Ref<boolean>`, `topRenameText: Ref<string>`
  - 方法 `startTopRename()`, `saveTopRename()`, `cancelTopRename()`
  - 计算属性 `sessionMetrics: ComputedRef<{ totalTokens: number; totalCostUsd: number; msgCount: number }>`
  - 方法 `goToTraces()`（跳转 `/ai-traces`）
  - 模板中重构的三段式工具栏（`.toolbar-left`, `.toolbar-center`, `.toolbar-right`）

- [ ] **Step 1: 在 `Chat.vue` 中移除冗余控件并新增计算属性与重命名状态**

在 `<script setup>` 中：
1. 引入 `useRouter` 并定义 `const router = useRouter()`, `function goToTraces() { router.push('/ai-traces') }`；
2. 定义 `currentSessionTitle`：读取 `chat.sessions.find(s => s.id === chat.currentSessionId)?.title || '新对话'`；
3. 定义 `editingTopTitle = ref(false)`, `topRenameText = ref('')`；
4. 实现 `startTopRename()`：若当前会话存在，初始化 `topRenameText.value = currentSessionTitle.value` 并设 `editingTopTitle.value = true`；
5. 实现 `saveTopRename()`：若 `topRenameText.value.trim()` 存在且与原标题不同，调用 `chat.renameSession(chat.currentSessionId, title)`，然后置 `editingTopTitle.value = false`；
6. 实现 `cancelTopRename()`：置 `editingTopTitle.value = false`；
7. 定义 `sessionMetrics` 计算属性：
   ```ts
   const sessionMetrics = computed(() => {
     let totalTokens = 0
     let totalCostUsd = 0
     for (const m of chat.messages) {
       if (m.usage) {
         totalTokens += m.usage.total_tokens || 0
         totalCostUsd += m.usage.cost_usd || 0
       }
     }
     return { totalTokens, totalCostUsd, msgCount: chat.messages.length }
   })
   ```

- [ ] **Step 2: 重构 `.chat-toolbar` 模板结构**

在 `<template>` 的 `.chat-toolbar` 中：
1. **`.toolbar-left`**：
   - 保留 `toggle-sidebar-btn`；
   - 保留 `model-selector-wrap`；
   - 移除独立的 `.current-model-badge`；
   - 移除 `.toolbar-thinking-btn`；
   - 保留流式状态指示器与停止按钮。
2. **新增 `.toolbar-center`**：
   - 包含双态渲染：
     - 编辑态：`<input v-focus v-model="topRenameText" class="top-rename-input" @keydown.enter="saveTopRename" @keydown.esc="cancelTopRename" @blur="saveTopRename" />`
     - 展示态：`<div class="session-title-wrap" @dblclick="startTopRename" :title="currentSessionTitle"><span class="top-session-title">{{ currentSessionTitle }}</span><button v-if="chat.currentSessionId" class="top-rename-btn" @click="startTopRename" title="重命名会话"><Icon icon="ph:pencil-simple" /></button></div>`
3. **`.toolbar-right`**：
   - **度量胶囊**：`<div v-if="sessionMetrics.totalTokens > 0" class="session-stat-pill" :title="`累计消耗 ${sessionMetrics.totalTokens.toLocaleString()} tokens，估算成本 $${sessionMetrics.totalCostUsd.toFixed(4)}`"><Icon icon="ph:lightning-fill" class="stat-icon" /><span>{{ sessionMetrics.totalTokens.toLocaleString() }}</span><span class="stat-divider">·</span><span>${{ sessionMetrics.totalCostUsd.toFixed(4) }}</span></div>`
   - **分析面板快捷开关**：`<button v-if="!isMobile && panelItems.length > 0" class="toolbar-panel-btn" :class="{ active: panelVisible }" @click="panelVisible = !panelVisible" :title="panelVisible ? '收起分析面板' : '展开分析面板'"><Icon icon="ph:sparkle-bold" /><span class="panel-badge">{{ panelItems.length }}</span></button>`
   - **新建对话按钮**：`<el-button type="primary" size="small" plain class="toolbar-action-btn new-chat-btn" @click="handleNewChat"><Icon icon="ph:plus-bold" class="btn-icon" /><span class="btn-text">新对话</span></el-button>`
   - **更多操作下拉菜单**：
     ```html
     <el-dropdown trigger="click" placement="bottom-end">
       <button class="more-menu-btn" title="更多操作" aria-label="更多操作">
         <Icon icon="ph:dots-three-vertical-bold" />
       </button>
       <template #dropdown>
         <el-dropdown-menu class="chat-more-dropdown">
           <el-dropdown-item @click="exportToMarkdown" :disabled="chat.messages.length === 0">
             <Icon icon="ph:download-simple" class="dropdown-item-icon" />
             <span>导出 Markdown</span>
           </el-dropdown-item>
           <el-dropdown-item @click="copyAllChat" :disabled="chat.messages.length === 0">
             <Icon icon="ph:copy" class="dropdown-item-icon" />
             <span>复制全文</span>
           </el-dropdown-item>
           <el-dropdown-item @click="goToTraces">
             <Icon icon="ph:chart-line-up" class="dropdown-item-icon" />
             <span>AI 审计追踪</span>
           </el-dropdown-item>
           <el-dropdown-item divided @click="handleClearChat" :disabled="chat.messages.length === 0" class="danger-item">
             <Icon icon="ph:trash" class="dropdown-item-icon" />
             <span>清空本场记录</span>
           </el-dropdown-item>
         </el-dropdown-menu>
       </template>
     </el-dropdown>
     ```

- [ ] **Step 3: 运行测试验证**

运行: `npm --prefix frontend test -- tests/chat-toolbar.spec.ts --run`  
预期: PASS

- [ ] **Step 4: 提交模板与逻辑改动**

```bash
git add frontend/src/views/Chat.vue
git commit -m "feat(chat): ⚡ restructure chat toolbar with session title, metrics pill and more menu"
```

---

### Task 3: 优化工具栏样式、交互动效与响应式断点

**Files:**
- Modify: `frontend/src/views/Chat.vue:1515-1670` (CSS 样式区)

**Interfaces:**
- Consumes: CSS 变量 (`--mf-surface`, `--mf-border`, `--mf-primary`, etc.)
- Produces:
  - `.toolbar-center`, `.top-session-title`, `.top-rename-input`, `.top-rename-btn` 样式规则
  - `.session-stat-pill`, `.toolbar-panel-btn`, `.more-menu-btn` 样式规则
  - `@container chat-main` / `@media` 响应式隐藏与自适应规则

- [ ] **Step 1: 编写三段式布局与中间标题样式**

1. 设置 `.chat-toolbar` 的 `justify-content: space-between; gap: 12px; height: 50px; min-height: 50px;`；
2. 编写 `.toolbar-center`：
   - 居中展示、`flex: 1; max-width: 420px; display: flex; justify-content: center; align-items: center;`
   - `.session-title-wrap`: 鼠标悬停时平滑显示 `.top-rename-btn`；
   - `.top-session-title`: 单行省略，字重 600，颜色 `--mf-text-main`，双击可编辑提示；
   - `.top-rename-input`: 紧凑内联输入框，聚焦带有高亮边框和发光阴影。

- [ ] **Step 2: 编写度量胶囊与操作按钮样式**

1. 编写 `.session-stat-pill`：
   - 紧凑圆角胶囊 (`border-radius: 12px; padding: 2px 10px; font-size: 11px; font-family: monospace;`)；
   - 背景 `--mf-surface-hover`，边框 `--mf-border`，文字带有轻微微光；
2. 编写 `.toolbar-panel-btn`：
   - 快捷唤起分析面板按钮，激活态背景高亮，带有数字红点/角标 `.panel-badge`；
3. 编写 `.more-menu-btn`：
   - 极简悬停按钮，圆角 6px，悬停平滑过渡；
4. 编写下拉菜单图标与文字对齐样式 `.chat-more-dropdown .dropdown-item-icon`。

- [ ] **Step 3: 编写自适应响应式断点**

```css
@media (max-width: 1024px) {
  .session-stat-pill {
    display: none;
  }
  .toolbar-center {
    max-width: 200px;
  }
  .new-chat-btn .btn-text {
    display: none;
  }
}

@media (max-width: 768px) {
  .toolbar-center {
    display: none;
  }
}
```

- [ ] **Step 4: 执行全量前端测试与生产构建验证**

运行:
```bash
npm --prefix frontend test -- --run
npm --prefix frontend run build
```
预期:
- 单元测试 100% 全部通过；
- Vue TypeScript 编译与 Vite 打包通过，0 错误。

- [ ] **Step 5: 提交样式与响应式优化**

```bash
git add frontend/src/views/Chat.vue
git commit -m "style(chat): 💄 style context hub toolbar with responsive breakpoints and micro-interactions"
```

---

### Task 4: 端到端功能回归与全面验证

**Files:**
- Test: `frontend/tests/` 全部测试集

- [ ] **Step 1: 执行所有前端测试套件**

运行: `npm --prefix frontend test -- --run`  
预期: 全部测试用例 PASS。

- [ ] **Step 2: 执行前端生产环境打包**

运行: `npm --prefix frontend run build`  
预期: 成功生成 `dist/`，耗时约 10s，0 TS 报错。

- [ ] **Step 3: 检查 Git 变更与工作树状态**

运行: `git status && git diff HEAD~3..HEAD --stat`  
预期: 变更仅涉及 `Chat.vue` 与 `chat-toolbar.spec.ts`，工作区干净无脏文件。
