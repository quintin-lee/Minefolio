import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { extractPanelItems } from '@/utils/chat-panel-extractor'

vi.mock('@/utils/http', () => ({
  default: { get: vi.fn(), post: vi.fn(), put: vi.fn(), delete: vi.fn() },
  getCookie: vi.fn(() => ''),
  buildApiUrl: vi.fn((p: string) => `/api${p}`),
  handleAuthError: vi.fn(),
  isMobileMode: vi.fn(() => false),
}))

describe('Frontend Audit Phase 2 fixes', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    localStorage.clear()
    vi.clearAllMocks()
  })

  afterEach(() => {
    vi.doUnmock('@/api/ai')
    vi.resetModules()
  })

  it('chatStore keeps isStreaming === true throughout streaming and resets on completion', async () => {
    vi.resetModules()
    vi.doMock('@/api/ai', () => ({
      listSessions: vi.fn().mockResolvedValue({ list: [] }),
      createSession: vi.fn().mockResolvedValue({ data: { id: 42, user_id: 1, title: 'T' } }),
      deleteSession: vi.fn(),
      getSession: vi.fn().mockResolvedValue({ data: { id: 42, title: 'T' } }),
      updateSession: vi.fn(),
      getMessages: vi.fn(),
      getModels: vi.fn(),
      getSettings: vi.fn(),
      getWorkflows: vi.fn(),
      runWorkflowStream: vi.fn(),
      chatStream: vi.fn(),
    }))

    const aiMod = await import('@/api/ai')
    const { useChatStore } = await import('@/stores/chat')
    const store = useChatStore()

    let streamingDuringChunk: boolean | null = null
    let releaseSecondChunk!: () => void
    const gate = new Promise<void>((resolve) => { releaseSecondChunk = resolve })

    ;(aiMod.chatStream as any).mockImplementation(async function* () {
      yield { type: 'delta', content: 'Hello ' }
      streamingDuringChunk = store.isStreaming
      await gate
      yield { type: 'delta', content: 'world' }
      yield { type: 'done', finish_reason: 'stop', total_tokens: 10 }
    })

    const sendPromise = store.sendMessage('hi')
    // Wait a tick for stream to reach first chunk
    await new Promise((r) => setTimeout(r, 20))
    expect(store.isStreaming).toBe(true)
    expect(streamingDuringChunk).toBe(true)

    releaseSecondChunk()
    await sendPromise
    expect(store.isStreaming).toBe(false)
  })

  it('chatStream resets currentEvent after blank lines and flushes trailing buffer without trailing newline', async () => {
    const { chatStream } = await vi.importActual<typeof import('@/api/ai')>('@/api/ai')
    const encoder = new TextEncoder()
    const stream = new ReadableStream<Uint8Array>({
      start(controller) {
        controller.enqueue(encoder.encode('event: thinking\ndata: {"content":"hmm"}\n\ndata: {"content":"answer"}\nevent: done\ndata: {"finish_reason":"stop"}'))
        controller.close()
      },
    })
    global.fetch = vi.fn().mockResolvedValue({
      status: 200,
      ok: true,
      headers: new Headers({ 'content-type': 'text/event-stream' }),
      body: stream,
    }) as any

    const chunks = []
    for await (const c of chatStream({ content: 'test' })) {
      chunks.push(c)
    }

    expect(chunks.map((c) => c.type)).toEqual(['thinking', 'delta', 'done'])
    expect(chunks[1]?.content).toBe('answer')
    expect(chunks[2]?.finish_reason).toBe('stop')
  })

  it('extractPanelItems assigns unique segmentIds across multiple tool proposals', () => {
    const toolCalls = [
      {
        name: 'propose_daily_expense',
        result: JSON.stringify({ propose_success: true, amount: 12, draft_token: 'tok1' }),
      },
      {
        name: 'propose_transfer',
        result: JSON.stringify({ propose_success: true, amount: 50, draft_token: 'tok2' }),
      },
    ]
    const res = extractPanelItems(200, '已生成两笔待确认操作', undefined, undefined, toolCalls)
    expect(res.panelItems.length).toBe(2)
    expect(res.panelItems[0]?.segmentId).not.toBe(res.panelItems[1]?.segmentId)
    expect(res.extractedSegmentIds.size).toBe(2)
  })
})
