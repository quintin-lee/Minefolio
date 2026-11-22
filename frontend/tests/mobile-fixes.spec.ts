import { describe, it, expect, vi, beforeEach } from 'vitest'
import { createPinia, setActivePinia } from 'pinia'
import { ElMessage } from 'element-plus'
import { isIncomeTransaction } from '@/utils/format'
import { isNetworkError, notifyMobileReadError } from '@/utils/http'

vi.mock('@/views-mobile/MobileLayout.vue', () => ({ default: { template: '<div />' } }))
vi.mock('@/views-mobile/DashboardMobile.vue', () => ({ default: { template: '<div />' } }))
vi.mock('@/views-mobile/LoginMobile.vue', () => ({ default: { template: '<div />' } }))

describe('Task 6 Mobile Fixes', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    localStorage.clear()
    vi.restoreAllMocks()
  })

  it('isIncomeTransaction prioritizes direction over transaction_type fallback', () => {
    expect(isIncomeTransaction({ direction: 'in', transaction_type: 'withdraw' })).toBe(true)
    expect(isIncomeTransaction({ direction: 'out', transaction_type: 'income' })).toBe(false)
    expect(isIncomeTransaction({ transaction_type: 'dividend' })).toBe(true)
    expect(isIncomeTransaction({ transaction_type: 'buy' })).toBe(false)
  })

  it('notifyMobileReadError displays warning on network failure and suppresses HTTP response errors', () => {
    const errSpy = vi.spyOn(ElMessage, 'error').mockImplementation((() => ({ close: () => {} })) as any)

    expect(isNetworkError(new Error('Network Error'))).toBe(true)
    expect(isNetworkError({ response: { status: 500 } })).toBe(false)

    notifyMobileReadError({ response: { status: 500 } }, '报表数据')
    expect(errSpy).not.toHaveBeenCalled()

    notifyMobileReadError(new Error('Network Error'), '持仓数据')
    expect(errSpy).toHaveBeenCalledTimes(1)
  })

  it('mobile router redirects authenticated users from /m/login to /m/dashboard', async () => {
    const { useAuthStore } = await import('@/stores/auth')
    const auth = useAuthStore()
    auth.token = 'valid-jwt-token'
    auth.isInitialized = true

    const mobileRouter = (await import('@/router/mobile')).default
    await mobileRouter.push('/m/login')
    await mobileRouter.isReady()

    expect(mobileRouter.currentRoute.value.path).toBe('/m/dashboard')
  })
})
