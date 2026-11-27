import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { ElMessage } from 'element-plus'
import http, { setAuthErrorHandler } from '@/utils/http'

describe('http Blob error envelope interception', () => {
  const authErrorHandler = vi.fn()

  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
    setAuthErrorHandler(authErrorHandler)
  })

  afterEach(() => {
    setAuthErrorHandler(null)
    vi.restoreAllMocks()
  })

  it('rejects Blob with type application/json and code 1001 and calls handleAuthError', async () => {
    const blob = new Blob([JSON.stringify({ code: 1001, message: 'Unauthorized' })], {
      type: 'application/json',
    })

    await expect(
      http.get('/backup/export', {
        responseType: 'blob',
        adapter: async (config) => ({
          data: blob,
          status: 200,
          statusText: 'OK',
          headers: { 'content-type': 'application/json' },
          config,
        }),
      })
    ).rejects.toMatchObject({
      response: {
        data: {
          code: 1001,
          message: 'Unauthorized',
        },
      },
    })

    expect(authErrorHandler).toHaveBeenCalledTimes(1)
  })

  it('rejects Blob with type application/json and non-zero business code and shows error toast', async () => {
    const errorSpy = vi.spyOn(ElMessage, 'error').mockImplementation(() => ({ close: vi.fn() }) as any)
    const blob = new Blob([JSON.stringify({ code: 1004, message: 'Export forbidden' })], {
      type: 'application/json; charset=utf-8',
    })

    await expect(
      http.get('/backup/export', {
        responseType: 'blob',
        adapter: async (config) => ({
          data: blob,
          status: 200,
          statusText: 'OK',
          headers: { 'content-type': 'application/json; charset=utf-8' },
          config,
        }),
      })
    ).rejects.toMatchObject({
      response: {
        data: {
          code: 1004,
          message: 'Export forbidden',
        },
      },
    })

    expect(authErrorHandler).not.toHaveBeenCalled()
    expect(errorSpy).toHaveBeenCalledWith('Export forbidden')
  })

  it('passes through non-JSON binary Blob unchanged', async () => {
    const blob = new Blob(['binary-content'], { type: 'application/octet-stream' })

    const res = await http.get<Blob>('/backup/export', {
      responseType: 'blob',
      adapter: async (config) => ({
        data: blob,
        status: 200,
        statusText: 'OK',
        headers: { 'content-type': 'application/octet-stream' },
        config,
      }),
    })

    expect(res).toBe(blob)
    expect(authErrorHandler).not.toHaveBeenCalled()
  })
})
