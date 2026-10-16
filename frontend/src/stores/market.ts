// frontend/src/stores/market.ts
import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { marketApi } from '@/api/market'
import type { FxHistoryPoint, MultiCurrencySummary, GlobalIndexItem, MarketClockItem } from '@/types'

const BASE_CURRENCY_STORAGE_KEY = 'minefolio_base_currency'
const RATES_CACHE_KEY = 'minefolio_cached_rates'

function getInitialRates(): Record<string, number> {
  try {
    const cached = localStorage.getItem(RATES_CACHE_KEY)
    return cached ? JSON.parse(cached) : { CNY: 1.0, USD: 7.2345, EUR: 7.852, HKD: 0.9248, JPY: 0.04781, GBP: 9.382 }
  } catch {
    return { CNY: 1.0, USD: 7.2345, EUR: 7.852, HKD: 0.9248, JPY: 0.04781, GBP: 9.382 }
  }
}

export const useMarketStore = defineStore('market', () => {
  const baseCurrency = ref<string>(
    localStorage.getItem(BASE_CURRENCY_STORAGE_KEY) || 'CNY'
  )
  const rates = ref<Record<string, number>>(getInitialRates())

  const lastSyncAt = ref<string | null>(null)
  const isSyncing = ref(false)
  const selectedCurrency = ref<string>('USD')
  const historyDays = ref<number>(30)
  const historyPoints = ref<FxHistoryPoint[]>([])
  const isLoadingHistory = ref(false)
  const holdingsSummary = ref<MultiCurrencySummary | null>(null)
  const isLoadingHoldings = ref(false)

  // Request sequence guard against asynchronous out-of-order responses
  let historyReqSeq = 0

  function setBaseCurrency(currency: string) {
    baseCurrency.value = currency
    localStorage.setItem(BASE_CURRENCY_STORAGE_KEY, currency)
    fetchHoldingsSummary()
  }

  function setSelectedCurrency(currency: string) {
    if (selectedCurrency.value !== currency) {
      selectedCurrency.value = currency
      fetchHistory(currency, historyDays.value)
    }
  }

  function setHistoryDays(days: number) {
    historyDays.value = days
    fetchHistory(selectedCurrency.value, days)
  }

  async function fetchRates() {
    try {
      const data = await marketApi.getExchangeRates()
      if (data && typeof data === 'object') {
        rates.value = { ...data, CNY: 1.0 }
        localStorage.setItem(RATES_CACHE_KEY, JSON.stringify(rates.value))
        lastSyncAt.value = new Date().toLocaleTimeString()
      }
    } catch (err) {
      console.warn('Failed to fetch exchange rates, using cached rates:', err)
    }
  }

  async function fetchHistory(currency = selectedCurrency.value, days = historyDays.value) {
    selectedCurrency.value = currency
    const currentSeq = ++historyReqSeq
    isLoadingHistory.value = true
    try {
      const pts = await marketApi.getFxHistory(currency, days)
      if (currentSeq === historyReqSeq && Array.isArray(pts)) {
        historyPoints.value = pts
      }
    } catch (err) {
      if (currentSeq === historyReqSeq) {
        historyPoints.value = []
        console.warn('Failed to fetch FX history:', err)
      }
    } finally {
      if (currentSeq === historyReqSeq) {
        isLoadingHistory.value = false
      }
    }
  }

  async function fetchHoldingsSummary() {
    isLoadingHoldings.value = true
    try {
      const res = await marketApi.getMultiCurrencySummary(baseCurrency.value)
      holdingsSummary.value = res
    } catch (err) {
      console.warn('Failed to fetch multi-currency summary:', err)
    } finally {
      isLoadingHoldings.value = false
    }
  }

  async function syncExchangeRates() {
    try {
      const data = await marketApi.syncExchangeRates()
      if (data && typeof data === 'object') {
        rates.value = { ...data, CNY: 1.0 }
        localStorage.setItem(RATES_CACHE_KEY, JSON.stringify(rates.value))
        lastSyncAt.value = new Date().toLocaleTimeString()
        await Promise.allSettled([fetchHoldingsSummary(), fetchHistory()])
        return true
      }
    } catch (err) {
      console.warn('Failed to sync exchange rates:', err)
    }
    return false
  }

  async function syncAllQuotes() {
    isSyncing.value = true
    try {
      const [fxRes, quoteRes] = await Promise.allSettled([
        marketApi.syncExchangeRates(),
        marketApi.syncAll(),
      ])

      await Promise.allSettled([fetchRates(), fetchHoldingsSummary(), fetchHistory()])
      lastSyncAt.value = new Date().toLocaleTimeString()

      const fxOk = fxRes.status === 'fulfilled'
      const quoteOk = quoteRes.status === 'fulfilled'
      if (!fxOk && !quoteOk) {
        console.error('Market sync failed: both FX and quotes sync failed', { fxRes, quoteRes })
        return false
      }
      return true
    } catch (err) {
      console.error('Market sync failed:', err)
      return false
    } finally {
      isSyncing.value = false
    }
  }

  async function updateRate(currency: string, rate: number) {
    await marketApi.updateExchangeRate(currency, rate)
    rates.value[currency] = rate
    localStorage.setItem(RATES_CACHE_KEY, JSON.stringify(rates.value))
  }

  const indices = ref<GlobalIndexItem[]>([])
  const marketClocks = ref<MarketClockItem[]>([])
  const indicesUpdatedAt = ref<string | null>(null)
  const isLoadingIndices = ref(false)
  const autoRefreshInterval = ref<number>(60)
  const activeCategory = ref<string>('all')
  const prevPrices = ref<Record<string, number>>({})
  const priceDirections = ref<Record<string, 'up' | 'down' | 'same'>>({})
  const selectedDrawerIndex = ref<GlobalIndexItem | null>(null)
  const isDrawerOpen = ref(false)

  const isAnyMarketActive = computed(() => {
    if (!marketClocks.value || marketClocks.value.length === 0) {
      return true
    }
    return marketClocks.value.some(
      (c) => c.status === 'open' || c.status === 'pre'
    )
  })

  let indicesReqSeq = 0

  function setAutoRefreshInterval(sec: number) {
    autoRefreshInterval.value = sec
  }

  function setActiveCategory(cat: string) {
    activeCategory.value = cat
    fetchIndices(cat)
  }

  function openIndexDrawer(item: GlobalIndexItem) {
    selectedDrawerIndex.value = item
    isDrawerOpen.value = true
  }

  function closeIndexDrawer() {
    isDrawerOpen.value = false
    selectedDrawerIndex.value = null
  }

  async function fetchIndices(category = activeCategory.value, silent = false) {
    const currentSeq = ++indicesReqSeq
    if (!silent) {
      isLoadingIndices.value = true
    }
    try {
      const data = await marketApi.getIndices(category === 'all' ? undefined : category)
      if (currentSeq === indicesReqSeq && data) {
        if (Array.isArray(data.indices)) {
          const nextDirections: Record<string, 'up' | 'down' | 'same'> = {}
          const nextPrices: Record<string, number> = {}

          for (const item of data.indices) {
            const oldPrice = prevPrices.value[item.symbol]
            if (oldPrice !== undefined && oldPrice !== null) {
              if (item.current_price > oldPrice) {
                nextDirections[item.symbol] = 'up'
              } else if (item.current_price < oldPrice) {
                nextDirections[item.symbol] = 'down'
              } else {
                nextDirections[item.symbol] = 'same'
              }
            } else {
              nextDirections[item.symbol] = 'same'
            }
            nextPrices[item.symbol] = item.current_price
          }

          priceDirections.value = nextDirections
          prevPrices.value = nextPrices
          indices.value = data.indices
        }

        if (Array.isArray(data.market_clocks)) {
          marketClocks.value = data.market_clocks
        }
        indicesUpdatedAt.value = data.updated_at || new Date().toLocaleTimeString()
      }
    } catch (err) {
      console.warn('Failed to fetch indices:', err)
    } finally {
      if (currentSeq === indicesReqSeq) {
        isLoadingIndices.value = false
      }
    }
  }

  async function init() {
    await Promise.allSettled([fetchRates(), fetchHoldingsSummary(), fetchHistory()])
  }

  return {
    baseCurrency,
    rates,
    lastSyncAt,
    isSyncing,
    selectedCurrency,
    historyDays,
    historyPoints,
    isLoadingHistory,
    holdingsSummary,
    isLoadingHoldings,
    indices,
    marketClocks,
    indicesUpdatedAt,
    isLoadingIndices,
    autoRefreshInterval,
    isAnyMarketActive,
    activeCategory,
    prevPrices,
    priceDirections,
    selectedDrawerIndex,
    isDrawerOpen,
    setBaseCurrency,
    setSelectedCurrency,
    setHistoryDays,
    fetchRates,
    fetchHistory,
    fetchHoldingsSummary,
    syncExchangeRates,
    syncAllQuotes,
    updateRate,
    setAutoRefreshInterval,
    setActiveCategory,
    openIndexDrawer,
    closeIndexDrawer,
    fetchIndices,
    init,
  }
})
