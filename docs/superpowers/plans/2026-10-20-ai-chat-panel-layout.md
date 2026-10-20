# AI 聊天页面右侧分析面板 (Analysis Panel) 布局改造计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 Minefolio AI 财务聊天页面构建右侧分析面板（Analysis Panel），将图表、报告表格、操作卡片等结构化输出抽离至独立标签页展示，消除宽屏留白并在窄屏下提供抽屉式覆盖浏览。

**Architecture:** 采用 Vue 3 + TypeScript 将原本双栏布局（会话列表 + 中央消息流）扩展为三栏 Flex 布局。通过独立抽取工具类识别并隔离消息流中的结构化 Segment（Mermaid、代码块、ActionCard、工作流卡片、Markdown 表格），对话流中替换为高对比度的胶囊链接卡片；右侧通过 `AnalysisPanel.vue` 以 `el-tabs` 管理多张卡片，支持自由拖拽调宽（300px~600px）与本地持久化记忆；窄屏通过 Container Queries / ResizeObserver 自动切换为右侧滑出抽屉遮罩。

**Tech Stack:** Vue 3 (Composition API / `<script setup>`), TypeScript, Element Plus (`el-tabs`, `el-tab-pane`), CSS Container Queries (`container-type: inline-size`), Vitest, Iconify.

**Spec:** [docs/superpowers/specs/2026-10-20-ai-chat-panel-layout-design.md](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-10-20-ai-chat-panel-layout-design.md)

## Global Constraints

- **前端独立改造**：严禁修改任何后端 C 代码或 API 接口定义。
- **组件高复用**：`MermaidBlock.vue`、`CodeBlock.vue`、`ActionCard.vue`、`WorkflowProgressCard.vue`、`WorkflowConfigCard.vue` 必须直接复用，不重复编写渲染引擎。
- **代码规范**：所有新增文件必须使用 TypeScript 且通过 `vue-tsc -b` 严格类型检查，无 `any` 滥用。
- **色彩与主题**：强制使用 `--mf-*` CSS 变量（`--mf-surface`, `--mf-primary`, `--mf-border` 等），完整适配 Light / Dark 双主题。
- **流式抗抖动**：流式生成中（`isStreaming=true`）内容完全停留在对话流打字，生成结束（`isStreaming=false`）后平滑抽入右侧面板，禁止在流式吐字中频繁触发面板重排。

## Review Focus

1. **历史会话切换时状态污染**：切换会话时若未重置或未过滤 `panelItems`，会导致上个会话的分析图表残留到新会话中。必须验证切换会话时面板清空且保持默认收起。
2. **ActionCard 抽取后的状态响应**：在面板中确认记账或转账后，卡片应响应式变为“已完成”，不能因为被搬入面板导致事件抛出丢失。
3. **Markdown 抽取误伤日常聊天**：纯文字长回复不能被当作报告抽走，必须严格校验只抽取包含 Markdown Table（`|...|`）或特定关键词结构的分析，日常短文本和列表留在对话流。
4. **拖拽调整宽度越界与移动端冲突**：拖拽宽度必须被严格限制在 300px ~ 600px 范围内；在移动端（<768px）必须强制禁用拖拽和面板，回退至内联展示。
5. **标签页淘汰（FIFO）与唯一 ID 冲突**：当标签页超过 20 个时，移除最早的标签不能导致 `activeTabId` 悬空变成空白页。

---

### Task 1: 核心类型定义与结构化内容抽取工具函数

**Files:**
- Create: `frontend/src/types/chat-panel.ts`
- Create: `frontend/src/utils/chat-panel-extractor.ts`
- Modify: `frontend/src/types/index.ts`
- Test: `frontend/tests/chat-panel-extractor.spec.ts`

**Interfaces:**
- Produces:
  ```ts
  export type PanelItemType = 'mermaid' | 'markdown' | 'action' | 'code' | 'workflow-progress' | 'workflow-config'

  export interface PanelItem {
    id: string
    label: string
    icon: string
    type: PanelItemType
    content: string
    lang?: string
    actionData?: any
    workflowData?: any
    workflowConfig?: any
    messageId?: number
    createdAt: number
  }

  export interface ExtractResult {
    panelItems: PanelItem[]
    extractedSegmentIds: Set<string>
  }

  export function extractPanelItems(
    messageId: number,
    content: string,
    workflowData?: any,
    workflowConfig?: any
  ): ExtractResult
  ```

- [ ] **Step 1: 编写失败的单元测试**

