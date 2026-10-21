# Chat 页面布局优化实施计划 (Chat Layout Optimization)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实施 Chat 页面三大核心布局重构：
1. **Chat 页面 Full-bleed 贴边**：在 `Layout.vue` 针对 `/chat` 移除外层 `24px 28px` 的内边距，使侧边栏平滑贴合；
2. **Chat 顶栏三合一**：精简/合并 `Chat.page-header` 与 `.model-bar`，为对话区域直接释放 50px~70px 纵向视野；
3. **Chat 输入框布局解耦**：将 `.chat-input-area` 从绝对定位改为 Flex 纵向底栏，消除消息内容被遮挡的致命 Bug。

**Tech Stack:** Vue 3, TypeScript, Pinia, Element Plus, CSS Container Queries, Vitest.

---

### Task 1: 全局布局 Full-Bleed 贴边适配 (`Layout.vue` & `router/index.ts`)

**Files:**
- Modify: `frontend/src/router/index.ts:40-45`
- Modify: `frontend/src/views/Layout.vue:120-130, 515-530`
- Test: `frontend/tests/layout-fullbleed.spec.ts`

**Interfaces:**
- Consumes: `route.meta.fullBleed`
- Produces: `.main.is-full-bleed` 样式（`padding: 0; overflow: hidden;`）

- [ ] **Step 1: 编写 Full-Bleed 布局单测**
  在 `frontend/tests/layout-fullbleed.spec.ts` 中新增测试用例：
  - 验证当路由元信息 `fullBleed: true` 时，`Layout.vue` 的 `.main` 容器渲染 `is-full-bleed` class；
  - 验证对应的样式规则去除了 padding 并设置 overflow 为 hidden。

- [ ] **Step 2: 在路由与 `Layout.vue` 中支持 `fullBleed`**
  - 在 `frontend/src/router/index.ts` 中为 `/chat` 路由增加 `meta: { requiresAuth: true, fullBleed: true }`；
  - 在 `frontend/src/views/Layout.vue` 中为 `<el-main class="main">` 绑定 `:class="{ 'is-full-bleed': route.meta.fullBleed }"`；
  - 在 `<style scoped>` 中添加：
    ```css
    .main.is-full-bleed {
      padding: 0;
      overflow: hidden;
    }
    ```

- [ ] **Step 3: 运行测试验证**
  Run: `npm --prefix frontend test -- tests/layout-fullbleed.spec.ts`
  Expected: PASS

- [ ] **Step 4: Commit**
  ```bash
  git add frontend/src/router/index.ts frontend/src/views/Layout.vue frontend/tests/layout-fullbleed.spec.ts
  git commit -m "feat(layout): support full-bleed edge-to-edge container for chat route"
  ```

---

### Task 2: Chat 顶栏三合一整合 (`Chat.vue`)

**Files:**
- Modify: `frontend/src/views/Chat.vue:1-170, 1030-1160, 1330-1420`
- Test: `frontend/tests/chat-toolbar.spec.ts`

**Interfaces:**
- Consumes: `Chat.vue` 的会话导出、复制、清空、新对话与模型选择交互
- Produces: 统一的 `.chat-toolbar` 与全高 `.chat-sidebar`

- [ ] **Step 1: 编写顶栏整合单测**
  在 `frontend/tests/chat-toolbar.spec.ts` 中测试：
  - `Chat.vue` 不再包含孤立的全局横向 `.page-header`；
  - 会话侧边栏顶部包含“新对话”操作；
  - 对话主区顶部包含统一的 `.chat-toolbar`，在单行内集成折叠切换、模型选择以及操作按钮。

