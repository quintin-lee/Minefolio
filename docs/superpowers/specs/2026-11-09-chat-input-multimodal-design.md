# AI 对话输入流呼吸感与多模态票据交互设计规范

**日期**: 2026-11-09  
**模块**: `frontend/src/views/Chat.vue`, `frontend/src/api/receipts.ts`  
**目标**: 优化 AI 对话页面底部的输入交互体验：实现快捷工作流条在打字时的平滑隐退与呼吸感；提供剪贴板截图粘贴 (`Ctrl+V`)、文件拖拽与附件上传的多模态交互能力，并在发送时调度 OCR 接口完成结构化智能记账闭环。

---

## 1. 现状痛点分析

在现有的 `frontend/src/views/Chat.vue` 中，底部输入区域存在两个痛点：
1. **视觉打扰与空间争抢**：
   - `<WorkflowBar>` 作为固定 36px 药丸横轨常驻在输入框正上方；
   - 用户打字输入多行文字时，工作流条无法自适应避让，产生视觉割裂感与垂直视口浪费。
2. **多模态记账断层**：
   - Minefolio 后端与前端已具备完善的 AI 视觉发票/票据识别能力（`POST /api/receipts/scan` 及 `frontend/src/api/receipts.ts`）；
   - 但主对话输入框仅支持纯文本，不支持直接 `Ctrl+V` 粘贴截图或拖入图片，用户必须跳转到单独的弹窗模态。

---

## 2. 总体架构与交互原型

### 2.1 状态转移

```text
[初始状态: 空闲]
-> inputText === '' && !attachedImage
-> WorkflowBar 正常展开展示推荐胶囊
-> 输入框展示提示: "输入财务咨询，或按 Ctrl+V 粘贴账单/发票截图..."

[打字或附加截图]
-> inputText !== '' || attachedImage !== null
-> WorkflowBar 平滑收起 (max-height: 0, opacity: 0, 0.25s 缓动)
-> 若有截图: 在 textarea 上方展示 .attachment-pill (缩略图 + 文件名 + 待解析标签 + ✕ 移除)

[点击发送]
-> 若带有 attachedImage:
   1. 提取 Base64 Data URL；
   2. 提示扫描中，调用 receiptsApi.scan({ image: dataUrl })；
   3. 解析成功后格式化结构化提示词发送至对话引擎；
   4. 对话引擎识别账目并调起 ActionCard 供用户一键入账；
   5. 成功后重置 attachedImage，输入框恢复。
```

---

## 3. 详细设计规范

### 3.1 渐进式工作流药丸栏（Progressive Workflow Bar）
- 计算属性 `isWorkflowCollapsed`:
  ```ts
  const isWorkflowCollapsed = computed(() => {
    return inputText.value.trim().length > 0 || attachedImage.value !== null
  })
  ```
- 容器动效：
  ```css
  .workflow-bar-wrapper {
    transition: all 0.25s cubic-bezier(0.4, 0, 0.2, 1);
    max-height: 48px;
    opacity: 1;
    overflow: hidden;
  }
  .workflow-bar-wrapper.is-collapsed {
    max-height: 0;
    opacity: 0;
    margin-bottom: 0;
    pointer-events: none;
  }
  ```

### 3.2 剪贴板粘贴与拖拽捕获（Paste & Drag Handling）
- 响应式状态：
  ```ts
  interface AttachedImageInfo {
    file: File
    dataUrl: string
    name: string
    sizeText: string
  }
  const attachedImage = ref<AttachedImageInfo | null>(null)
  const isDragOver = ref(false)
  ```
- 事件处理：
  - `handlePaste(e: ClipboardEvent)`: 遍历 `e.clipboardData.items`，查找图片类型文件，转为 Data URL 并赋值给 `attachedImage`；
  - `handleDrop(e: DragEvent)`: 读取 `e.dataTransfer.files`，处理第一张图片；
  - `handleFileSelect(e: Event)`: 原生 input 文件选择；
  - `removeAttachment()`: 清除 `attachedImage`。

### 3.3 附件微卡片渲染 (`.attachment-pill`)
- 位于 `chat-input-box` 内部顶部：
  - 缩略图（36×36px，圆角 6px）；
  - 文件名（省略号截断）与大小文字；
  - 标签：“AI 视觉识别”；
  - 移除按钮（`Icon icon="ph:x-bold"`）。

### 3.4 底部工具栏新增多模态上传入口
- 在 `input-bottom-left` 中增加：
  ```html
  <label class="input-attach-btn" title="上传或粘贴发票/账单截图 (Ctrl+V)">
    <Icon icon="ph:image-square-bold" class="btn-icon" />
    <span>{{ attachedImage ? '换图' : '传票据' }}</span>
    <input type="file" accept="image/*" @change="handleFileSelect" style="display: none;" />
  </label>
  ```

### 3.5 发送调度与多模态记账联动
- 发送前如果存在 `attachedImage`：
  - 先设置 `isScanningReceipt = true`；
  - 调用 `receiptsApi.scan({ image: attachedImage.value.dataUrl })`；
  - 组装带识别明细的 Prompt；
  - 调用 `chat.sendMessage(finalPrompt)`；
  - 清理附件与扫描状态。

---

## 4. 影响文件与修改范围
- `frontend/src/views/Chat.vue`
- `frontend/tests/chat-input-layout.spec.ts`
