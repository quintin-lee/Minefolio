import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'

vi.mock('@/utils/http', () => ({
  default: {
    get: vi.fn().mockResolvedValue({ id: 1, title: '月度复盘' }),
    post: vi.fn(),
    put: vi.fn(),
    delete: vi.fn(),
  },
  getCookie: vi.fn(() => ''),
  buildApiUrl: vi.fn((p: string) => `/api${p}`),
  handleAuthError: vi.fn(),
}))

import { assertStreamResponseOk, runWorkflowStream } from '@/api/ai'
import { useChatStore } from '@/stores/chat'

describe('Workflow SSE message IDs and assertStreamResponseOk', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    localStorage.clear()
    vi.clearAllMocks()
  })

  it('preserves id on user_message_id and assistant_message_id events in runWorkflowStream', async () => {
    const encoder = new TextEncoder()
    const stream = new ReadableStream<Uint8Array>({
      start(controller) {
        controller.enqueue(
          encoder.encode(
            [
              'event: user_message_id\ndata: {"id":"101"}\n\n',
              'event: workflow_start\ndata: {"workflow_id":"wf_monthly_review","title":"月度复盘","total_steps":1}\n\n',
              'event: delta\ndata: {"content":"报告正文"}\n\n',
              'event: assistant_message_id\ndata: {"id":202}\n\n',
              'event: workflow_complete\ndata: {}\n\n',
            ].join('')
          )
        )
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
    for await (const chunk of runWorkflowStream({ workflow_id: 'wf_monthly_review', session_id: 1 })) {
      chunks.push(chunk)
    }

    const userIdChunk = chunks.find(c => c.type === 'user_message_id')
    const assistantIdChunk = chunks.find(c => c.type === 'assistant_message_id')
    expect(userIdChunk?.id).toBe('101')
    expect(assistantIdChunk?.id).toBe(202)
  })

  it('throws body.error on HTTP 403 JSON without consuming valid SSE body', async () => {
    const resp403 = {
      status: 403,
      ok: false,
      headers: new Headers({ 'content-type': 'application/json' }),
      json: vi.fn().mockResolvedValue({ error: 'Forbidden: read-only ledger access' }),
    } as unknown as Response

    await expect(assertStreamResponseOk(resp403)).rejects.toThrow('Forbidden: read-only ledger access')

    const encoder = new TextEncoder()
    const sseBody = new ReadableStream<Uint8Array>({
      start(controller) {
        controller.enqueue(encoder.encode('event: delta\ndata: {"content":"ok"}\n\n'))
        controller.close()
      },
    })
    const jsonSpy = vi.fn()
    const textSpy = vi.fn()
    const respOk = {
      status: 200,
      ok: true,
      headers: new Headers({ 'content-type': 'text/event-stream' }),
      body: sseBody,
      json: jsonSpy,
      text: textSpy,
    } as unknown as Response

    const returnedBody = await assertStreamResponseOk(respOk)
    expect(returnedBody).toBe(sseBody)
    expect(jsonSpy).not.toHaveBeenCalled()
    expect(textSpy).not.toHaveBeenCalled()
  })

  it('adopts user_message_id and assistant_message_id into message server_id during runWorkflow', async () => {
    const encoder = new TextEncoder()
    const stream = new ReadableStream<Uint8Array>({
      start(controller) {
        controller.enqueue(
          encoder.encode(
            [
              'event: user_message_id\ndata: {"id":"301"}\n\n',
              'event: delta\ndata: {"content":"分析完成"}\n\n',
              'event: assistant_message_id\ndata: {"id":"302"}\n\n',
              'event: workflow_complete\ndata: {}\n\n',
            ].join('')
          )
        )
        controller.close()
      },
    })

    global.fetch = vi.fn().mockResolvedValue({
      status: 200,
      ok: true,
      headers: new Headers({ 'content-type': 'text/event-stream' }),
      body: stream,
    }) as any

    const store = useChatStore()
    store.currentSessionId = 1
    await store.runWorkflow('wf_monthly_review')

    const userMsg = store.messages.find(m => m.role === 'user')
    const assistantMsg = store.messages.find(m => m.role === 'assistant')
    expect(userMsg?.server_id).toBe(301)
    expect(assistantMsg?.server_id).toBe(302)
  })
})