- [ ] **Step 2: 改造 `Chat.vue` 模板与组件结构**
  - 移除原先横跨整个页面顶部的 `.page-header` 结构（第 3-46 行）；
  - 使 `.chat-sidebar` 成为全高度容器（从顶部贴合到底部），在 `.sidebar-header` 中并排增加“新对话”操作按钮；
  - 将 `.model-bar` 重构为 `.chat-toolbar`：
    - 左侧：侧边栏展开图标按钮（当侧边栏折叠时）、模型选择器 (`el-select`)、模型标识；
    - 右侧：思考与生成状态指示（流式中）、“导出”、“复制”、“清空记录”操作按钮组（非流式且有消息时）。
  - 更新对应的 CSS 规则：去除冗余的 50px 顶栏高度，将 `.chat-toolbar` 设为 `min-height: 48px;`，完善各按钮的悬浮与禁用样式。

- [ ] **Step 3: 运行测试验证**
  Run: `npm --prefix frontend test -- tests/chat-toolbar.spec.ts`
  Expected: PASS

- [x] **Step 4: Commit**
  ```bash
  git add frontend/src/views/Chat.vue frontend/tests/chat-toolbar.spec.ts
  git commit -m "feat(ui): unify chat page-header and model-bar into integrated chat-toolbar"
  ```

---

### Task 3: Chat 输入框布局解耦 (绝对定位改为 Flex 纵向底栏)

**Files:**
- Modify: `frontend/src/views/Chat.vue:160-310, 1420-1510, 1660-1760`
- Test: `frontend/tests/chat-input-layout.spec.ts`

**Interfaces:**
- Consumes: 输入框卡片、工作流工具栏、回到底部按钮
- Produces: Flex 自适应底栏，从根本上杜绝绝对定位对消息底部的遮挡

- [x] **Step 1: 编写输入框 Flex 解耦单测**
  在 `frontend/tests/chat-input-layout.spec.ts` 中验证：
  - `.messages` 不再依赖硬编码的 `padding-bottom: 140px`；
  - `.chat-input-area` 位于 Flex 流中，其高度动态变化时不会遮挡 `.messages` 底部的任何消息和操作按钮。

- [x] **Step 2: 改造 `.chat-main` 容器结构与输入框样式**
  - 在 `.chat-main` 内部划分为：
    1. 顶部：`.chat-toolbar` (`flex-shrink: 0`)；
    2. 中间：`.messages-container` (`flex: 1; min-height: 0; position: relative; overflow: hidden;`)；
       - 内含滚动容器 `.messages`，将内边距调整为常规的 `padding: 20px 20px 16px`；
       - 内含 `.scroll-bottom-btn`，定位在滚动容器底部（`bottom: 12px`），永远浮在输入框之上且随滚动容器边界自适应；
    3. 底部：`.chat-input-area` (`flex-shrink: 0`)；
       - 移除 `position: absolute; bottom: 18px; left: 50%; transform: translateX(-50%);`；
       - 改为文档流自然沉底：`width: 100%; max-width: min(1060px, 100cqi); margin: 0 auto; padding: 0 20px 16px;`；
       - 保留 `.chat-input-box` 精致的浮岛圆角（`border-radius: 18px`）、毛玻璃背景与阴影。

- [x] **Step 3: 运行测试验证**
  Run: `npm --prefix frontend test -- tests/chat-input-layout.spec.ts`
  Expected: PASS

- [x] **Step 4: Commit**
  ```bash
  git add frontend/src/views/Chat.vue frontend/tests/chat-input-layout.spec.ts
  git commit -m "feat(ui): decouple chat input area from absolute positioning to flex layout"
  ```

---

### Task 4: 全量回归测试与端到端构建验证

**Files:**
- Test: 全量前端单元测试与生产打包

- [x] **Step 1: 运行前端全量单元测试**
  Run: `npm --prefix frontend test`
  Expected: 所有测试套件全部通过 (100% PASS)。

- [x] **Step 2: 运行前端严格类型检查与 Vite 生产构建**
  Run: `npm --prefix frontend run build`
  Expected: `vue-tsc -b` 0 报错，Vite 编译打包成功。

- [x] **Step 3: 检查工作区状态**
  Run: `git status`
  Expected: 工作区干净无残留。