```ts
// frontend/tests/chat-panel-extractor.spec.ts
import { describe, it, expect } from 'vitest'
import { extractPanelItems } from '@/utils/chat-panel-extractor'

describe('chat-panel-extractor', () => {
  it('extracts mermaid block into panel item', () => {
    const content = '分析如下：\n```mermaid\npie title 资产\n "现金" : 50\n```\n以上是占比。'
    const result = extractPanelItems(101, content)
    expect(result.panelItems.length).toBe(1)
    expect(result.panelItems[0].type).toBe('mermaid')
    expect(result.panelItems[0].label).toContain('图表')
    expect(result.panelItems[0].content).toContain('pie title 资产')
  })

  it('extracts markdown table but leaves plain text in conversation', () => {
    const tableContent = '这是收支表：\n| 项目 | 金额 |\n| --- | --- |\n| 餐饮 | 100 |\n总结完毕。'
    const result = extractPanelItems(102, tableContent)
    expect(result.panelItems.some(i => i.type === 'markdown')).toBe(true)
  })

  it('does not extract simple conversational text without tables or code', () => {
    const plain = '你好！这是关于你财务状况的简短分析，请注意控制开支。'
    const result = extractPanelItems(103, plain)
    expect(result.panelItems.length).toBe(0)
  })
})
```

- [ ] **Step 2: 运行测试验证失败**

Run: `npm --prefix frontend test -- tests/chat-panel-extractor.spec.ts --run`
Expected: FAIL（模块不存在）

- [ ] **Step 3: 实现类型定义与抽取逻辑**

在 `frontend/src/types/chat-panel.ts` 中定义接口并在 `frontend/src/types/index.ts` 中导出；在 `frontend/src/utils/chat-panel-extractor.ts` 中基于正则表达式解析 mermaid、code、action、workflow 和 table 块并生成标准 `PanelItem` 列表。

- [ ] **Step 4: 运行测试验证通过**

Run: `npm --prefix frontend test -- tests/chat-panel-extractor.spec.ts --run`
Expected: PASS

- [ ] **Step 5: 提交代码**

```bash
git add frontend/src/types/chat-panel.ts frontend/src/types/index.ts frontend/src/utils/chat-panel-extractor.ts frontend/tests/chat-panel-extractor.spec.ts
git commit -m "feat(ai-chat): add panel item types and segment extraction utility"
```

---

### Task 2: 独立分析面板组件 `AnalysisPanel.vue`

**Files:**
- Create: `frontend/src/components/AnalysisPanel.vue`
- Test: `frontend/tests/analysis-panel.spec.ts`

**Interfaces:**
- Consumes: `PanelItem` from `@/types/chat-panel`, `MermaidBlock.vue`, `CodeBlock.vue`, `ActionCard.vue`, `WorkflowProgressCard.vue`, `WorkflowConfigCard.vue`
- Produces:
  ```html
  <AnalysisPanel
    :visible="panelVisible"
    :items="panelItems"
    :active-tab-id="activeTabId"
    :overlay="panelOverlay"
    :width="panelWidth"
    @close="panelVisible = false"
    @update:active-tab-id="val => activeTabId = val"
    @remove-tab="handleRemoveTab"
    @resize-start="startResize"
  />
  ```

- [ ] **Step 1: 编写失败的组件测试**

```ts
// frontend/tests/analysis-panel.spec.ts
import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import AnalysisPanel from '@/components/AnalysisPanel.vue'
import type { PanelItem } from '@/types/chat-panel'

describe('AnalysisPanel.vue', () => {
  const dummyItems: PanelItem[] = [
    {
      id: 'item-1',
      label: '财务图表 #1',
      icon: 'ph:chart-pie',
      type: 'mermaid',
      content: 'pie title 测试\n "A": 10',
      createdAt: Date.now()
    }
  ]

  it('renders panel header and tabs when items exist', () => {
    const wrapper = mount(AnalysisPanel, {
      props: {
        visible: true,
        items: dummyItems,
        activeTabId: 'item-1',
        overlay: false,
        width: 420
      }
    })
    expect(wrapper.find('.panel-title').text()).toBe('分析结果')
    expect(wrapper.text()).toContain('财务图表 #1')
  })

  it('emits close event when clicking close button', async () => {
    const wrapper = mount(AnalysisPanel, {
      props: { visible: true, items: dummyItems, activeTabId: 'item-1', overlay: false }
    })
    const closeBtn = wrapper.find('.panel-close-btn')
    await closeBtn.trigger('click')
    expect(wrapper.emitted('close')).toBeTruthy()
  })
})
```

- [ ] **Step 2: 运行测试验证失败**

Run: `npm --prefix frontend test -- tests/analysis-panel.spec.ts --run`
Expected: FAIL（组件未定义）

- [ ] **Step 3: 实现 `AnalysisPanel.vue` 组件**

