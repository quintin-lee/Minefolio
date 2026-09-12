import { describe, it, expect, beforeEach, vi, afterEach } from 'vitest'
import {
  buildApiUrl,
  getCookie,
  setMobileMode,
  getServerUrl,
  setServerUrl,
  resetServerUrl,
  isCustomServerUrl,
  normalizeServerUrl,
  isValidServerUrl,
  testServerConnection,
  onServerUrlChange,
  STORAGE_KEY_SERVER_URL,
} from '@/utils/http'

describe('http utils', () => {
  beforeEach(() => {
    localStorage.removeItem(STORAGE_KEY_SERVER_URL)
    resetServerUrl()
  })

  afterEach(() => {
    localStorage.removeItem(STORAGE_KEY_SERVER_URL)
    resetServerUrl()
  })

  describe('normalizeServerUrl', () => {
    it('handles empty strings cleanly', () => {
      expect(normalizeServerUrl('')).toBe('')
      expect(normalizeServerUrl('   ')).toBe('')
    })

    it('defaults to http:// when scheme is missing', () => {
      expect(normalizeServerUrl('192.168.1.100:8080')).toBe('http://192.168.1.100:8080')
      expect(normalizeServerUrl('example.com/api/')).toBe('http://example.com/api')
    })

    it('preserves existing http:// and https:// schemes', () => {
      expect(normalizeServerUrl('https://api.minefolio.com')).toBe('https://api.minefolio.com')
      expect(normalizeServerUrl('http://localhost:8080/')).toBe('http://localhost:8080')
    })

    it('strips multiple trailing slashes', () => {
      expect(normalizeServerUrl('https://demo.minefolio.com///')).toBe('https://demo.minefolio.com')
    })
  })

  describe('isValidServerUrl', () => {
    it('returns true for empty string (default fallback)', () => {
      expect(isValidServerUrl('')).toBe(true)
      expect(isValidServerUrl('   ')).toBe(true)
    })

    it('returns true for valid hostnames and IPs', () => {
      expect(isValidServerUrl('http://192.168.1.1:8080')).toBe(true)
      expect(isValidServerUrl('192.168.1.1:8080')).toBe(true)
      expect(isValidServerUrl('https://minefolio.example.com')).toBe(true)
    })

    it('returns false for invalid URLs', () => {
      expect(isValidServerUrl('http://')).toBe(false)
      expect(isValidServerUrl('https://')).toBe(false)
    })
  })

  describe('server URL configuration and persistence', () => {
    it('starts with uncustomized default', () => {
      expect(isCustomServerUrl()).toBe(false)
      expect(getServerUrl()).toBe('')
    })

    it('stores and retrieves custom server url', () => {
      setServerUrl('192.168.1.200:9090')
      expect(isCustomServerUrl()).toBe(true)
      expect(getServerUrl()).toBe('http://192.168.1.200:9090')
      expect(localStorage.getItem(STORAGE_KEY_SERVER_URL)).toBe('http://192.168.1.200:9090')
    })

    it('resets custom server url', () => {
      setServerUrl('https://custom.minefolio.org')
      expect(isCustomServerUrl()).toBe(true)
      resetServerUrl()
      expect(isCustomServerUrl()).toBe(false)
      expect(getServerUrl()).toBe('')
      expect(localStorage.getItem(STORAGE_KEY_SERVER_URL)).toBeNull()
    })

    it('triggers onServerUrlChange hooks', () => {
      const hook = vi.fn()
      onServerUrlChange(hook)

      setServerUrl('http://10.0.0.1:8080')
      expect(hook).toHaveBeenCalled()

      hook.mockClear()
      resetServerUrl()
      expect(hook).toHaveBeenCalled()
    })
  })

  describe('buildApiUrl', () => {
    it('normalizes leading slashes and prepends /api for relative paths', () => {
      expect(buildApiUrl('/auth/login')).toBe('/api/auth/login')
      expect(buildApiUrl('auth/login')).toBe('/api/auth/login')
      expect(buildApiUrl('/api/auth/login')).toBe('/api/auth/login')
    })

    it('handles special /api root path', () => {
      expect(buildApiUrl('/api')).toBe('/api')
      expect(buildApiUrl('/')).toBe('/api/')
    })

    it('dynamically prepends configured server URL', () => {
      setServerUrl('http://192.168.1.50:8080')
      expect(buildApiUrl('/auth/login')).toBe('http://192.168.1.50:8080/api/auth/login')
      expect(buildApiUrl('/api/transactions')).toBe('http://192.168.1.50:8080/api/transactions')

      setServerUrl('http://192.168.1.50:8080/api')
      expect(buildApiUrl('/auth/login')).toBe('http://192.168.1.50:8080/api/auth/login')
      expect(buildApiUrl('/api/transactions')).toBe('http://192.168.1.50:8080/api/transactions')
    })
  })

  describe('testServerConnection', () => {
    it('returns success when server responds with code 0', async () => {
      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn().mockResolvedValue({
        ok: true,
        json: async () => ({
          code: 0,
          message: 'ok',
          data: { initialized: true, version: '1.3.1' },
        }),
      }) as any

      try {
        const result = await testServerConnection('http://192.168.1.88:8080')
        expect(result.ok).toBe(true)
        expect(result.version).toBe('1.3.1')
        expect(result.initialized).toBe(true)
        expect(result.message).toContain('连接成功')
      } finally {
        globalThis.fetch = originalFetch
      }
    })

    it('returns error when server returns non-zero code or invalid body', async () => {
      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn().mockResolvedValue({
        ok: true,
        json: async () => ({
          other: 'data',
        }),
      }) as any

      try {
        const result = await testServerConnection('http://192.168.1.88:8080')
        expect(result.ok).toBe(false)
        expect(result.message).toContain('不是有效的 Minefolio 服务端')
      } finally {
        globalThis.fetch = originalFetch
      }
    })

    it('returns error when network fails', async () => {
      const originalFetch = globalThis.fetch
      globalThis.fetch = vi.fn().mockRejectedValue(new Error('Network unreachable')) as any

      try {
        const result = await testServerConnection('http://192.168.1.88:8080')
        expect(result.ok).toBe(false)
        expect(result.message).toBe('Network unreachable')
      } finally {
        globalThis.fetch = originalFetch
      }
    })
  })

  describe('getCookie', () => {
    beforeEach(() => {
      document.cookie = 'csrf_token=; expires=Thu, 01 Jan 1970 00:00:00 UTC; path=/;'
      document.cookie = 'other_val=; expires=Thu, 01 Jan 1970 00:00:00 UTC; path=/;'
    })

    it('extracts specific cookie value correctly', () => {
      document.cookie = 'csrf_token=abcdef123456'
      document.cookie = 'other_val=999'
      expect(getCookie('csrf_token')).toBe('abcdef123456')
      expect(getCookie('other_val')).toBe('999')
    })

    it('returns null for nonexistent cookie', () => {
      expect(getCookie('nonexistent')).toBeNull()
    })
  })

  describe('setMobileMode', () => {
    it('sets mobile mode without crashing', () => {
      expect(() => setMobileMode(true)).not.toThrow()
      expect(() => setMobileMode(false)).not.toThrow()
    })
  })
})

