import { describe, it, expect, vi, beforeEach } from 'vitest'

vi.mock('@/utils/http', () => ({
  default: {
    get: vi.fn(),
    post: vi.fn(),
    put: vi.fn(),
    delete: vi.fn(),
  },
  getCookie: vi.fn(() => ''),
  buildApiUrl: vi.fn((p: string) => `/api${p}`),
  handleAuthError: vi.fn(),
}))

import { chatStream, runWorkflowStream } from '@/api/ai'
import { handleAuthError } from '@/utils/http'

describe('AI SSE auth envelope alignment', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('triggers handleAuthError when chatStream receives HTTP 200 JSON code=1001', async () => {
    global.fetch = vi.fn().mockResolvedValue({
      status: 200,
      ok: true,
      headers: new Headers({ 'content-type': 'application/json' }),
      json: async () => ({ code: 1001, message: 'token expired' }),
    }) as any

    const iter = chatStream({ content: 'hello' })[Symbol.asyncIterator]()
    await expect(iter.next()).rejects.toThrow(/token expired/)
    expect(handleAuthError).toHaveBeenCalledTimes(1)
  })

  it('triggers handleAuthError when runWorkflowStream receives HTTP 200 JSON code=1001', async () => {
    global.fetch = vi.fn().mockResolvedValue({
      status: 200,
      ok: true,
      headers: new Headers({ 'content-type': 'application/json' }),
      json: async () => ({ code: 1001, message: 'unauthorized' }),
    }) as any

    const iter = runWorkflowStream({ workflow_id: 'wf_monthly_review' })[Symbol.asyncIterator]()
    await expect(iter.next()).rejects.toThrow(/unauthorized/)
    expect(handleAuthError).toHaveBeenCalledTimes(1)
  })

  it('uses credentials=same-origin instead of include so mobile cross-origin CORS does not fail', async () => {
    const encoder = new TextEncoder()
    const stream = new ReadableStream<Uint8Array>({
      start(controller) {
        controller.enqueue(encoder.encode('event: done\ndata: {"finish_reason":"stop"}\n\n'))
        controller.close()
      },
    })
    const fetchMock = vi.fn().mockResolvedValue({
      status: 200,
      ok: true,
      headers: new Headers({ 'content-type': 'text/event-stream' }),
      body: stream,
    })
    global.fetch = fetchMock as any

    const chunks = []
    for await (const c of chatStream({ content: 'ping' })) chunks.push(c)
    expect(chunks.some(c => c.type === 'done')).toBe(true)
    expect(fetchMock.mock.calls[0]?.[1]?.credentials).toBe('same-origin')

    const wfStream = new ReadableStream<Uint8Array>({
      start(controller) {
        controller.enqueue(encoder.encode('event: workflow_complete\ndata: {}\n\n'))
        controller.close()
      },
    })
    fetchMock.mockResolvedValueOnce({
      status: 200,
      ok: true,
      headers: new Headers({ 'content-type': 'text/event-stream' }),
      body: wfStream,
    })
    for await (const c of runWorkflowStream({ workflow_id: 'wf_monthly_review' })) {
      void c
    }
    expect(fetchMock.mock.calls[1]?.[1]?.credentials).toBe('same-origin')
  })
})
