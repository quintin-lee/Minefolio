# AI 对话输入流呼吸感与多模态票据交互实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 重构 `Chat.vue` 输入区域，实现快捷工作流条在打字时的渐进式平滑折叠，并支持剪贴板截图粘贴 (`Ctrl+V`)、拖拽与上传票据图片的多模态附件微卡片预览，并在发送时自动联动 AI 视觉 OCR 接口形成智能记账闭环。

**Architecture:** 
1. 在 `Chat.vue` 输入区外层为 `<WorkflowBar />` 增加平滑折叠包装器，通过 `isWorkflowCollapsed` 计算属性在输入内容或附带图片时自动收起，还原打字呼吸感。
2. 在 `chat-input-box` 中捕获 `paste`、`drop` 与文件选择事件，将发票截图解析为 Data URL 并以 `.attachment-pill` 微卡片形式预览展示，支持单键移除。
3. 在 `handleSend` 调度流程中集成 `receiptsApi.scan`，将解析出的消费日期、金额、商户、分类无缝组装至 Prompt 发起对话，联动 `propose_daily_expense` 与 `ActionCard` 实现一键入账。

**Tech Stack:** Vue 3, TypeScript, Pinia, Element Plus, Iconify, Vitest, receiptsApi (`POST /api/receipts/scan`).

**Spec:** [`docs/superpowers/specs/2026-11-09-chat-input-multimodal-design.md`](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-11-09-chat-input-multimodal-design.md)

## Global Constraints

- **保留全部既有功能**：`/` 快捷工作流菜单、Enter 发送、Shift+Enter 换行、深度思考模式切换必须完整保留。
- **内存安全**：图片 Base64 仅作为内存临时引用，发送或移除后立即重置，不留悬挂引用。
- **构建零错误**：`npm --prefix frontend run build`（vue-tsc -b && vite build）必须 0 错误。
- **测试通过率 100%**：`npm --prefix frontend test -- --run` 全量测试通过。

## Review Focus

1. **工作流条折叠与展开的平滑性**：打字时收起不应引起光标失焦或高度抖动；清空文字后应平滑复位。
2. **非图片剪贴板内容的正常放行**：用户在输入框粘贴普通文本时，应正常进行文本粘贴，不误触发图片附件逻辑。
3. **过大图片或非标准格式的防御校验**：支持主流图片类型（jpg, png, webp），限制单张图片最大体积（如 10MB），避免超大 Base64 引起页面卡顿。
4. **票据 OCR 失败时的优雅降级**：若网络超时或识别无果，不中断对话发送，而是作为普通提示继续向模型求助。
5. **移动端触控兼容**：移动端上传按钮可正常调起相机/图库。

---

### Task 1: 更新输入区布局与多模态契约测试 (Test Contract)

**Files:**
- Modify: `frontend/tests/chat-input-layout.spec.ts`

**Interfaces:**
- Consumes: `frontend/src/views/Chat.vue` 源码
- Produces: 针对渐进式工作流折叠、附件微卡片预览、粘贴与拖拽捕获、多模态上传入口的测试用例

- [ ] **Step 1: 编写失败的测试用例**

在 `frontend/tests/chat-input-layout.spec.ts` 中新增以下断言：
1. 验证 `WorkflowBar` 被包裹在具备 `isWorkflowCollapsed` 响应式类的容器中；
2. 验证 `chat-input-box` 支持 `handlePaste` 与 `handleDrop`，并绑定 `is-dragover` 样式；
3. 验证输入框内渲染 `.attachment-pill` 附件预览微卡片结构；
4. 验证输入框底部操作区包含 `.input-attach-btn` 上传票据按钮；
5. 验证引入了 `receiptsApi` 并在发送阶段支持多模态识别调用。

- [ ] **Step 2: 运行测试以确认其正确失败**

运行: `npm --prefix frontend test -- tests/chat-input-layout.spec.ts --run`  
预期: FAIL（当前 `Chat.vue` 尚无 `isWorkflowCollapsed`、`handlePaste` 及 `attachment-pill`）

- [ ] **Step 3: 提交测试改动**

```bash
git add frontend/tests/chat-input-layout.spec.ts
git commit -m "test(chat): 🧪 update chat input test contract for progressive workflow and multimodal attachments"
```

---

### Task 2: 实现渐进式工作流折叠与图片粘贴/拖拽捕获

**Files:**
- Modify: `frontend/src/views/Chat.vue`

