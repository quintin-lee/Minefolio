import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { useAuthStore } from '@/stores/auth'

vi.mock('@/api/auth', () => ({
  authApi: {
    login: vi.fn(),
    logout: vi.fn().mockResolvedValue(undefined),
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

vi.mock('@/db/local', () => {
  let db: any = null
  return {
    initLocalDb: vi.fn(async () => {
      db = { exec: vi.fn() }
      return db
    }),
    getDb: vi.fn(() => db),
    __resetDb: () => { db = null },
  }
})

import { authApi } from '@/api/auth'
import { systemApi } from '@/api/system'
import { clearCryptoKeyCache } from '@/utils/crypto'
import { initLocalDb, getDb } from '@/db/local'

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

  it('re-initializes local db on successful login', async () => {
    ;(authApi.login as any).mockResolvedValue({ token: 'reinit-jwt-token' })
    ;(authApi.me as any).mockResolvedValue({ id: 1, username: 'admin', created_at: '2026-01-01' })

    const store = useAuthStore()
    const res = await store.login('admin', 'password123')
    expect(res.token).toBe('reinit-jwt-token')
    // 登出重置本地库后重新登录，必须调用 initLocalDb() 恢复离线功能，
    // 否则 getDb() 抛 'localDb not initialized'，离线降级与同步队列全部失效直至重启 App
    expect(initLocalDb).toHaveBeenCalled()
    expect(getDb()).not.toBeNull()
  })

  it('re-initializes local db after 2FA verification', async () => {
    ;(authApi.verify2FaLogin as any).mockResolvedValue({ token: 'verified-jwt-token' })
    ;(authApi.me as any).mockResolvedValue({ id: 1, username: 'admin', created_at: '2026-01-01' })

    const store = useAuthStore()
    await store.verify2FaLogin('temp-2fa-token', '123456')
    expect(initLocalDb).toHaveBeenCalled()
    expect(getDb()).not.toBeNull()
  })

  it('survives local db re-init failure without breaking login', async () => {
    ;(initLocalDb as any).mockRejectedValueOnce(new Error('wasm load failed'))
    ;(authApi.login as any).mockResolvedValue({ token: 'still-ok-token' })
    ;(authApi.me as any).mockResolvedValue({ id: 1, username: 'admin', created_at: '2026-01-01' })

    const store = useAuthStore()
    const res = await store.login('admin', 'password123')
    // 离线库初始化失败不影响在线登录主流程（降级为纯在线模式）
    expect(res.token).toBe('still-ok-token')
    expect(store.token).toBe('still-ok-token')
  })

  it('login failure surfaces crypto_failed only for WebCrypto-missing errors', async () => {
    // 场景 1: WebCrypto 缺失 (ensureSubtleCrypto 抛错消息含 WebCrypto/Crypto/subtle) 必须标记 crypto_failed
    ;(global as any).fetch = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({ code: 0, data: { public_key: { kty: 'RSA', n: 'u1lq', e: 'AQAB' } } }),
    })
    vi.spyOn(crypto.subtle, 'importKey').mockRejectedValueOnce(
      new Error('当前浏览环境不支持 WebCrypto（RSA 加密仅在 HTTPS 或 localhost 下可用）')
    )
    const store1 = useAuthStore()
    const err1 = await store1.login('admin', 'password123').catch((e: unknown) => e) as any
    expect(err1?.crypto_failed).toBe(true)
    expect(err1?.message).toMatch(/WebCrypto/)

    // 场景 2: 网络错误 (fetch 无 response 属性) 不再被误标为 crypto_failed，必须原样上抛让 isNetworkError 命中
    clearCryptoKeyCache()
    ;(global as any).fetch = vi.fn().mockRejectedValue(new Error('Failed to fetch'))
    const store2 = useAuthStore()
    const err2 = await store2.login('admin', 'password123').catch((e: unknown) => e) as any
    expect(err2?.crypto_failed).toBeUndefined()
    expect(err2?.message).toBe('Failed to fetch')
    expect('response' in err2).toBe(false)
  })

  it('clears state on logout', async () => {
    const store = useAuthStore()
    store.token = 'existing-token'
    store.user = { id: 1, username: 'admin', created_at: '2026-01-01' }
    localStorage.setItem('token', 'existing-token')
    localStorage.setItem('minefolio:chat:draft:new', 'secret draft text')

    // 模拟上一账号残留的聊天会话、账本列表与分类缓存
    const { useChatStore } = await import('@/stores/chat')
    const { useLedgerStore } = await import('@/stores/ledger')
    const { useCategoryStore } = await import('@/stores/category')
    const chat = useChatStore()
    chat.currentSessionId = 123
    chat.sessions.push({ id: 123, title: '旧会话' } as any)
    chat.messages.push({ id: 1, session_id: 123, role: 'user', content: '机密内容', created_at: '2026-01-01' } as any)
    const ledger = useLedgerStore()
    ledger.ledgers.push({ id: 1, name: '旧账本', currency: 'CNY', is_default: true, my_role: 'owner' } as any)
    ledger.currentLedgerId = 1
    const category = useCategoryStore()
    category.allNodes.push({ id: 1, name: '旧分类', type: 'asset', currency: 'CNY', sort_order: 0 } as any)
    category.loaded = true

    await store.logout()
    expect(authApi.logout).toHaveBeenCalledWith('existing-token')
    expect(store.token).toBe('')
    expect(store.user).toBeNull()
    expect(localStorage.getItem('token')).toBeNull()
    // 聊天草稿与各 store 内存态一并清空，避免泄露给下一个登录账号
    expect(localStorage.getItem('minefolio:chat:draft:new')).toBeNull()
    expect(chat.currentSessionId).toBeNull()
    expect(chat.sessions.length).toBe(0)
    expect(chat.messages.length).toBe(0)
    expect(ledger.ledgers.length).toBe(0)
    expect(ledger.currentLedgerId).toBeNull()
    expect(category.allNodes.length).toBe(0)
    expect(category.loaded).toBe(false)
  }, 60000)

  it('skips server logout request when skipServer is true', async () => {
    const store = useAuthStore()
    store.token = 'expired-token'
    await store.logout({ skipServer: true })
    expect(authApi.logout).not.toHaveBeenCalled()
    expect(store.token).toBe('')
  })

  it('does not wipe a newly logged-in session when an earlier async logout finishes late', async () => {
    const store = useAuthStore()
    store.token = 'old-token'

    const { useLedgerStore } = await import('@/stores/ledger')
    const ledger = useLedgerStore()

    const pendingLogout = store.logout()
    // Simulate immediate login of a new user before logout's dynamic imports resolve
    store.setToken('new-user-token')
    ledger.ledgers.push({ id: 99, name: '新用户账本', currency: 'CNY', is_default: true, my_role: 'owner' } as any)
    ledger.currentLedgerId = 99

    await pendingLogout

    expect(store.token).toBe('new-user-token')
    expect(ledger.ledgers.length).toBe(1)
    expect(ledger.currentLedgerId).toBe(99)
  }, 60000)
})
