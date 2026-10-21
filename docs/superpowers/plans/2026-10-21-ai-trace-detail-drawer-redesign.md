# AI Trace 详情页布局重构实施计划 (Trace Detail Drawer Redesign Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `AiTraceDetail.vue` 从局促的 800px 居中弹窗全面重构为现代化、工业级 AI 追踪右侧抽屉（Slide-over Drawer），提供 Bento 性能/成本指标条、输入输出左右分栏独立滚动、Markdown 富文本/原始文本双模渲染及一键复制功能。

**Architecture:** 
- 表现层容器由 `<el-dialog>` 迁移至 `<el-drawer>`，支持全屏展开与自适应断点；
- 顶部集成 Bento 统计横条（延迟/TTFT/Token细分/tok/s/花费/参数）；
- 核心工作区采用 Element Plus Tabs (`el-tabs`) 承载三大视图：对话与输出（宽屏下输入与输出左右并列分栏、窄屏流式堆叠）、原始报文（格式化高亮 JSON）与运行参数；
- 模型回复区支持 `marked` + `dompurify` 富文本渲染与纯文本切换。

**Tech Stack:** Vue 3 (Composition API, `<script setup>`), TypeScript, Element Plus (`el-drawer`, `el-tabs`, `el-tag`, `el-button`), marked, dompurify, Iconify.

**Spec:** `docs/superpowers/specs/2026-10-21-ai-trace-detail-drawer-redesign.md`

## Global Constraints

- **前端独立改造**：零后端 C 语言或 API 协议变更，完全复用 `/api/ai/traces/:id` 接口及 `AiTraceDetail` 接口定义；
- **组件规范**：必须遵循 Minefolio `--mf-*` 设计变量体系，完美适配 Light / Dark 暗色科技感主题；
- **全端构建零容错**：`vue-tsc -b && vite build` 生产构建必须 0 错误通过；全量测试 100% PASS。

## Review Focus

1. **抽屉打开与全屏切换稳定性**：验证 `visible` 双向绑定、`isMaximized` 状态切换时抽屉尺寸平滑变化；
2. **输入消息 JSON 容错解析**：当 `input_messages` 存在非法 JSON 或空值时，优雅降级展示原始文本或系统提示词，不发生运行时异常；
3. **输出模式平滑切换**：Markdown 渲染与原始文本切换时内容完整一致，表格、段落与代码块排版美观；
4. **一键复制交互反馈**：点击复制单条消息、输入全量、输出全量或原始 JSON 时，触发 `ElMessage.success` 友好提示；
5. **滚动条嵌套隔离**：输入面板与输出面板在独立分栏中拥有独立的滚动容器，抽屉外层不产生多层冲突滚动条。

---

### Task 1: 编写 Trace 详情抽屉单测用例

**Files:**
- Create: `frontend/tests/ai-trace-detail.spec.ts`

**Interfaces:**
- Consumes: `AiTraceDetail.vue` 组件、`AiTraceDetail` 类型定义
- Produces: 验证抽屉容器元素、Bento 性能指标渲染、双栏工作区结构与复制操作的单元测试

- [ ] **Step 1: 编写单测用例**
  创建 `frontend/tests/ai-trace-detail.spec.ts`：
  - 测试 `AiTraceDetail.vue` 源码结构包含 `<el-drawer`，且废弃 `<el-dialog`；
  - 测试包含 Bento 性能横条（延迟、Tokens 细分、Cost USD 等）；
  - 测试包含左右分栏容器结构（`prompt-pane` 与 `output-pane`）；
  - 测试包含 Markdown / 原始代码切换选项；
  - 测试包含原始 JSON 报文展示选项。

- [ ] **Step 2: 运行测试并确认失败**
  Run: `npm --prefix frontend test -- tests/ai-trace-detail.spec.ts`
  Expected: FAIL（由于当前 `AiTraceDetail.vue` 仍为旧版 `el-dialog`）

- [ ] **Step 3: Commit**
  ```bash
  git add frontend/tests/ai-trace-detail.spec.ts
  git commit -m "test(trace): add unit tests for modern ai trace detail drawer layout"
  ```

---

### Task 2: 重构 `AiTraceDetail.vue` 为现代化右侧滑动工作台

**Files:**
- Modify: `frontend/src/views/AiTraceDetail.vue`

**Interfaces:**
- Props: `modelValue: boolean`, `traceId: number`
- Emits: `update:modelValue: [boolean]`