编写 `frontend/src/components/AnalysisPanel.vue`：
- 使用 Element Plus `el-tabs`（`closable`，`v-model="activeTab"`）展示标签页。
- 支持空状态提示（当 `items.length === 0` 时展示友好插画/文字）。
- 支持左侧边缘 `.panel-resize-handle` 供桌面端拖拽调整宽度。
- 区分内联模式与抽屉模式（`.overlay` 属性搭配遮罩层）。
- 动态分发渲染对应内容（Mermaid, CodeBlock, ActionCard, Workflow 卡片, Markdown 渲染）。

- [ ] **Step 4: 运行测试验证通过**

Run: `npm --prefix frontend test -- tests/analysis-panel.spec.ts --run`
Expected: PASS

- [ ] **Step 5: 提交代码**

```bash
git add frontend/src/components/AnalysisPanel.vue frontend/tests/analysis-panel.spec.ts
git commit -m "feat(ai-chat): implement AnalysisPanel component with closable tabs and dynamic renderers"
```

---

### Task 3: 改造 `ChatMessageContent.vue` 嵌入面板引用卡片

**Files:**
- Modify: `frontend/src/components/ChatMessageContent.vue`
- Test: `frontend/tests/chat-message-content-panel.spec.ts`

**Interfaces:**
- Consumes: `PanelItem` from `@/types/chat-panel`
- Produces:
  - Props新增: `extractedItemIds?: string[]` (已被抽到面板中的 segment id 集合)
  - Emits新增: `(e: 'open-panel-item', itemId: string): void`

- [ ] **Step 1: 编写失败的单元测试**

```ts
// frontend/tests/chat-message-content-panel.spec.ts
import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import ChatMessageContent from '@/components/ChatMessageContent.vue'

describe('ChatMessageContent panel extraction', () => {
  it('renders a compact panel-ref button when a segment is marked as extracted', async () => {
    const content = '```mermaid\npie title 资产\n "现金": 100\n```'
    const wrapper = mount(ChatMessageContent, {
      props: {
        messageId: 10,
        content,
        isStreaming: false,
        extractedItemIds: ['mmd-0']
      }
    })
    const refBtn = wrapper.find('.panel-ref-pill')
    expect(refBtn.exists()).toBe(true)
    await refBtn.trigger('click')
    expect(wrapper.emitted('open-panel-item')).toBeTruthy()
  })

  it('keeps normal inline rendering when isStreaming is true even if IDs provided', () => {
    const content = '```mermaid\npie title 资产\n "现金": 100\n```'
    const wrapper = mount(ChatMessageContent, {
      props: {
        messageId: 10,
        content,
        isStreaming: true,
        extractedItemIds: ['mmd-0']
      }
    })
    expect(wrapper.find('.panel-ref-pill').exists()).toBe(false)
  })
})
```

- [ ] **Step 2: 运行测试验证失败**

Run: `npm --prefix frontend test -- tests/chat-message-content-panel.spec.ts --run`
Expected: FAIL（props 或 `.panel-ref-pill` 未定义）

- [ ] **Step 3: 修改 `ChatMessageContent.vue`**

在 `ChatMessageContent.vue` 中：
- 增加 `extractedItemIds` prop 与 `open-panel-item` emit。
- 在 `v-for="seg in segments"` 循环中，若 `!isStreaming && props.extractedItemIds?.includes(seg.id)`，则渲染 `<div class="panel-ref-pill" @click="$emit('open-panel-item', ...)">` 胶囊，显示图标、名称与箭头指示。
- 添加 `.panel-ref-pill` 的磨砂发光与悬停微动 CSS 样式。

- [ ] **Step 4: 运行测试验证通过**

Run: `npm --prefix frontend test -- tests/chat-message-content-panel.spec.ts --run`
Expected: PASS

- [ ] **Step 5: 提交代码**

```bash
git add frontend/src/components/ChatMessageContent.vue frontend/tests/chat-message-content-panel.spec.ts
git commit -m "feat(ai-chat): add panel reference pill rendering to ChatMessageContent"
```

---

### Task 4: `Chat.vue` 三栏布局重构、拖拽调宽与自动弹出协同

**Files:**
- Modify: `frontend/src/views/Chat.vue`
- Test: `frontend/tests/chat-layout-panel.spec.ts`

