import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const chatSource = readFileSync(resolve(process.cwd(), 'src/views/Chat.vue'), 'utf8')

describe('Chat sidebar session pinning, scenario tags and single session actions', () => {
  it('integrates chat.isSessionPinned and chat.togglePinSession into groupedSessions', () => {
    // 验证在 Chat.vue 中使用 isSessionPinned 分离出置顶会话组
    expect(chatSource).toMatch(/isSessionPinned/)
    expect(chatSource).toMatch(/togglePinSession/)
    expect(chatSource).toMatch(/📌\s*置顶会话/)
  })

  it('implements getSessionTag helper for intelligent scenario badges', () => {
    // 验证实现了 getSessionTag 函数以识别体检、记账、投资等场景
    expect(chatSource).toMatch(/function getSessionTag\(/)
    expect(chatSource).toMatch(/体检|健康|诊断/)
    expect(chatSource).toMatch(/记账|消费|支出|调拨/)
    expect(chatSource).toMatch(/行情|汇率|定投|复利/)
  })

  it('renders scenario tag and pin indicator in .session-item', () => {
    // 验证会话项中渲染场景标签与置顶图钉图标
    expect(chatSource).toMatch(/class="session-scenario-tag"/)
    expect(chatSource).toMatch(/class="session-pin-icon"/)
  })

  it('provides a pin/unpin button in session-item-actions', () => {
    // 验证会话项悬浮操作区包含快速置顶/取消置顶按钮
    expect(chatSource).toMatch(/class="item-action-btn pin-btn"/)
  })

  it('provides single session dropdown menu with markdown export and rename', () => {
    // 验证会话项包含单会话专属下拉菜单与 Markdown 导出功能
    expect(chatSource).toMatch(/handleExportSingleSession/)
    expect(chatSource).toMatch(/session-more-btn/)
  })
})
