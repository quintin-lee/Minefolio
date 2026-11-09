import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const chatSource = readFileSync(resolve(process.cwd(), 'src/views/Chat.vue'), 'utf8')

describe('Chat input area progressive disclosure and multimodal layout', () => {
  it('wraps messages in .messages-container separate from .chat-input-area', () => {
    // 验证 .chat-main 内有独立的 .messages-container
    expect(chatSource).toMatch(/<div class="messages-container"/)
    expect(chatSource).toMatch(/<div class="chat-input-area"/)
  })

  it('decouples .chat-input-area from absolute positioning in CSS', () => {
    // 验证 .chat-input-area 不再使用 position: absolute
    expect(chatSource).not.toMatch(/\.chat-input-area\s*{[^}]*position:\s*absolute/s)
  })

  it('removes hardcoded 140px bottom padding from .messages', () => {
    // 验证 .messages 不再依赖硬编码的 140px 底部内边距
    expect(chatSource).not.toMatch(/\.messages\s*{[^}]*140px/s)
  })

  it('places .scroll-bottom-btn inside .messages-container above the input area', () => {
    // 验证回到底部按钮被放置在 .messages-container 内部，浮动在输入框上方
    expect(chatSource).toMatch(/<div class="messages-container"[\s\S]*?<button[\s\S]*?class="scroll-bottom-btn"[\s\S]*?<\/div>\s*<!-- Input Area -->/s)
  })

  it('wraps WorkflowBar in progressive disclosure container with isWorkflowCollapsed', () => {
    // 验证工作流条包裹在具有 isWorkflowCollapsed 折叠状态的容器中
    expect(chatSource).toMatch(/class="workflow-bar-wrapper"[\s\S]*?isWorkflowCollapsed[\s\S]*?<WorkflowBar/)
  })

  it('supports clipboard paste and drag-and-drop on .chat-input-box', () => {
    // 验证输入框支持 paste 截图与 drop 拖拽，并绑定 is-dragover
    expect(chatSource).toMatch(/class="chat-input-box"[\s\S]*?@paste="handlePaste"/)
    expect(chatSource).toMatch(/@drop\.prevent="handleDrop"/)
    expect(chatSource).toMatch(/is-dragover/)
  })

  it('renders .attachment-pill preview capsule when attachedImage is present', () => {
    // 验证存在附件缩略图微卡片，并提供移除方法
    expect(chatSource).toMatch(/v-if="attachedImage"[\s\S]*?class="attachment-pill"/)
    expect(chatSource).toMatch(/removeAttachment/)
  })

  it('includes .input-attach-btn in bottom bar for receipt upload', () => {
    // 验证输入框底部操作区包含票据上传按钮及处理函数
    expect(chatSource).toMatch(/class="input-attach-btn"/)
    expect(chatSource).toMatch(/handleFileSelect/)
  })

  it('integrates receiptsApi for multimodal invoice OCR scan pipeline', () => {
    // 验证引入并集成了 receiptsApi.scan OCR 识别调用
    expect(chatSource).toMatch(/receiptsApi\.scan/)
  })
})
