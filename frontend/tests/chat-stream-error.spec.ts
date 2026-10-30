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

describe('chat store streaming pacing', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    localStorage.clear()
    vi.clearAllMocks()
  })

  it('buffers deltas through the typewriter instead of appending bursts', async () => {
    async function* burstStream() {
      yield { type: 'delta' as const, content: '这一段'.repeat(20) } // 60 字符突发
      yield { type: 'done' as const, finish_reason: 'stop' }
    }
    ;(chatStream as any).mockReturnValue(burstStream())

    // fake RAF：手动推进帧，观察每帧吐字量
    let frameCb: (() => void) | null = null
    const raf = (cb: () => void) => { frameCb = cb; return 1 }
    const caf = () => { frameCb = null }
    vi.stubGlobal('requestAnimationFrame', raf)
    vi.stubGlobal('cancelAnimationFrame', caf)

    const store = useChatStore()
    store.currentSessionId = 1
    const sending = store.sendMessage('测试平滑输出')

    // delta 已到达：writer 持有积压，正文尚未整块蹦出
    await Promise.resolve()
    await Promise.resolve()
    const assistantMsg = store.messages.find(m => m.role === 'assistant')!
    const afterFirstFrames = assistantMsg.content.length
    expect(afterFirstFrames).toBeLessThan(60) // 不是整块直写

    // 推进若干帧：逐步吐字
    const observed: number[] = []
    for (let i = 0; i < 10; i++) {
      frameCb?.()
      await Promise.resolve()
      observed.push(assistantMsg.content.length)
    }
    // 每帧增量有上限（自适应步长，非整块）
    const maxStep = Math.max(...observed.map((v, i) => v - (observed[i - 1] ?? afterFirstFrames)))
    expect(maxStep).toBeLessThanOrEqual(20)

    // 推进到清空缓冲
    for (let i = 0; i < 200 && frameCb; i++) { frameCb?.(); await Promise.resolve() }
    expect(assistantMsg.content.length).toBe(60)

    await sending
    vi.unstubAllGlobals()
  })
})
