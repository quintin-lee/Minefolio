import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { useAuthStore } from '@/stores/auth'

vi.mock('@/api/auth', () => ({
  authApi: {
    login: vi.fn(),
    register: vi.fn(),
    me: vi.fn(),
    verify2FaLogin: vi.fn(),
    changePassword: vi.fn(),
  },
}))

vi.mock('@/api/system', () => ({
  systemApi: {
    status: vi.fn(),
    setup: vi.fn(),
  },
}))

import { authApi } from '@/api/auth'
import { systemApi } from '@/api/system'
import { clearCryptoKeyCache } from '@/utils/crypto'

describe('auth store', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    localStorage.clear()
    clearCryptoKeyCache()
    vi.clearAllMocks()

    // Mock fetch for public key
    global.fetch = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        code: 0,
        data: {
          public_key: {
            kty: 'RSA',
            n: 'u1lq',
            e: 'AQAB',
          },
        },
      }),
    }) as any

    // Mock crypto.subtle methods
    vi.spyOn(crypto.subtle, 'importKey').mockResolvedValue({} as CryptoKey)
    vi.spyOn(crypto.subtle, 'encrypt').mockResolvedValue(new Uint8Array([1, 2, 3]).buffer)
  })

  it('checks system status', async () => {
    ;(systemApi.status as any).mockResolvedValue({ initialized: true })
    const store = useAuthStore()
    const res = await store.checkSystemStatus()
    expect(res).toBe(true)
    expect(store.isInitialized).toBe(true)
  })

  it('handles login with standard token and fetches user', async () => {
    ;(authApi.login as any).mockResolvedValue({ token: 'test-jwt-token' })
    ;(authApi.me as any).mockResolvedValue({ id: 1, username: 'admin', created_at: '2026-01-01' })

    const store = useAuthStore()
    const res = await store.login('admin', 'password123')
    expect(res.token).toBe('test-jwt-token')
    expect(store.token).toBe('test-jwt-token')
    expect(localStorage.getItem('token')).toBe('test-jwt-token')
    expect(store.user?.username).toBe('admin')
  })

  it('handles login requiring 2FA challenge', async () => {
    ;(authApi.login as any).mockResolvedValue({
      require_2fa: true,
      temp_token: 'temp-2fa-token',
    })

    const store = useAuthStore()
    const res = await store.login('admin', 'password123')
    expect(res.require_2fa).toBe(true)
    expect(res.temp_token).toBe('temp-2fa-token')
    expect(store.token).toBe('') // Main token not set yet
  })

  it('handles 2FA verification and sets token', async () => {
    ;(authApi.verify2FaLogin as any).mockResolvedValue({ token: 'verified-jwt-token' })
    ;(authApi.me as any).mockResolvedValue({ id: 1, username: 'admin', created_at: '2026-01-01' })

    const store = useAuthStore()
    const res = await store.verify2FaLogin('temp-2fa-token', '123456')
    expect(res.token).toBe('verified-jwt-token')
    expect(store.token).toBe('verified-jwt-token')
    expect(localStorage.getItem('token')).toBe('verified-jwt-token')
  })

  it('clears state on logout', async () => {
    const store = useAuthStore()
    store.token = 'existing-token'
    store.user = { id: 1, username: 'admin', created_at: '2026-01-01' }
    localStorage.setItem('token', 'existing-token')
    localStorage.setItem('minefolio:chat:draft:new', 'secret draft text')

    // 模拟上一账号残留的聊天会话与消息
    const { useChatStore } = await import('@/stores/chat')
    const chat = useChatStore()
    chat.currentSessionId = 123
    chat.sessions.push({ id: 123, title: '旧会话' } as any)
    chat.messages.push({ id: 1, session_id: 123, role: 'user', content: '机密内容', created_at: '2026-01-01' } as any)

    await store.logout()
    expect(store.token).toBe('')
    expect(store.user).toBeNull()
    expect(localStorage.getItem('token')).toBeNull()
    // 聊天草稿与内存态一并清空，避免泄露给下一个登录账号
    expect(localStorage.getItem('minefolio:chat:draft:new')).toBeNull()
    expect(chat.currentSessionId).toBeNull()
    expect(chat.sessions.length).toBe(0)
    expect(chat.messages.length).toBe(0)
  })
})
