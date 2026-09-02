import http from '@/utils/http'
import type { MarketSearchItem, MarketQuote, PriceHistoryItem, MarketSettings, TestProxyResult } from '@/types'

export const marketApi = {
  search: (keyword: string) =>
    http.get<MarketSearchItem[], MarketSearchItem[]>('/market/search', { params: { keyword } }),

  getQuote: (symbol: string, source?: string) =>
    http.get<MarketQuote, MarketQuote>('/market/quote', { params: { symbol, source } }),

  syncAll: () =>
    http.post<{ synced_count: number; failed_count: number }, { synced_count: number; failed_count: number }>('/market/sync'),

  syncSingle: (assetId: number) =>
    http.post<MarketQuote, MarketQuote>(`/market/sync/${assetId}`),

  getHistory: (assetId: number, limit = 90) =>
    http.get<PriceHistoryItem[], PriceHistoryItem[]>(`/market/history/${assetId}`, { params: { limit } }),

  getSettings: () =>
    http.get<MarketSettings, MarketSettings>('/market/settings'),

  updateSettings: (data: Partial<MarketSettings>) =>
    http.put<void, void>('/market/settings', data),

  testProxy: (data?: { market_proxy?: string }) =>
    http.post<TestProxyResult, TestProxyResult>('/market/test-proxy', data),

  getExchangeRates: () =>
    http.get<Record<string, number>, Record<string, number>>('/market/exchange-rates'),

  updateExchangeRate: (currency: string, rate: number) =>
    http.post<void, void>('/market/exchange-rates', { currency, rate }),

  getMultiCurrencySummary: (baseCurrency = 'CNY') =>
    http.get<import('@/types').MultiCurrencySummary, import('@/types').MultiCurrencySummary>('/reports/multi-currency-summary', {
      params: { base_currency: baseCurrency },
    }),
}

