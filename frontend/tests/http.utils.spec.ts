import { describe, it, expect, beforeEach } from 'vitest'
import { buildApiUrl, getCookie, setMobileMode } from '@/utils/http'

describe('http utils', () => {
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