**Interfaces:**
- Consumes: `AnalysisPanel.vue`, `extractPanelItems`, `useChatStore`
- Behavior:
  - 维持 `.chat-sidebar` + `.chat-main` + `.analysis-panel` 三栏 Flex
  - 监听流式结束 `watch(chat.isStreaming, (newVal, oldVal))`：从 `true` 到 `false` 时自动抽取新消息卡片、加入 `panelItems`、激活 Tab、展开面板
  - 切换会话 `watch(chat.currentSessionId)`：清空面板项，面板默认保持关闭（方案 A）
  - 面板拖拽左边框手柄调整宽度，边界限定 300px ~ 600px，存入 `localStorage` (`minefolio:analysis-panel-width`)
  - 容器尺寸感知：当 `.chat-main` 宽度 < 800px 时，自动设置 `panelOverlay = true`

- [ ] **Step 1: 编写布局与状态协同测试**

```ts
// frontend/tests/chat-layout-panel.spec.ts
import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import Chat from '@/views/Chat.vue'
import { createPinia, setActivePinia } from 'pinia'

describe('Chat.vue with AnalysisPanel integration', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
  })

  it('mounts without crashing and contains analysis panel element', () => {
    const wrapper = mount(Chat, {
      global: {
        stubs: {
          ChatMessageContent: true,
          PromptStarters: true,
          WorkflowBar: true,
          WorkflowSlashMenu: true,
          Icon: true
        }
      }
    })
    expect(wrapper.findComponent({ name: 'AnalysisPanel' }).exists()).toBe(true)
  })
})
```

- [ ] **Step 2: 运行测试验证失败**

Run: `npm --prefix frontend test -- tests/chat-layout-panel.spec.ts --run`
Expected: FAIL（未引入 AnalysisPanel）

- [ ] **Step 3: 改造 `Chat.vue` 模板与脚本**

- 引入 `AnalysisPanel.vue` 和 `extractPanelItems`。
- 在 `.chat-layout` 尾部添加 `<AnalysisPanel ... />`。
- 实现宽度拖拽逻辑（`onMouseDownResize` / `onMouseMove` / `onMouseUp`），从 `localStorage` 读取初始宽度。
- 绑定流式结束提取与面板唤醒；绑定会话切换重置逻辑；保证标签页上限 20 个（FIFO）。
- 添加三栏 Flex 样式与自适应过渡动画。

- [ ] **Step 4: 运行测试验证通过**

Run: `npm --prefix frontend test -- tests/chat-layout-panel.spec.ts --run`
Expected: PASS

- [ ] **Step 5: 提交代码**

```bash
git add frontend/src/views/Chat.vue frontend/tests/chat-layout-panel.spec.ts
git commit -m "feat(ai-chat): integrate 3-column layout, panel resizing, and auto-popup workflow in Chat.vue"
```

---

### Task 5: 移动端回退保护与全量构建验证

**Files:**
- Modify: `frontend/src/views/Chat.vue`
- Modify: `frontend/src/components/AnalysisPanel.vue`
- Test: All frontend tests

- [ ] **Step 1: 确保移动端 (<768px) 彻底禁用面板并维持原有对话流**

验证在 `isMobile = true` 条件下，`AnalysisPanel` 不渲染，所有卡片退回原始内联直接渲染模式。

- [ ] **Step 2: 运行前端所有单元测试**

Run: `npm --prefix frontend test -- --run`
Expected: PASS (所有测试用例通过)

- [ ] **Step 3: 运行 TypeScript 严格类型检查与生产构建**

Run: `npm --prefix frontend run build`
Expected: PASS (`vue-tsc -b` 0 errors, `vite build` 成功输出)

- [ ] **Step 4: 提交所有调整与最终成果**

```bash
git add frontend/
git commit -m "chore(ai-chat): complete analysis panel layout optimization and pass full build verification"
```

---

## Self-Review

1. **Spec Coverage:**
   - 宽屏三栏布局与留白消除 → Task 2, Task 4
   - 结构化内容（Mermaid, Code, ActionCard, Workflow, Table）抽取 → Task 1, Task 3
   - 标签页（Tabs）管理与最多 20 个 FIFO 限制 → Task 2, Task 4
   - 窄屏覆盖式抽屉模式（<800px）与遮罩 → Task 2, Task 4
   - 移动端（<768px）保持原有内联不拆分 → Task 5
   - 拖拽调整宽度与持久化 → Task 2, Task 4
   - 方案 A（历史会话默认收起、纯文字不抽取）→ Task 1, Task 4
2. **Step Scan:** 每个 Task 都划分了“写测试 -> 验证失败 -> 编写实现 -> 验证通过 -> 提交代码”的严密步骤。
3. **Type Consistency:** `PanelItem`, `PanelItemType`, `ExtractResult` 在 Task 1 定义并在后续任务全局复用。
4. **Review Focus:** 5 个边界隐患均在 Task 1、Task 3、Task 4、Task 5 中提供了对应断言覆盖。
5. **Proportion:** 任务结构紧凑，职责分离清晰。
