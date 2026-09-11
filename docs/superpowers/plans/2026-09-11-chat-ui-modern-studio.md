# Chat UI Modern Studio 实施计划 (Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 Minefolio AI 对话页面全面重塑为现代化 AI Studio 风格，提供居中舒适阅读视窗、悬浮胶囊输入舱、非对称高质感气泡、精致流式动效及高辨识度金融富文本。

**Architecture:** 保持现有 `useChatStore`、`sessionSwitchSeq` 竞态保护与 `SmoothStreamWriter` 流式打字核心逻辑不变；重构 `Chat.vue` 的视窗布局与悬浮输入岛定位；打磨 `ChatMessageContent.vue`、`PromptStarters.vue`、`ActionCard.vue` 与 `WorkflowBar.vue` 的现代卡片与微交互样式，全面适配深浅色主题与移动端响应式。

**Tech Stack:** Vue 3 (Composition API / `<script setup>`), TypeScript, Pinia, Element Plus, Iconify Icons, CSS3 (Bento glassmorphism, flex/grid, custom animations).

---

### Task 1: 升级空状态 Bento 启程卡片 (`PromptStarters.vue`)

**Files:**
- Modify: `frontend/src/components/PromptStarters.vue`

- [ ] **Step 1: 优化启程卡片结构与现代轻奢样式**

在 `frontend/src/components/PromptStarters.vue` 中升级 Bento 网格布局、渐变发光图标、毛玻璃卡片、微上浮 Hover 动效与清晰的金融指引文案：
- 增加卡片柔和投影与边框过渡（`transition: transform 0.2s, border-color 0.2s, box-shadow 0.2s;`）；
- 悬浮时增加轻微上浮（`transform: translateY(-3px);`）与外发光（`box-shadow: var(--mf-shadow-md), 0 0 16px rgba(0, 212, 255, 0.08);`）；
- 完善深浅色主题下的背景色与文字对比度。

- [ ] **Step 2: 验证组件构建与单元测试**

运行: `npm --prefix frontend test`
预期: 全部 45 个测试通过。

- [ ] **Step 3: 提交更改**

```bash
git add frontend/src/components/PromptStarters.vue
git commit -m "feat(chat): upgrade empty state prompt starters with bento cards and hover lift"
```

---

### Task 2: 优化金融富文本、数据表格与卡片组件 (`ChatMessageContent.vue`, `ActionCard.vue`)

**Files:**
- Modify: `frontend/src/components/ChatMessageContent.vue`
- Modify: `frontend/src/components/ActionCard.vue`

- [ ] **Step 1: 优化 Markdown 表格、引用块与代码块样式**

在 `frontend/src/components/ChatMessageContent.vue` 的 `<style scoped>` 中：
- 表格增加圆角外框（`border-radius: 8px; overflow: hidden;`）、表头背景色（`var(--mf-surface-muted)`）及斑马纹（`tr:nth-child(even)`）；
- 针对数字和金额列提供等宽字体（`var(--mf-font-mono)`）与右对齐；
- 引用块（`blockquote`）采用左侧 `3px` 主色强调条、圆角 `0 8px 8px 0` 及柔和背景底色；
- 优化流式打字光标为微圆角胶囊竖条，呼吸式平滑闪烁；
- Mermaid 等待生成状态的骨架脉冲卡片精致化。

- [ ] **Step 2: 升级自然语言记账确认卡片 (`ActionCard.vue`)**

在 `frontend/src/components/ActionCard.vue` 中：
- 金额数字加粗放大，支出显示红色高亮，收入显示绿色高亮；
- 分类、账户与时间标签升级为圆角药丸徽章（Pill badges）；
- 操作按钮增加微渐变背景与按下反馈。

- [ ] **Step 3: 验证构建与单元测试**

运行: `npm --prefix frontend test`
预期: 全部测试通过。

- [ ] **Step 4: 提交更改**

```bash
git add frontend/src/components/ChatMessageContent.vue frontend/src/components/ActionCard.vue
git commit -m "feat(chat): polish financial markdown tables, callouts and action card typography"
```

---

### Task 3: 打磨顶栏导航与侧边栏交互 (`Chat.vue`)

**Files:**
- Modify: `frontend/src/views/Chat.vue`

- [ ] **Step 1: 改造顶部磨砂导航栏 (Header) 与模型选择栏 (Model Bar)**

- 顶栏收窄为精致的 `52px`，磨砂毛玻璃透光（`backdrop-filter: blur(16px)`）；
- 操作按钮统一调整为轻量幽灵按钮（Ghost button）样式，Hover 带微光底色与平滑过渡；
- 模型切换下拉框去掉厚重外框，小巧圆润，并增加当前运行状态指示微绿点。

- [ ] **Step 2: 优化会话侧边栏平滑折叠与会话项状态**

- 侧边栏折叠增加非线性贝塞尔曲线（`transition: width 0.25s cubic-bezier(0.4, 0, 0.2, 1), min-width 0.25s cubic-bezier(0.4, 0, 0.2, 1);`）；
- 会话分组标签（今天、昨天、近 7 天等）采用精致小药丸微标签；
- Active 选中态增加左侧 `3px` 品牌色高光条，背景采用微渐变高光；
- 重命名/删除按钮在 Hover 时平滑淡入，原地重命名输入框自适应聚焦。

