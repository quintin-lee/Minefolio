import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { createPinia, setActivePinia } from 'pinia'

vi.mock('@/api/system', () => ({
  systemApi: {
    status: vi.fn(),
    setup: vi.fn(),
  },
}))

vi.mock('@/views-mobile/MobileLayout.vue', () => ({
  default: { template: '<router-view />' },
}))
vi.mock('@/views-mobile/DashboardMobile.vue', () => ({
  default: { template: '<div>dashboard</div>' },
}))
vi.mock('@/views-mobile/LoginMobile.vue', () => ({
  default: { template: '<div>login</div>' },
}))

import { systemApi } from '@/api/system'
import { useAuthStore } from '@/stores/auth'
import router from '@/router/mobile'

describe('mobile router offline cold start', () => {
  let originalOnLineDescriptor: PropertyDescriptor | undefined

  beforeEach(async () => {
    setActivePinia(createPinia())
    localStorage.clear()
    vi.clearAllMocks()
    originalOnLineDescriptor = Object.getOwnPropertyDescriptor(Navigator.prototype, 'onLine')

    const auth = useAuthStore()
    auth.isInitialized = true
    auth.token = ''
    await router.replace('/m/login')
    await router.isReady()
    vi.clearAllMocks()
  })

  afterEach(() => {
    if (originalOnLineDescriptor) {
      Object.defineProperty(Navigator.prototype, 'onLine', originalOnLineDescriptor)
    } else {
      delete (navigator as unknown as { onLine?: boolean }).onLine
    }
  })

  it('does not block navigation to /m/dashboard when auth.token is present and status check hangs', async () => {
    localStorage.setItem('token', 'existing-mobile-token')
    const auth = useAuthStore()
    auth.token = 'existing-mobile-token'
    auth.isInitialized = null

    ;(systemApi.status as ReturnType<typeof vi.fn>).mockImplementation(
      () => new Promise(() => {})
    )

    await router.push('/m/dashboard')
    await router.isReady()

    expect(router.currentRoute.value.path).toBe('/m/dashboard')
    expect(auth.isInitialized).toBe(true)
    expect(systemApi.status).toHaveBeenCalledWith({ timeout: 3000 })
  })

  it('redirects immediately to /m/login when navigator.onLine is false and no token', async () => {
    Object.defineProperty(Navigator.prototype, 'onLine', {
      configurable: true,
      get: () => false,
    })

    const auth = useAuthStore()
    auth.token = ''
    auth.isInitialized = null

    await router.push('/m/dashboard')
    await router.isReady()

    expect(router.currentRoute.value.path).toBe('/m/login')
    expect(auth.isInitialized).toBe(true)
    expect(systemApi.status).not.toHaveBeenCalled()
  })

  it('uses a short 2500ms timeout when online without a token', async () => {
    Object.defineProperty(Navigator.prototype, 'onLine', {
      configurable: true,
      get: () => true,
    })
    ;(systemApi.status as ReturnType<typeof vi.fn>).mockResolvedValue({
      initialized: true,
      user_count: 1,
      version: '1.5.0',
    })

    const auth = useAuthStore()
    auth.token = ''
    auth.isInitialized = null

    await router.push('/m/dashboard')
    await router.isReady()

    expect(router.currentRoute.value.path).toBe('/m/login')
    expect(systemApi.status).toHaveBeenCalledWith({ timeout: 2500 })
  })
})
