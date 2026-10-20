# AI 聊天页面布局优化 — 右侧分析面板

## 意图与动机

Minefolio 的 AI 聊天页面 (`Chat.vue`) 目前采用双栏布局：左侧会话列表 (280px) + 中央对话流 (max 1060px 居中)。在宽屏显示器上，对话区两侧留白大量浪费空间；同时 AI 返回的结构化内容（图表、分析报告、操作卡片、工作流进度、代码块）全部混在对话流里纵向堆叠，用户需要反复滚动才能在对话和分析结果之间切换。

本次优化引入右侧分析面板 (Analysis Panel)，将对话与结构化内容分离：对话留在中央保持聚焦，分析结果自动弹出到右侧面板，通过标签页管理多个内容项。宽屏留白被面板填充；面板关闭时对话流恢复居中。

## 目标

1. **宽屏空间利用**：在 ≥1200px 可用宽度时，右侧面板内联展示，消除大面积留白。
2. **内容分离**：所有结构化 AI 输出（Mermaid 图表、Markdown 报告/表格、ActionCard、WorkflowProgressCard、WorkflowConfigCard、CodeBlock）自动抽取到右侧面板，对话流只保留简要摘要 + 链接。
3. **标签页管理**：面板内多个内容项通过标签页切换，用户可回看本次会话中产生的所有分析结果。
4. **窄屏适配**：在 <1200px 可用宽度时，面板以覆盖式抽屉（从右侧滑出）呈现，不挤压对话区。
5. **可关闭**：面板可手动关闭，关闭后对话流恢复现有的居中 max-width 布局。

## 非目标

- 不重新设计对话流本身的消息样式或交互（复制、编辑、重新生成等保持不变）。
- 不改变后端 AI 返回的数据结构或 API。
- 不改变移动端布局（移动端保持现有的全宽对话流，结构化内容继续 inline 展示）。
- 不引入持久化面板状态到服务器（面板状态为前端会话级）。

## 反目标（如果出现这些情况就算失败）

- 面板强制占屏、无法关闭，用户在窄屏上被迫使用受压缩的对话区。
- 对话流中完全没有任何结构化内容的踪迹，用户不知道 AI 曾经输出过什么。
- 标签页无限增长没有上限，导致内存或性能问题。
- 流式输出期间面板频繁闪烁或重排。

## 约束

- **前端独立改动**：不涉及后端 C 代码或 API 变更。
- **现有组件复用**：`ChatMessageContent.vue`、`ActionCard.vue`、`MermaidBlock.vue`、`CodeBlock.vue`、`WorkflowProgressCard.vue`、`WorkflowConfigCard.vue` 等组件在面板中复用，不需要重写。
- **Vue 3 + TypeScript**：所有新代码遵循项目现有的 Vue 3 Composition API + TypeScript 规范。
- **Element Plus**：UI 组件优先使用已有的 Element Plus 组件（如 `el-tabs`、`el-button`）。
- **CSS 变量**：使用现有的 `--mf-*` CSS 变量体系，支持 light/dark 主题。
- **Container Queries**：利用现有的 `container-type: inline-size` on `.chat-main` 来判断可用宽度。

## 设计

### 布局结构

```
┌─────────────────────────────────────────────────────────────────┐
│ .page-header (保持不变)                                          │
├──────────┬────────────────────────┬─────────────────────────────┤
│ .chat-   │ .chat-main             │ .analysis-panel             │
│ sidebar  │ (flex:1)               │ (width: 420px, 可拖拽调整)   │
│ (280px)  │                        │                             │
│          │ .model-bar             │ .panel-header               │
│          │ .messages              │  标题 + 关闭按钮             │
│          │  (居中, max-width 自适应)│ .panel-tabs                │
│          │                        │  [图表] [报告] [操作] ...    │
│          │                        │ .panel-content              │
│          │ .chat-input-area       │  (当前选中标签的内容)         │
│          │  (浮岛输入框)            │                             │
└──────────┴────────────────────────┴─────────────────────────────┘
```

### 三栏 Flex 布局

`.chat-layout` 保持 `display: flex`，新增 `.analysis-panel` 作为第三个 flex 子元素：

```
.chat-layout
  ├── .chat-sidebar (width: 280px | 0)
  ├── .chat-main (flex: 1)
  └── .analysis-panel (width: 420px | 0, 可通过 CSS transition 展开/收起)
```

当面板打开时，`.chat-main` 的 `flex: 1` 自动收缩。由于 `.chat-main` 已有 `container-type: inline-size`，`.messages` 和 `.chat-input-area` 的 `max-width: min(1060px, 100cqi)` 会自然适应变窄的 container。

### 分析面板组件：`AnalysisPanel.vue`

