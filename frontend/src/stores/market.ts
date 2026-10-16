// frontend/src/stores/market.ts
import { defineStore } from 'pinia'
import { ref } from 'vue'
import { marketApi } from '@/api/market'
import type { FxHistoryPoint, MultiCurrencySummary } from '@/types'

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

  async function syncAllQuotes() {
    isSyncing.value = true
    try {
      await marketApi.syncAll()
      await Promise.allSettled([fetchRates(), fetchHoldingsSummary(), fetchHistory()])
      lastSyncAt.value = new Date().toLocaleTimeString()
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
    setBaseCurrency,
    setSelectedCurrency,
    setHistoryDays,
    fetchRates,
    fetchHistory,
    fetchHoldingsSummary,
    syncAllQuotes,
    updateRate,
    init,
  }
})
