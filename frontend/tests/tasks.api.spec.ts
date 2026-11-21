import { describe, it, expect, vi, beforeEach } from 'vitest'
import http from '@/utils/http'
import { pollTask, resolveMaybeAsync, isAsyncTaskSubmission } from '@/api/tasks'

vi.mock('@/utils/http', () => ({
  default: {
    get: vi.fn(),
    post: vi.fn(),
  },
}))

describe('tasks API & pollTask', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('recognizes async task submission envelopes', () => {
    expect(isAsyncTaskSubmission({ task_id: 12, status: 'pending' })).toBe(true)
    expect(isAsyncTaskSubmission({ task_id: 0, status: 'pending' })).toBe(false)
    expect(isAsyncTaskSubmission({ synced_count: 3 })).toBe(false)
    expect(isAsyncTaskSubmission(null)).toBe(false)
  })

  it('polls until task status becomes completed and forwards progress', async () => {
    const onProgress = vi.fn()
    vi.mocked(http.get)
      .mockResolvedValueOnce({ id: 9, status: 'running', progress: 40, message: '执行中', result: null })
      .mockResolvedValueOnce({ id: 9, status: 'completed', progress: 100, message: '已完成', result: { synced_count: 2 } })

    const res = await pollTask<{ synced_count: number }>(9, { intervalMs: 1, timeoutMs: 2000, onProgress })
    expect(res).toEqual({ synced_count: 2 })
    expect(onProgress).toHaveBeenCalledTimes(2)
  })

  it('throws server error_message when async task fails', async () => {
    vi.mocked(http.get).mockResolvedValueOnce({
      id: 10,
      status: 'failed',
      progress: 0,
      message: '失败',
      error_message: '上游连接超时',
      result: null,
    })

    await expect(pollTask(10, { intervalMs: 1, timeoutMs: 2000 })).rejects.toThrow('上游连接超时')
  })

  it('supports synchronous direct response in resolveMaybeAsync', async () => {
    const direct = await resolveMaybeAsync({ status: 'ok', mode: 'l2' })
    expect(direct).toEqual({ status: 'ok', mode: 'l2' })
    expect(http.get).not.toHaveBeenCalled()
  })
})