新建组件 `frontend/src/components/AnalysisPanel.vue`：

**Props:**
- `visible: boolean` — 面板是否打开
- `items: PanelItem[]` — 面板中的内容项列表
- `activeTabId: string` — 当前激活的标签页 ID
- `overlay: boolean` — 是否以覆盖式抽屉模式渲染

**Emits:**
- `close` — 关闭面板
- `update:activeTabId` — 切换标签页
- `removeItem(id)` — 移除某个标签页

**PanelItem 类型:**
```ts
interface PanelItem {
  id: string                 // 唯一标识，如 `msg-{messageId}-{segIdx}`
  label: string              // 标签页显示名称，如 "收支饼图"、"分析报告"、"记账操作"
  icon: string               // Iconify 图标名
  type: 'mermaid' | 'markdown' | 'action' | 'code' | 'workflow-progress' | 'workflow-config'
  content: string            // 原始内容（Mermaid 代码 / Markdown 文本 / JSON / 代码）
  lang?: string              // 代码块语言
  actionData?: ProposedAction
  workflowData?: WorkflowRunState
  workflowConfig?: WorkflowConfigState
  messageId?: number         // 关联的消息 ID
  createdAt: number          // 时间戳，用于排序
}
```

**面板内部结构:**
```html
<div class="analysis-panel" :class="{ overlay, visible }">
  <div class="panel-header">
    <span class="panel-title">分析结果</span>
    <button @click="$emit('close')">×</button>
  </div>
  <el-tabs v-model="activeTabId" type="card" closable @tab-remove="removeItem">
    <el-tab-pane v-for="item in items" :key="item.id" :name="item.id" :label="item.label">
      <!-- 根据 item.type 渲染对应组件 -->
      <MermaidBlock v-if="item.type === 'mermaid'" :code="item.content" />
      <div v-else-if="item.type === 'markdown'" class="markdown-part" v-html="renderMarkdown(item.content)" />
      <ActionCard v-else-if="item.type === 'action'" :action-data="item.actionData" />
      <CodeBlock v-else-if="item.type === 'code'" :code="item.content" :lang="item.lang" />
      <WorkflowProgressCard v-else-if="item.type === 'workflow-progress'" :workflow-data="item.workflowData" />
      <WorkflowConfigCard v-else-if="item.type === 'workflow-config'" :message-id="item.messageId" :config="item.workflowConfig" />
    </el-tab-pane>
  </el-tabs>
</div>
```

### 内容抽取逻辑

`ChatMessageContent.vue` 中的 `segments` computed 已经将消息内容解析为 `Segment[]`（类型有 `markdown`、`mermaid`、`action`、`code`、`streaming-text`、`streaming-mermaid`）。

**抽取时机：** 当一条 assistant 消息的流式输出结束（`isStreaming` 从 `true` 变为 `false`）时，`Chat.vue` 扫描该消息的 segments，将符合条件的 segment 注册为 `PanelItem` 并添加到面板。

**抽取规则（用户决策：方案 A 结构化抽取）：**
- `mermaid` segment → 面板 item，label 取 `"图表 #{n}"`
- `action` segment → 面板 item，label 取 action_type 中文名（如"记账确认"、"转账操作"）
- `code` segment → 面板 item，label 取语言名或 `"代码块"`
- `workflow-progress` / `workflow-config` → 面板 item
- **Markdown 表格/专业报告**：当段落中包含 Markdown Table（`|...|`）时抽取至面板，普通纯文字长回复保留在对话流内，保证日常连续对话自然连贯。

**历史会话加载行为（用户决策：方案 A 默认收起）：**
- 切换到历史会话时，右侧面板默认**保持收起状态**。
- 历史消息中被抽取的元素在对话流中呈现为引用卡片，用户主动点击卡片才唤出面板并定位到该标签页，避免打扰。

**对话流内的替换：** 被抽取的 segment 在对话流中替换为一个紧凑的引用链接卡片：

```html
<div class="panel-ref" @click="openPanelItem(item.id)">
  <Icon :icon="item.icon" />
  <span>{{ item.label }}</span>
  <Icon icon="ph:arrow-right" />
</div>
```

样式为一个小型胶囊卡片，点击后在面板中激活对应标签页。

### 面板状态管理

在 `Chat.vue` 中管理面板状态（不引入新 Pinia store，保持会话级）：