- [ ] **Step 3: 验证构建与单元测试**

运行: `npm --prefix frontend test`
预期: 全部测试通过。

- [ ] **Step 4: 提交更改**

```bash
git add frontend/src/views/Chat.vue
git commit -m "feat(chat): streamline header bar and polish collapsible session sidebar"
```

---

### Task 4: 实现悬浮胶囊输入舱与工作流联动 (`Chat.vue`, `WorkflowBar.vue`)

**Files:**
- Modify: `frontend/src/views/Chat.vue`
- Modify: `frontend/src/components/WorkflowBar.vue`

- [ ] **Step 1: 升级工作流胶囊条 (`WorkflowBar.vue`)**

- 胶囊芯片升级为半透明微渐变胶囊（Chip），支持横向平滑滚动；
- Hover 触发微上浮与边框微光；
- 紧密吸附在输入岛正上方。

- [ ] **Step 2: 实现居中悬浮输入岛 (`Chat.vue`)**

- 将传统的底端固定通栏输入条改造为**居中悬浮胶囊输入岛**：
  ```css
  .chat-input-area {
    position: absolute;
    bottom: 20px;
    left: 50%;
    transform: translateX(-50%);
    width: 100%;
    max-width: 860px;
    padding: 0 20px;
    pointer-events: none;
    z-index: 10;
  }
  .chat-input-box {
    pointer-events: auto;
    border-radius: 20px;
    background: var(--mf-surface);
    backdrop-filter: blur(20px);
    border: 1px solid var(--mf-border);
    box-shadow: 0 8px 32px rgba(0, 0, 0, 0.12), 0 2px 8px rgba(0, 0, 0, 0.06);
    transition: border-color 0.2s, box-shadow 0.2s;
  }
  .chat-input-box:focus-within {
    border-color: var(--mf-primary);
    box-shadow: 0 8px 32px rgba(0, 0, 0, 0.16), 0 0 0 2px var(--mf-primary-light);
  }
  ```
- 调整 `WorkflowSlashMenu` 的浮出层定位；
- 优化字数统计与快捷键提示（`↵ 发送 · ⇧↵ 换行`）；
- 升级发送按钮微交互（平常态为渐变发送胶囊，生成中为带红晕呼吸脉冲的“停止生成”按钮）；
- “回到底部”悬浮按钮定位在输入岛正上方并带弹性平滑滚动。

- [ ] **Step 3: 验证构建与单元测试**

运行: `npm --prefix frontend test`
预期: 全部测试通过。

- [ ] **Step 4: 提交更改**

```bash
git add frontend/src/views/Chat.vue frontend/src/components/WorkflowBar.vue
git commit -m "feat(chat): implement floating island input capsule with integrated workflows"
```

---

### Task 5: 优化居中消息流、角色气泡与流式动效 (`Chat.vue`)

**Files:**
- Modify: `frontend/src/views/Chat.vue`

- [ ] **Step 1: 重构居中视窗与消息气泡排版**

- 消息流容器限制最大宽度为 `860px` 并居中：
  ```css
  .messages {
    max-width: 860px;
    margin: 0 auto;
    width: 100%;
    padding: 24px 20px 140px;
  }
  ```
- 用户提问气泡：采用品牌主色微渐变（`linear-gradient(135deg, var(--mf-primary), #6366f1)`），大圆角 `18px`（右下角收为 `4px`），悬浮操作栏平滑淡入；
- AI 助手消息卡片：通透表面卡片，圆角 `18px`（左下角收为 `4px`），自适应舒展；
- 机器人头像：流式生成中激活环形脉冲呼吸光环（Breathing Pulse Ring）；
- 深度思考中（Thinking State）：首字到达前展示微发光 Shimmer 卡片与 3 个波浪微跳动点；
- 回复完成操作栏：淡入“复制回答”、“重新生成”、“重试”小按钮，附带即时成功反馈。

- [ ] **Step 2: 适配深浅色模式与移动端响应式 (< 768px)**

- 保证深浅双主题下的文字可读性与边框发光；
- 移动端下输入岛贴底避让安全区（`env(safe-area-inset-bottom)`），侧边栏支持抽屉式滑出与遮罩。

- [ ] **Step 3: 验证构建与单元测试**

运行: `npm --prefix frontend test`
预期: 全部测试通过。

- [ ] **Step 4: 提交更改**

```bash
git add frontend/src/views/Chat.vue
git commit -m "feat(chat): modernize centered message stream, avatars and streaming micro-interactions"
```

---

### Task 6: 完整构建与端到端视觉验证

**Files:**
- Test: 全局构建与类型检查

- [ ] **Step 1: 运行严格前端构建与类型检查**

运行: `npm --prefix frontend run build`
预期: `vue-tsc -b` 0 错误，Vite 构建成功生成 `dist/`。

- [ ] **Step 2: 运行前端自动化测试套件**

运行: `npm --prefix frontend test`
预期: 11 个测试文件 45 个测试用例全部通过。

- [ ] **Step 3: 整理提交与回顾**

```bash
git status
```
确认工作区干净，所有改动均已提交。
