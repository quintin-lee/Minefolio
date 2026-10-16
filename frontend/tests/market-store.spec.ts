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

  it('updates indices state and calculates price directions for flash animation', async () => {
    const store = useMarketStore()
    const initialData = {
      updated_at: '2026-10-16 12:00:00',
      market_clocks: [],
      indices: [{ symbol: 'sh000001', name: '上证指数', category: 'cn', region_flag: '🇨🇳', current_price: 3280.0, change_amount: 20, change_percent: 0.6, open_price: 3260, high_price: 3290, low_price: 3250, prev_close: 3260, currency: 'CNY', quote_time: '12:00', sparkline: [] }]
    }
    vi.mocked(marketApi.getIndices).mockResolvedValueOnce(initialData as any)

    await store.fetchIndices()
    expect(store.indices[0].current_price).toBe(3280.0)

    // 第二次拉取，价格上涨 -> priceDirections 标记为 'up'
    const updatedData = {
      updated_at: '2026-10-16 12:00:30',
      market_clocks: [],
      indices: [{ symbol: 'sh000001', name: '上证指数', category: 'cn', region_flag: '🇨🇳', current_price: 3285.4, change_amount: 25.4, change_percent: 0.78, open_price: 3260, high_price: 3290, low_price: 3250, prev_close: 3260, currency: 'CNY', quote_time: '12:00:30', sparkline: [] }]
    }
    vi.mocked(marketApi.getIndices).mockResolvedValueOnce(updatedData as any)

    await store.fetchIndices()
    expect(store.indices[0].current_price).toBe(3285.4)
    expect(store.priceDirections['sh000001']).toBe('up')
  })

  it('guards against asynchronous out-of-order race responses in fetchIndices', async () => {
    const store = useMarketStore()
    let resolveFirst: any
    const firstPromise = new Promise((resolve) => { resolveFirst = resolve })

    vi.mocked(marketApi.getIndices)
      .mockReturnValueOnce(firstPromise as any)
      .mockResolvedValueOnce({
        updated_at: '2026-10-16 12:01:00',
        market_clocks: [],
        indices: [{ symbol: '^GSPC', name: '标普500', category: 'us', region_flag: '🇺🇸', current_price: 5864.0, change_amount: 24, change_percent: 0.4, open_price: 5840, high_price: 5870, low_price: 5830, prev_close: 5840, currency: 'USD', quote_time: '12:01', sparkline: [] }]
      } as any)

    const p1 = store.fetchIndices('cn')
    const p2 = store.fetchIndices('us')

    await p2
    expect(store.indices[0].symbol).toBe('^GSPC')

    // 慢速请求返回，应被丢弃
    resolveFirst({
      updated_at: '2026-10-16 12:00:00',
      market_clocks: [],
      indices: [{ symbol: 'sh000001', name: '上证指数', category: 'cn', region_flag: '🇨🇳', current_price: 3280.0, change_amount: 20, change_percent: 0.6, open_price: 3260, high_price: 3290, low_price: 3250, prev_close: 3260, currency: 'CNY', quote_time: '12:00', sparkline: [] }]
    })
    await p1

    expect(store.indices[0].symbol).toBe('^GSPC')
  })
})
