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
})
