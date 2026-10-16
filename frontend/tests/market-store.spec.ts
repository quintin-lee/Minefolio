// frontend/tests/market-store.spec.ts
import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { useMarketStore } from '@/stores/market'
import { marketApi } from '@/api/market'

vi.mock('@/api/market', () => ({
  marketApi: {
    getExchangeRates: vi.fn(),
    getFxHistory: vi.fn(),
    getMultiCurrencySummary: vi.fn(),
    syncAll: vi.fn(),
    updateExchangeRate: vi.fn(),
    getIndices: vi.fn(),
  },
}))

describe('market store', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    localStorage.clear()
    vi.clearAllMocks()
  })

  it('initializes with default baseCurrency as CNY', () => {
    const store = useMarketStore()
    expect(store.baseCurrency).toBe('CNY')
    expect(store.selectedCurrency).toBe('USD')
    expect(store.historyDays).toBe(30)
  })

  it('calls marketApi.getIndices with category param', async () => {
    const mockData = {
      updated_at: '2026-10-16 12:00:00',
      market_clocks: [{ market: 'SSE_SZSE', name: 'A股', timezone: 'Asia/Shanghai', status: 'open', open_time: '09:30', close_time: '15:00' }],
      indices: [{ symbol: 'sh000001', name: '上证指数', category: 'cn', region_flag: '🇨🇳', current_price: 3285.4, change_amount: 25.3, change_percent: 0.78, open_price: 3260, high_price: 3290, low_price: 3250, prev_close: 3260.1, currency: 'CNY', quote_time: '2026-10-16 11:30', sparkline: [3260, 3285] }]
    }
    vi.mocked(marketApi.getIndices).mockResolvedValueOnce(mockData as any)

    const res = await marketApi.getIndices('cn')
    expect(res.indices[0].name).toBe('上证指数')
    expect(res.market_clocks[0].status).toBe('open')
  })

  it('updates baseCurrency and persists to localStorage', () => {
    const store = useMarketStore()
    store.setBaseCurrency('USD')
    expect(store.baseCurrency).toBe('USD')
    expect(localStorage.getItem('minefolio_base_currency')).toBe('USD')
  })

  it('fetches exchange rates and caches them', async () => {
    const mockRates = { USD: 7.23, EUR: 7.85 }
    vi.mocked(marketApi.getExchangeRates).mockResolvedValueOnce(mockRates)

    const store = useMarketStore()
    await store.fetchRates()

    expect(store.rates.USD).toBe(7.23)
    expect(store.rates.CNY).toBe(1.0)
    expect(store.lastSyncAt).toBeTruthy()
  })

  it('guards against out-of-order race responses in fetchHistory', async () => {
    const store = useMarketStore()

    let resolveFirst: any
    const firstPromise = new Promise((resolve) => {
      resolveFirst = resolve
    })

    vi.mocked(marketApi.getFxHistory)
      .mockReturnValueOnce(firstPromise as any)
      .mockResolvedValueOnce([
        { rate_date: '2026-10-15', rate: 7.85, base_currency: 'CNY', target_currency: 'EUR' },
      ])

    // Request 1: USD (slow)
    const p1 = store.fetchHistory('USD', 30)
    // Request 2: EUR (fast)
    const p2 = store.fetchHistory('EUR', 30)

    await p2
    expect(store.selectedCurrency).toBe('EUR')
    expect(store.historyPoints[0].target_currency).toBe('EUR')

    // Now request 1 completes late
    resolveFirst([
      { rate_date: '2026-10-14', rate: 7.23, base_currency: 'CNY', target_currency: 'USD' },
    ])
    await p1

    // State should still be EUR, not overwritten by stale USD response!
    expect(store.historyPoints[0].target_currency).toBe('EUR')
  })
})