**Interfaces:**
- Consumes: `inputText`, `chat.isStreaming`
- Produces:
  - `attachedImage: Ref<AttachedImageInfo | null>`
  - `isWorkflowCollapsed: ComputedRef<boolean>`
  - `isDragOver: Ref<boolean>`
  - `handlePaste(e: ClipboardEvent): void`
  - `handleDrop(e: DragEvent): void`
  - `handleFileSelect(e: Event): void`
  - `removeAttachment(): void`
  - 模板中的 `.workflow-bar-wrapper`、`.attachment-pill` 与 `.input-attach-btn`

- [ ] **Step 1: 在 `Chat.vue` 脚本中引入 receiptsApi 并定义附件状态与交互函数**

1. 引入 `import { receiptsApi } from '@/api/receipts'`；
2. 定义 `AttachedImageInfo` 接口，定义 `attachedImage` 与 `isDragOver`；
3. 定义 `isWorkflowCollapsed = computed(() => !!inputText.value.trim() || !!attachedImage.value)`；
4. 实现 `processImageFile(file: File)`：检查文件类型是否为 `image/`，限制体积 <= 10MB，转为 Data URL 并填充 `attachedImage`，同时弹出成功提示；
5. 实现 `handlePaste(e: ClipboardEvent)`：提取剪贴板中的图片并调用 `processImageFile`；
6. 实现 `handleDrop(e: DragEvent)`：提取拖拽文件并调用 `processImageFile`；
7. 实现 `handleFileSelect(e: Event)` 与 `removeAttachment()`。

- [ ] **Step 2: 在 `Chat.vue` 模板中绑定渐进式容器与附件卡片**

1. 将 `<WorkflowBar />` 用 `<div class="workflow-bar-wrapper" :class="{ 'is-collapsed': isWorkflowCollapsed }"><WorkflowBar /></div>` 包裹；
2. 在 `chat-input-box` 上监听 `@paste="handlePaste"`、`@dragover.prevent="isDragOver = true"`、`@dragleave.prevent="isDragOver = false"`、`@drop.prevent="handleDrop"`；
3. 在 `textarea` 前方增加 `<div v-if="attachedImage" class="attachment-pill">...</div>`；
4. 在 `input-bottom-left` 增加 `<label class="input-attach-btn">...</label>`。

- [ ] **Step 3: 运行测试验证 Task 2**

运行: `npm --prefix frontend test -- tests/chat-input-layout.spec.ts --run`  
预期: 前 4 个新增断言 PASS。

- [ ] **Step 4: 提交 Task 2 代码**

```bash
git add frontend/src/views/Chat.vue
git commit -m "feat(chat): ⚡ implement progressive workflow disclosure and image paste attachments"
```

---

### Task 3: 联动发票 OCR 解析与多模态记账发送闭环

**Files:**
- Modify: `frontend/src/views/Chat.vue`

**Interfaces:**
- Consumes: `receiptsApi.scan`, `attachedImage`, `inputText`, `chat.sendMessage`
- Produces:
  - 具备多模态拦截与 OCR 格式化逻辑的 `handleSend()`
  - 扫描中状态提示（`isScanningReceipt`）

- [ ] **Step 1: 改造 `handleSend` 函数支持多模态记账流水线**

在 `handleSend()` 中：
1. 检查是否存在 `attachedImage.value`；
2. 若存在：
   - 设置扫描中状态，捕获当前图片 Data URL 与文件名；
   - 调用 `receiptsApi.scan({ image: dataUrl })`；
   - 若识别成功，将商户、金额、日期、分类等结构化参数拼接至发送提示词（如：`【消费小票识别】商户: ${res.counterparty} | 金额: ¥${res.amount} | 日期: ${res.date} | 推荐分类: ${res.category_name}。请核实并协助入账。`）；
   - 若识别失败，优雅降级：提示“小票识别超时，已直接发送图片求助”并发送带图片占位符的普通提示词；
   - 发送完成后重置 `attachedImage.value = null`。

- [ ] **Step 2: 补充附件卡片与拖拽高光 CSS 样式**

编写 `.workflow-bar-wrapper` 折叠动效、`.is-dragover` 边框发光、`.attachment-pill` 缩略图卡片及 `.input-attach-btn` 样式规则。

- [ ] **Step 3: 运行全量测试验证**

运行: `npm --prefix frontend test -- tests/chat-input-layout.spec.ts --run`  
预期: PASS。

- [ ] **Step 4: 提交 Task 3 代码**

```bash
git add frontend/src/views/Chat.vue
git commit -m "feat(chat): ⚡ link multimodal receipt scan with smart expense proposal pipeline"
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
