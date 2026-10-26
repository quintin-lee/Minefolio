import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { useChatStore } from '@/stores/chat'

vi.mock('@/api/ai', () => ({
  listSessions: vi.fn().mockResolvedValue({ list: [] }),
  createSession: vi.fn().mockResolvedValue({ data: { id: 1, title: '新会话' } }),
  deleteSession: vi.fn().mockResolvedValue(undefined),
  getSession: vi.fn().mockResolvedValue({ data: { id: 1, title: '测试会话' } }),
  updateSession: vi.fn(),
  getMessages: vi.fn(),
  chatStream: vi.fn(),
  getModels: vi.fn(),
  getSettings: vi.fn(),
  getWorkflows: vi.fn(),
  runWorkflowStream: vi.fn(),
}))

import { chatStream } from '@/api/ai'

describe('chat store stream failure handling', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    localStorage.clear()
    vi.clearAllMocks()
  })

  it('preserves already streamed content and records the error separately', async () => {
    async function* failingStream() {
      yield { type: 'delta' as const, content: '已生成的部分正文' }
      yield { type: 'error' as const, message: '请求过于频繁或额度不足 (HTTP 429)' }
    }
    ;(chatStream as any).mockReturnValue(failingStream())

    const store = useChatStore()
    store.currentSessionId = 1
    await store.sendMessage('测试失败路径')

    const assistantMsg = store.messages.find(m => m.role === 'assistant')
    // 失败不得覆盖已流式输出的正文，错误单独落在 error 字段供错误条渲染
    expect(assistantMsg?.content).toBe('已生成的部分正文')
    expect(assistantMsg?.error).toBe('请求过于频繁或额度不足 (HTTP 429)')
    expect(store.isStreaming).toBe(false)
  })

  it('aborts the in-flight stream when deleting the active session', async () => {
    let aborted = false
    async function* slowStream() {
      yield { type: 'delta' as const, content: '开头' }
      await new Promise<void>(resolve => setTimeout(resolve, 50))
      yield { type: 'delta' as const, content: '结尾' }
    }
    ;(chatStream as any).mockImplementation((_body: unknown, signal?: AbortSignal) => {
      signal?.addEventListener('abort', () => {
        aborted = true
      })
      return slowStream()
    })

    const store = useChatStore()
    store.currentSessionId = 1
    const sending = store.sendMessage('会被删除中断的消息')
    await new Promise<void>(resolve => setTimeout(resolve, 0))
    await store.deleteSessionById(1)
    await sending

    expect(aborted).toBe(true)
    expect(store.currentSessionId).toBeNull()
    expect(store.messages.length).toBe(0)
  })
})
