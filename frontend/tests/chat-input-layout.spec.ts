import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const chatSource = readFileSync(resolve(process.cwd(), 'src/views/Chat.vue'), 'utf8')

describe('Chat input area flex layout decoupling', () => {
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
})