- [ ] **Step 1: 重构 Template 模板**
  - 使用 `<el-drawer v-model="visible" direction="rtl" :size="drawerSize" :with-header="false" destroy-on-close class="ai-trace-drawer">`；
  - 自定义 Header 区域：
    - 左侧：Trace 编号 (`#ID`)、供应商 (`provider`)、模型名称 (`model`)、状态 Badge (`ok` 成功 / `error` 失败)；
    - 右侧工具栏：全屏切换按钮（`<Icon :icon="isMaximized ? 'ph:corners-in-bold' : 'ph:corners-out-bold'" />`）、复制完整 JSON 按钮、关闭按钮；
  - 顶部 Bento 性能与成本指标带 (`.trace-metrics-bar`)：
    - 延迟指标：总耗时 (`latency_ms`) + 首 Token (`first_token_ms`)；
    - Token 分解：`total_tokens` (含 `prompt_tokens` / `completion_tokens` 标签)；
    - 速率与成本：`tokens_per_sec` (tok/s) + 预估费用 (`cost_usd` 格式化为 `$0.0000`)；
    - 参数标签：`temperature` + `top_p`；
  - 标签页工作区 (`el-tabs`):
    - **Tab 1: 对话与输出 (`messages`)**：
      - 宽屏双栏布局 (`.workspace-split`)：
        - 左栏（输入上下文 `.prompt-pane`）：
          - 顶部标题与「全部复制」按钮；
          - 消息卡片流：System (紫)、User (蓝)、Assistant (绿)、Tool (琥珀) 独立卡片，带角色标识与单条复制按钮；
        - 右栏（模型回复 `.output-pane`）：
          - 顶部标题、视图切换控件（Markdown / 原始纯文本）与「复制回复」按钮；
          - 错误信息横幅（若 `error_message` 存在，显示红底警告框）；
          - 内容区：Markdown 视图（`v-html="renderedOutput"`）或原始文本区块（`<pre class="raw-output">`）；
    - **Tab 2: 原始报文 (`raw`)**：
      - 格式化展示 `detail` 完整 JSON 结构，支持高亮与一键复制；
    - **Tab 3: 运行参数与元数据 (`params`)**：
      - 详细 Key-Value 属性表展示 `session_id`, `created_at`, `max_tokens`, `metadata` 等。

- [ ] **Step 2: 完善 Script 逻辑**
  - 引入 `marked` 与 `DOMPurify` 实现 `renderedOutput` 安全解析；
  - 实现 `isMaximized` 响应式切换，动态计算 `drawerSize` (`isMaximized ? '100%' : '75%'`，移动端 100%)；
  - 实现复制辅助函数 `copyText(text, successMsg)`，配合 `ElMessage.success`；
  - 实现 `formattedCost(val)`、`formatDateTime(s)`、`formatMs(ms)` 等健壮格式化工具。

- [ ] **Step 3: 编写精细化 CSS 样式**
  - 基于 `--mf-*` 主题变量定制抽屉背景、边框、卡片毛玻璃质感与滚动条；
  - `.workspace-split` 采用 Grid 或 Flex 双栏布局，在大屏下高度撑满剩余视口（`flex: 1; min-height: 0;`），两栏内部独立滚动；
  - `@media (max-width: 992px)` 自动退化为单列流式堆叠布局。

- [ ] **Step 4: 运行单测验证**
  Run: `npm --prefix frontend test -- tests/ai-trace-detail.spec.ts`
  Expected: PASS

- [ ] **Step 5: Commit**
  ```bash
  git add frontend/src/views/AiTraceDetail.vue
  git commit -m "feat(trace): redesign ai trace detail with slide-over drawer, bento metrics, and split workspace"
  ```

---

### Task 3: 全量构建与端到端回归验证

**Files:**
- Test: 全量前端单元测试与生产打包

- [ ] **Step 1: 运行前端全量单元测试**
  Run: `npm --prefix frontend test`
  Expected: 33 个测试套件全部通过 (100% PASS)。

- [ ] **Step 2: 运行前端类型检查与 Vite 生产打包**
  Run: `npm --prefix frontend run build`
  Expected: `vue-tsc -b` 0 报错，Vite 编译成功。

- [ ] **Step 3: 检查工作区状态**
  Run: `git status`
  Expected: 工作区干净无残留。
