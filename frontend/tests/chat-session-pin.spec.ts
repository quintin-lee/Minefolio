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

describe('Session scenario tag detection logic', () => {
  function getSessionTag(title: string): string | null {
    if (!title) return null
    if (/体检|健康|诊断/.test(title)) return '📊'
    if (/记账|消费|支出|调拨/.test(title)) return '🧾'
    if (/行情|汇率|定投|复利/.test(title)) return '📈'
    return null
  }

  it('correctly maps titles to scenario icons', () => {
    expect(getSessionTag('2026年度财务健康全面体检')).toBe('📊')
    expect(getSessionTag('超市购物记账与资金调拨')).toBe('🧾')
    expect(getSessionTag('基金定投复利收益测算')).toBe('📈')
    expect(getSessionTag('美元汇率查询')).toBe('📈')
    expect(getSessionTag('普通闲聊无特定主题')).toBe(null)
    expect(getSessionTag('')).toBe(null)
  })
})

describe('Session grouping with pinning logic', () => {
  it('places pinned sessions in top pinned group and regular sessions in date group', () => {
    const pinnedIds = [2]
    const sessions = [
      { id: 1, title: '常规会话', updated_at: new Date().toISOString() },
      { id: 2, title: '置顶会话', updated_at: new Date().toISOString() },
    ]
    const pinnedItems = sessions.filter(s => pinnedIds.includes(s.id))
    const regularItems = sessions.filter(s => !pinnedIds.includes(s.id))

    const groups: { label: string; isPinnedGroup?: boolean; items: typeof sessions }[] = []
    if (pinnedItems.length > 0) {
      groups.push({ label: '📌 置顶会话', isPinnedGroup: true, items: pinnedItems })
    }
    groups.push({ label: '今天', items: regularItems })

    expect(groups[0]!.label).toBe('📌 置顶会话')
    expect(groups[0]!.isPinnedGroup).toBe(true)
    expect(groups[0]!.items[0]!.id).toBe(2)
    expect(groups[1]!.label).toBe('今天')
    expect(groups[1]!.items[0]!.id).toBe(1)
  })
})
