import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { initLocalDb, query, rowsFrom, resetLocalDb } from '@/db/local'
import { useSyncStore } from '@/stores/sync'
import { useAuthStore } from '@/stores/auth'
import { useMarketStore } from '@/stores/market'
import mobileRouter from '@/router/mobile'

vi.mock('@/utils/http', async (importOriginal) => {
  const actual = (await importOriginal()) as any
  return {
    ...actual,
    default: {
      ...actual.default,
      request: vi.fn(),
    },
  }
})

import http from '@/utils/http'
import { offlineApi } from '@/utils/offline-http'

describe('frontend audit phase 4 fixes', () => {
  beforeEach(async () => {
    setActivePinia(createPinia())
    localStorage.clear()
    vi.clearAllMocks()
    resetLocalDb()
    await initLocalDb()
    await useSyncStore().init()
  })

  it('does not route sub-action endpoints like /daily-expenses/import into offline SQLite writes', async () => {
    vi.mocked(http.request).mockRejectedValueOnce(new Error('Network Error'))
    await expect(offlineApi.post('/daily-expenses/import', { csv: 'a,b' })).rejects.toThrow('Network Error')
    expect(rowsFrom(query('SELECT * FROM daily_expenses'))).toHaveLength(0)
    expect(rowsFrom(query('SELECT * FROM sync_queue'))).toHaveLength(0)
  })

  it('clears minefolio_pinned_sessions and resets marketStore on logout', async () => {
    localStorage.setItem('minefolio_pinned_sessions', JSON.stringify(['sess-1']))
    const market = useMarketStore()
    market.holdingsSummary = { base_currency: 'USD', total_in_base: 100, currencies: [] }
    market.historyPoints = [{ rate_date: '2026-01-01', rate: 7.2, base_currency: 'CNY', target_currency: 'USD' }]

    const auth = useAuthStore()
    await auth.logout()

    expect(localStorage.getItem('minefolio_pinned_sessions')).toBeNull()
    expect(market.holdingsSummary).toBeNull()
    expect(market.historyPoints).toEqual([])
  })

  it('registers /oauth/callback on the mobile router as a public route', () => {
    const oauthRoute = mobileRouter.getRoutes().find((r) => r.path === '/oauth/callback')
    expect(oauthRoute).toBeTruthy()
    expect(oauthRoute?.meta.requiresAuth).toBe(false)
  })
})
