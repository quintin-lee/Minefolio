import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { useChatStore } from '@/stores/chat'

vi.mock('@/api/ai', () => ({
  listSessions: vi.fn().mockResolvedValue({ list: [] }),
  createSession: vi.fn(),
  deleteSession: vi.fn(),
  getSession: vi.fn(),
  updateSession: vi.fn(),
  getMessages: vi.fn(),
  chatStream: vi.fn(),
  getModels: vi.fn(),
  getSettings: vi.fn(),
  getWorkflows: vi.fn(),
  runWorkflowStream: vi.fn(),
}))

import { getMessages } from '@/api/ai'

function msg(id: number, sessionId: number, role: 'user' | 'assistant' = 'user') {
  return { id, session_id: sessionId, role, content: `消息-${id}`, created_at: '2026-01-01T00:00:00Z' }
}

interface Deferred {
  promise: Promise<Record<string, unknown>>
  resolve: (v: Record<string, unknown>) => void
}

function deferred(): Deferred {
  let resolve!: (v: Record<string, unknown>) => void
  const promise = new Promise<Record<string, unknown>>((r) => { resolve = r })
  return { promise, resolve }
}

describe('chat store session switching races', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    localStorage.clear()
    vi.clearAllMocks()
  })

  it('discards an out-of-order selectSession result that resolves after a newer switch', async () => {
    const pending = new Map<number, Deferred>()
    ;(getMessages as any).mockImplementation((sessionId: number) => {
      const d = deferred()
      pending.set(sessionId, d)
      return d.promise
    })

    const store = useChatStore()
    const p1 = store.selectSession(1) // 先切换 1，未返回
    const p2 = store.selectSession(2) // 用户紧接着切到 2

    // 会话 2 先返回
    pending.get(2)!.resolve({ list: [msg(20, 2)], total: 1 } as any)
    await p2
    // 会话 1 后返回 (过期) → 必须被丢弃
    pending.get(1)!.resolve({ list: [msg(10, 1)], total: 1 } as any)
    await p1

    expect(store.currentSessionId).toBe(2)
    expect(store.messages.length).toBe(1)
    expect(store.messages[0]!.session_id).toBe(2)
    expect(store.messages[0]!.content).toBe('消息-20')
  })

  it('discards a stale loadMoreMessages result when the session changes mid-flight', async () => {
    const pending = new Map<string, Deferred>()
    ;(getMessages as any).mockImplementation((sessionId: number, page: number) => {
      const d = deferred()
      pending.set(`${sessionId}:${page}`, d)
      return d.promise
    })

    const store = useChatStore()

    // 打开会话 1 首页
    const p1 = store.selectSession(1)
    pending.get('1:1')!.resolve({ list: [msg(11, 1)], total: 100 } as any)
    await p1
    expect(store.messages.length).toBe(1)

    // 触发会话 1 的"加载更多" (第 2 页在途)
    const more = store.loadMoreMessages()

    // 结果返回前用户切到会话 2 并完成加载
    const p2 = store.selectSession(2)
    pending.get('2:1')!.resolve({ list: [msg(20, 2)], total: 1 } as any)
    await p2

    // 会话 1 的过期第 2 页此时才返回 → 必须丢弃
    pending.get('1:2')!.resolve({ list: [msg(12, 1)], total: 100 } as any)
    await more

    expect(store.currentSessionId).toBe(2)
    // 会话 2 的消息未被旧会话的分页结果污染
    expect(store.messages.length).toBe(1)
    expect(store.messages[0]!.session_id).toBe(2)
    expect(store.messages[0]!.content).toBe('消息-20')
    // 旧分页不能推进当前会话的页码/总数
    expect(store.loadedMessagePage).toBe(1)
  })
})

describe('chat store session pinning', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    localStorage.clear()
    vi.clearAllMocks()
  })

  it('initializes pinned sessions from localStorage', () => {
    localStorage.setItem('minefolio_pinned_sessions', JSON.stringify([101, 102]))
    const store = useChatStore()
    expect(store.pinnedSessionIds).toEqual([101, 102])
    expect(store.isSessionPinned(101)).toBe(true)
    expect(store.isSessionPinned(103)).toBe(false)
  })

  it('togglePinSession adds, removes session ID, and persists to localStorage', () => {
    const store = useChatStore()
    expect(store.pinnedSessionIds).toEqual([])
    expect(store.isSessionPinned(50)).toBe(false)

    // 置顶会话 50
    store.togglePinSession(50)
    expect(store.pinnedSessionIds).toEqual([50])
    expect(store.isSessionPinned(50)).toBe(true)
    expect(localStorage.getItem('minefolio_pinned_sessions')).toBe(JSON.stringify([50]))

    // 置顶会话 60
    store.togglePinSession(60)
    expect(store.pinnedSessionIds).toEqual([50, 60])
    expect(store.isSessionPinned(60)).toBe(true)

    // 再次点击 50 取消置顶
    store.togglePinSession(50)
    expect(store.pinnedSessionIds).toEqual([60])
    expect(store.isSessionPinned(50)).toBe(false)
    expect(localStorage.getItem('minefolio_pinned_sessions')).toBe(JSON.stringify([60]))
  })
})

