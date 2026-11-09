import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const chatSource = readFileSync(resolve(process.cwd(), 'src/views/Chat.vue'), 'utf8')

describe('Chat unified toolbar layout and context hub', () => {
  it('eliminates the standalone full-width .page-header from Chat.vue', () => {
    // 验证不再有单独占用全宽顶部 50px 的 .page-header
    expect(chatSource).not.toMatch(/<div class="page-header"/)
  })

  it('renders integrated .chat-toolbar with left, center, and right sections', () => {
    // 验证在 .chat-main 顶部整合了三段式 .chat-toolbar
    expect(chatSource).toMatch(/<div class="chat-toolbar"/)
    expect(chatSource).toMatch(/class="toolbar-left"/)
    expect(chatSource).toMatch(/class="toolbar-center"/)
    expect(chatSource).toMatch(/class="toolbar-right"/)
  })

  it('includes new chat action inside sidebar-header and toolbar-right', () => {
    // 验证侧边栏头部与工具栏右侧均包含新对话操作
    expect(chatSource).toMatch(/class="sidebar-header"[\s\S]*?handleNewChat/)
    expect(chatSource).toMatch(/class="toolbar-right"[\s\S]*?handleNewChat/)
  })

  it('removes duplicate toolbar-thinking-btn and current-model-badge from .chat-toolbar', () => {
    // 验证顶栏不再包含重复的思考模式按钮与模型徽标，思考模式保留在输入框
    expect(chatSource).not.toMatch(/class="toolbar-thinking-btn"/)
    expect(chatSource).not.toMatch(/class="current-model-badge"/)
    expect(chatSource).toMatch(/class="input-thinking-btn"/)
  })

  it('renders .toolbar-center with session title and inline rename capabilities', () => {
    // 验证中间区域展示会话标题并支持内联重命名
    expect(chatSource).toMatch(/class="toolbar-center"/)
    expect(chatSource).toMatch(/currentSessionTitle/)
    expect(chatSource).toMatch(/startTopRename/)
    expect(chatSource).toMatch(/saveTopRename/)
  })

  it('renders .session-stat-pill for aggregated session metrics', () => {
    // 验证右侧包含会话级 Token/开销度量胶囊
    expect(chatSource).toMatch(/class="session-stat-pill"/)
    expect(chatSource).toMatch(/sessionMetrics/)
  })

  it('renders el-dropdown in .toolbar-right with export, copy, clear and trace actions', () => {
    // 验证右侧包含更多操作下拉菜单，收纳导出、复制、清空和 trace 跳转
    expect(chatSource).toMatch(/class="toolbar-right"[\s\S]*?<el-dropdown/)
    expect(chatSource).toMatch(/exportToMarkdown/)
    expect(chatSource).toMatch(/copyAllChat/)
    expect(chatSource).toMatch(/handleClearChat/)
    expect(chatSource).toMatch(/\/ai-traces/)
  })
})