```ts
const panelVisible = ref(false)
const panelItems = ref<PanelItem[]>([])
const activeTabId = ref('')
const panelOverlay = ref(false) // 由 container query 或 resize 决定

// 最大标签页数量限制
const MAX_PANEL_ITEMS = 20

function addPanelItem(item: PanelItem) {
  if (panelItems.value.length >= MAX_PANEL_ITEMS) {
    panelItems.value.shift() // 移除最早的
  }
  panelItems.value.push(item)
  activeTabId.value = item.id
  panelVisible.value = true
}

function removePanelItem(id: string) {
  const idx = panelItems.value.findIndex(i => i.id === id)
  if (idx >= 0) panelItems.value.splice(idx, 1)
  if (panelItems.value.length === 0) panelVisible.value = false
  else if (activeTabId.value === id) {
    activeTabId.value = panelItems.value[Math.min(idx, panelItems.value.length - 1)].id
  }
}

// 切换会话时清空面板
watch(() => chat.currentSessionId, () => {
  panelItems.value = []
  panelVisible.value = false
  activeTabId.value = ''
})
```

### 响应式断点

利用 `.chat-main` 现有的 `container-type: inline-size`：

| `.chat-main` 可用宽度 | 面板模式 | 行为 |
|---|---|---|
| ≥ 800px | **内联** | 面板作为 flex 子元素，宽度 420px |
| < 800px 且 ≥ 768px（非移动端） | **覆盖式抽屉** | 面板 `position: fixed; right: 0`，带半透明遮罩 |
| < 768px（移动端） | **不使用面板** | 结构化内容继续 inline 展示在对话流中 |

判断逻辑：在 `.chat-main` 上添加 `ResizeObserver`，当宽度 < 800px 时 `panelOverlay.value = true`。

### 面板宽度可调

面板默认宽度 420px，用户可通过拖拽左边框调整（300px ~ 600px）。使用一个 `.panel-resize-handle` 元素 + `mousedown/mousemove/mouseup` 事件实现，宽度存入 `localStorage`（key: `minefolio:analysis-panel-width`）。

### 流式输出期间的行为

- 流式输出期间，结构化内容 **仍然 inline 展示在对话流中**（与当前行为一致），面板不做任何更新。
- 流式结束后（`isStreaming` 变 `false`），触发抽取逻辑，将 segments 移到面板。
- 这避免了流式期间面板频繁闪烁的问题。

### CSS 关键样式

```css
/* Analysis Panel - Inline Mode */
.analysis-panel {
  width: 420px;
  min-width: 300px;
  max-width: 600px;
  display: flex;
  flex-direction: column;
  border-left: 1px solid var(--mf-border);
  background: var(--mf-surface);
  backdrop-filter: blur(16px);
  transition: width 0.25s cubic-bezier(0.4, 0, 0.2, 1);
  overflow: hidden;
}

.analysis-panel:not(.visible) {
  width: 0;
  min-width: 0;
  border-left: none;
}

/* Analysis Panel - Overlay Mode */
.analysis-panel.overlay {
  position: fixed;
  right: 0;
  top: 0;
  bottom: 0;
  z-index: 100;
  box-shadow: -4px 0 20px rgba(0, 0, 0, 0.15);
  transform: translateX(100%);
  transition: transform 0.25s cubic-bezier(0.4, 0, 0.2, 1);
}

.analysis-panel.overlay.visible {
  transform: translateX(0);
}
```

### 对话流中引用卡片样式

```css
.panel-ref {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 6px 12px;
  background: var(--mf-primary-light);
  border: 1px solid var(--mf-primary-border);
  border-radius: 10px;
  color: var(--mf-primary);
  font-size: 12.5px;
  font-weight: 500;
  cursor: pointer;
  transition: background 0.15s, transform 0.12s;
}

.panel-ref:hover {
  background: var(--mf-surface-hover);
  transform: translateY(-1px);
}
```

## 架构与构建者裁定 (Builder's Calls)

以下细节已由构建者按最佳工程实践确定并记录：

1. **ActionCard 交互完整性**：抽取到面板中的操作卡片（如记账、转账）保持与 inline 完全一致的双向交互与 API 联动，操作完成后状态同步更新，并在 Tab 标题上打上“已完成”标记。
2. **面板宽度自适应与记忆**：默认 420px，支持在 300px~600px 间自由拖拽，尺寸保存在 `localStorage` (`minefolio:analysis-panel-width`)。
3. **标签页上限与策略**：上限 20 个（FIFO 队列，新出分析自动顶掉最旧标签，支持手动 `x` 关闭）。
4. **流式抗闪烁机制**：流式生成期间所有内容临时停留在对话流打字，生成结束且语法闭合的一瞬间无缝移入右侧面板并弹出，绝不产生布局抖动。

## 留给实现者的决定

- `panel-ref` 引用卡片的具体 icon 映射（哪种 segment type 对应哪个 Iconify icon）。
- 标签页标题的截断长度（建议 8~10 字）和 tooltip 完整展示策略。
- 覆盖式抽屉遮罩的半透明样式与 blur 细节。
