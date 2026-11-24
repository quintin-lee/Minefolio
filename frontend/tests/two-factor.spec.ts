import { describe, it, expect, beforeEach, vi, afterEach } from 'vitest'
import {
  normalize2FaCode,
  save2FaTempSession,
  load2FaTempSession,
  clear2FaTempSession,
  STORAGE_KEY_2FA_SESSION,
  STORAGE_KEY_2FA_LEGACY,
} from '@/utils/two-factor'

describe('two-factor.ts utility', () => {
  beforeEach(() => {
    localStorage.clear()
    sessionStorage.clear()
    vi.useRealTimers()
  })

  afterEach(() => {
    vi.useRealTimers()
  })

  describe('normalize2FaCode', () => {
    it('returns empty string for falsy input', () => {
      expect(normalize2FaCode('')).toBe('')
      expect(normalize2FaCode(null as any)).toBe('')
      expect(normalize2FaCode(undefined as any)).toBe('')
    })

    it('normalizes standard 6-digit TOTP code with spaces', () => {
      expect(normalize2FaCode(' 123 456 ')).toBe('123456')
      expect(normalize2FaCode('123\u00A0456')).toBe('123456') // non-breaking space
      expect(normalize2FaCode('123\u3000456')).toBe('123456') // full-width ideographic space
    })

    it('converts full-width digits to half-width ASCII digits', () => {
      expect(normalize2FaCode('１２３４５６')).toBe('123456')
      expect(normalize2FaCode(' ７８９ ０１２ ')).toBe('789012')
      expect(normalize2FaCode('０1２3４5')).toBe('012345')
    })

    it('strips zero-width spaces and invisible characters', () => {
      expect(normalize2FaCode('12\u200B34\uFEFF56')).toBe('123456')
      expect(normalize2FaCode('\u200C123\u200D456\u200E')).toBe('123456')
    })

    it('normalizes backup codes with full-width letters and unicode dashes', () => {
      // 全角字母 + 中文破折号
      expect(normalize2FaCode('Ａ１Ｂ２—Ｃ３Ｄ４')).toBe('a1b2-c3d4')
      // 全角减号
      expect(normalize2FaCode('a1b2－c3d4')).toBe('a1b2-c3d4')
      // 英文 em-dash / en-dash
      expect(normalize2FaCode('A1B2–C3D4')).toBe('a1b2-c3d4')
      expect(normalize2FaCode('A1B2—C3D4')).toBe('a1b2-c3d4')
    })

    it('converts uppercase backup code to lowercase', () => {
      expect(normalize2FaCode('X9Y8-Z7W6')).toBe('x9y8-z7w6')
    })
  })

  describe('session persistence & expiration', () => {
    it('persists session in localStorage with TTL and mirrors to sessionStorage', () => {
      save2FaTempSession('token_abc', 300000)

      expect(sessionStorage.getItem(STORAGE_KEY_2FA_LEGACY)).toBe('token_abc')
      const stored = JSON.parse(localStorage.getItem(STORAGE_KEY_2FA_SESSION) || '{}')
      expect(stored.tempToken).toBe('token_abc')
      expect(stored.expiresAt).toBeGreaterThan(Date.now())

      expect(load2FaTempSession()).toBe('token_abc')
    })

    it('falls back to sessionStorage if localStorage session is missing', () => {
      sessionStorage.setItem(STORAGE_KEY_2FA_LEGACY, 'token_session_only')
      expect(load2FaTempSession()).toBe('token_session_only')
    })

    it('expires and drops token when TTL has passed', () => {
      vi.useFakeTimers()
      const now = 1700000000000
      vi.setSystemTime(now)

      save2FaTempSession('token_short_lived', 5000) // 5 seconds TTL
      expect(load2FaTempSession()).toBe('token_short_lived')

      // Advance clock past expiration
      vi.setSystemTime(now + 6000)
      sessionStorage.clear() // simulate webview killed and sessionStorage dropped
      expect(load2FaTempSession()).toBeNull()
      expect(localStorage.getItem(STORAGE_KEY_2FA_SESSION)).toBeNull()
    })

    it('clears both localStorage and sessionStorage on clear2FaTempSession', () => {
      save2FaTempSession('token_to_clear')
      expect(load2FaTempSession()).toBe('token_to_clear')

      clear2FaTempSession()
      expect(load2FaTempSession()).toBeNull()
      expect(localStorage.getItem(STORAGE_KEY_2FA_SESSION)).toBeNull()
      expect(sessionStorage.getItem(STORAGE_KEY_2FA_LEGACY)).toBeNull()
    })
  })
})
