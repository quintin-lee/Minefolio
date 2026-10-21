import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const chatSource = readFileSync(resolve(process.cwd(), 'src/views/Chat.vue'), 'utf8')

describe('Chat unified toolbar layout', () => {
  it('eliminates the standalone full-width .page-header from Chat.vue', () => {
    // 验证不再有单独占用全宽顶部 50px 的 .page-header
    expect(chatSource).not.toMatch(/<div class="page-header"/)
  })

  it('renders integrated .chat-toolbar inside .chat-main', () => {
    // 验证在 .chat-main 顶部整合了 .chat-toolbar
    expect(chatSource).toMatch(/<div class="chat-toolbar"/)
    expect(chatSource).toMatch(/class="toolbar-left"/)
    expect(chatSource).toMatch(/class="toolbar-right"/)
  })

  it('includes new chat action inside sidebar-header', () => {
    // 验证侧边栏头部包含新对话按钮
    expect(chatSource).toMatch(/class="sidebar-header"[\s\S]*?handleNewChat/)
  })

  it('includes export, copy and clear actions in .chat-toolbar', () => {
    // 验证工具栏右侧集成了导出、复制和清空操作
    expect(chatSource).toMatch(/class="toolbar-right"[\s\S]*?exportToMarkdown/)
    expect(chatSource).toMatch(/class="toolbar-right"[\s\S]*?copyAllChat/)
    expect(chatSource).toMatch(/class="toolbar-right"[\s\S]*?handleClearChat/)
  })
})
