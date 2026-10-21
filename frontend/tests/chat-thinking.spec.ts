import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { useChatStore } from '@/stores/chat'

vi.mock('@/api/ai', () => ({
  listSessions: vi.fn().mockResolvedValue({ list: [] }),
  createSession: vi.fn().mockResolvedValue({ data: { id: 1, title: '新会话' } }),
  deleteSession: vi.fn(),
  getSession: vi.fn().mockResolvedValue({ data: { id: 1, title: '测试会话' } }),
  updateSession: vi.fn(),
  getMessages: vi.fn(),
  chatStream: vi.fn(),
  getModels: vi.fn(),
  getSettings: vi.fn(),
  getWorkflows: vi.fn(),
  runWorkflowStream: vi.fn(),
}))

import { chatStream, getMessages } from '@/api/ai'

describe('chat store thinking mode support', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    localStorage.clear()
    vi.clearAllMocks()
  })

  it('accumulates thinking_content when receiving thinking stream chunks', async () => {
    // 模拟流式生成器：先返回思考分块，再返回正文分块
    async function* mockThinkingStream() {
      yield { type: 'thinking' as const, content: '正在分析资产状况...' }
      yield { type: 'thinking' as const, content: '计算负债比例...' }
      yield { type: 'delta' as const, content: '您的资产健康度良好。' }
    }

    ;(chatStream as any).mockReturnValue(mockThinkingStream())

    const store = useChatStore()
    store.currentSessionId = 1

    await store.sendMessage('帮我分析资产')

    const assistantMsg = store.messages.find(m => m.role === 'assistant')
    expect(assistantMsg).toBeDefined()
    expect(assistantMsg?.thinking_content).toBe('正在分析资产状况...计算负债比例...')
    expect(assistantMsg?.content).toContain('您的资产健康度良好。')
  })

  it('preserves thinking_content when loading historical messages', async () => {
    ;(getMessages as any).mockResolvedValue({
      list: [
        {
          id: 101,
          session_id: 1,
          role: 'assistant',
          content: '这是最终回答',
          thinking_content: '这是历史思考过程',
          created_at: '2026-10-21T10:00:00Z',
        },
      ],
      total: 1,
    })

    const store = useChatStore()
    store.currentSessionId = 1
    await store.loadMoreMessages()

    expect(store.messages.length).toBe(1)
    expect(store.messages[0]?.thinking_content).toBe('这是历史思考过程')
    expect(store.messages[0]?.content).toBe('这是最终回答')
  })

  it('toggles enableThinking and persists preference to localStorage', () => {
    const store = useChatStore()
    expect(store.enableThinking).toBe(true)

    store.toggleThinking()
    expect(store.enableThinking).toBe(false)
    expect(localStorage.getItem('minefolio_chat_enable_thinking')).toBe('false')

    store.toggleThinking()
    expect(store.enableThinking).toBe(true)
    expect(localStorage.getItem('minefolio_chat_enable_thinking')).toBe('true')
  })

  it('passes enable_thinking boolean to chatStream on sendMessage', async () => {
    async function* mockStream() {
      yield { type: 'delta' as const, content: '回答' }
    }
    ;(chatStream as any).mockReturnValue(mockStream())

    const store = useChatStore()
    store.currentSessionId = 1
    store.enableThinking = true

    await store.sendMessage('测试问题')

    expect(chatStream).toHaveBeenCalledWith(
      expect.objectContaining({
        content: '测试问题',
        enable_thinking: true,
      }),
      expect.anything()
    )

    store.enableThinking = false
    await store.sendMessage('再次测试')

    expect(chatStream).toHaveBeenCalledWith(
      expect.objectContaining({
        content: '再次测试',
        enable_thinking: false,
      }),
      expect.anything()
    )
  })
})
