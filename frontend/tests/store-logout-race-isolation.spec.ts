import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'

vi.mock('@/api/categories', () => ({
  categoriesApi: {
    list: vi.fn(),
    children: vi.fn(),
  },
}))

vi.mock('@/api/ai', () => ({
  listSessions: vi.fn(),
  createSession: vi.fn(),
  deleteSession: vi.fn(),
  getSession: vi.fn(),
  updateSession: vi.fn(),
  getMessages: vi.fn(),
  chatStream: vi.fn(),
  getModels: vi.fn(),
  getSettings: vi.fn(),
  getWorkflows: vi.fn(),
  runWorkflowStream: vi.fn(),
}))

vi.mock('@/api/ledgers', () => ({
  ledgerApi: {
    list: vi.fn(),
  },
}))

vi.mock('@/api/market', () => ({
  marketApi: {
    getExchangeRates: vi.fn(),
    getFxHistory: vi.fn(),
    getMultiCurrencySummary: vi.fn(),
    syncAll: vi.fn(),
    syncExchangeRates: vi.fn(),
    updateExchangeRate: vi.fn(),
    getIndices: vi.fn(),
  },
}))

vi.mock('@/api/accounts', () => ({ accountsApi: { list: vi.fn() } }))
vi.mock('@/api/assets', () => ({ assetsApi: { list: vi.fn() } }))
vi.mock('@/api/reports', () => ({ reportsApi: { holdings: vi.fn() } }))

import { categoriesApi } from '@/api/categories'
import { listSessions, createSession, chatStream, runWorkflowStream } from '@/api/ai'
import { ledgerApi } from '@/api/ledgers'
import { marketApi } from '@/api/market'
import { accountsApi } from '@/api/accounts'
import { assetsApi } from '@/api/assets'
import { reportsApi } from '@/api/reports'

import { useCategoryStore } from '@/stores/category'
import { useChatStore } from '@/stores/chat'
import { useLedgerStore } from '@/stores/ledger'
import { useMarketStore } from '@/stores/market'
import { useAuthStore } from '@/stores/auth'
import { sharedMdCache, linkedMdCache } from '@/utils/chat-md-cache'
import { useFinancialEntities, resetFinancialEntities } from '@/composables/useFinancialEntities'

function deferred<T>() {
  let resolve!: (v: T) => void
  let reject!: (e?: unknown) => void
  const promise = new Promise<T>((res, rej) => {
    resolve = res
    reject = rej
  })
  return { promise, resolve, reject }
}

describe('store logout race isolation (async epoch guards)', () => {
  beforeEach(() => {
    localStorage.clear()
    setActivePinia(createPinia())
    resetFinancialEntities()
    vi.clearAllMocks()
  })

  it('category store: discards stale loadCategories result when reset() or invalidate() runs mid-flight', async () => {
    const d1 = deferred<any[]>()
    ;(categoriesApi.list as any).mockReturnValueOnce(d1.promise)

    const store = useCategoryStore()
    const loadPromise = store.loadCategories()

    store.reset()
    expect(store.allNodes).toEqual([])
    expect(store.loaded).toBe(false)
    expect(store.loading).toBe(false)
    expect(localStorage.getItem('minefolio_cache_categories')).toBeNull()

    d1.resolve([
      { id: 1, name: '旧账号分类', type: 'expense', parent_id: null, sort_order: 0, created_at: '' },
    ])
    await loadPromise

    expect(store.allNodes).toEqual([])
    expect(store.loaded).toBe(false)
    expect(localStorage.getItem('minefolio_cache_categories')).toBeNull()

    // invalidate() mid-flight also discards stale inflightLoad and lets a fresh load proceed
    const d2 = deferred<any[]>()
    const d3 = deferred<any[]>()
    ;(categoriesApi.list as any)
      .mockReturnValueOnce(d2.promise)
      .mockReturnValueOnce(d3.promise)

    const p2 = store.loadCategories()
    store.invalidate()
    const p3 = store.loadCategories()

    d3.resolve([
      { id: 2, name: '新账号分类', type: 'income', parent_id: null, sort_order: 0, created_at: '' },
    ])
    await p3

    d2.resolve([
      { id: 1, name: '过期分类', type: 'expense', parent_id: null, sort_order: 0, created_at: '' },
    ])
    await p2

    expect(categoriesApi.list).toHaveBeenCalledTimes(3)
    expect(store.allNodes.map((n) => n.name)).toEqual(['新账号分类'])
  })

  it('chat store: discards stale fetchSessions result after resetState()', async () => {
    const d = deferred<{ list: any[] }>()
    ;(listSessions as any).mockReturnValueOnce(d.promise)

    const store = useChatStore()
    const p = store.fetchSessions()

    store.resetState()
    expect(store.sessions).toEqual([])

    d.resolve({
      list: [
        { id: 101, user_id: 1, title: '旧账号秘密会话', model: 'gpt-4o', provider: 'openai', created_at: '', updated_at: '' },
      ],
    })
    await p

    expect(store.sessions).toEqual([])
  })

  it('chat store: aborts doSendMessage, doRunWorkflow, and stageWorkflow when resetState() runs during createNewSession()', async () => {
    const store = useChatStore()

    // 1. sendMessage / doSendMessage
    const dSend = deferred<any>()
    ;(createSession as any).mockReturnValueOnce(dSend.promise)
    const sendPromise = store.sendMessage('旧账号消息')
    store.resetState()
    dSend.resolve({ data: { id: 201, user_id: 1, title: '旧会话' } })
    await sendPromise

    expect(store.sessions).toEqual([])
    expect(store.currentSessionId).toBeNull()
    expect(store.messages).toEqual([])
    expect(store.isStreaming).toBe(false)
    expect(chatStream).not.toHaveBeenCalled()

    // 2. runWorkflow / doRunWorkflow
    const dWf = deferred<any>()
    ;(createSession as any).mockReturnValueOnce(dWf.promise)
    store.workflows = [{ id: 'wf_test', title: '测试工作流', description: '', icon: '', step_count: 1, steps: [] }] as any
    const wfPromise = store.runWorkflow('wf_test')
    store.resetState()
    dWf.resolve({ data: { id: 202, user_id: 1, title: '旧工作流会话' } })
    await wfPromise

    expect(store.sessions).toEqual([])
    expect(store.currentSessionId).toBeNull()
    expect(store.messages).toEqual([])
    expect(store.activeWorkflow).toBeNull()
    expect(store.isStreaming).toBe(false)
    expect(runWorkflowStream).not.toHaveBeenCalled()

    // 3. stageWorkflow
    const dStage = deferred<any>()
    ;(createSession as any).mockReturnValueOnce(dStage.promise)
    store.workflows = [{ id: 'wf_test', title: '测试工作流', description: '', icon: '', step_count: 1, steps: [] }] as any
    const stagePromise = store.stageWorkflow('wf_test')
    store.resetState()
    dStage.resolve({ data: { id: 203, user_id: 1, title: '旧暂存会话' } })
    await stagePromise

    expect(store.sessions).toEqual([])
    expect(store.currentSessionId).toBeNull()
    expect(store.messages).toEqual([])
  })

  it('ledger store: discards stale fetchLedgers result and does not repopulate localStorage after reset()', async () => {
    const d = deferred<any[]>()
    ;(ledgerApi.list as any).mockReturnValueOnce(d.promise)

    const store = useLedgerStore()
    localStorage.setItem('minefolio_active_ledger_id', '99')
    const p = store.fetchLedgers()

    store.reset()
    expect(store.ledgers).toEqual([])
    expect(store.currentLedgerId).toBeNull()
    expect(store.loading).toBe(false)
    expect(localStorage.getItem('minefolio_active_ledger_id')).toBeNull()

    d.resolve([
      { id: 99, name: '旧账号账本', currency: 'CNY', is_default: true, my_role: 'owner' },
    ])
    await p

    expect(store.ledgers).toEqual([])
    expect(store.currentLedgerId).toBeNull()
    expect(localStorage.getItem('minefolio_active_ledger_id')).toBeNull()
  })

  it('market store: reset() clears prevPrices/priceDirections/rates/baseCurrency and discards stale fetchRates/syncExchangeRates', async () => {
    const store = useMarketStore()
    store.setBaseCurrency('USD')
    store.prevPrices = { '^GSPC': 5000 }
    store.priceDirections = { '^GSPC': 'up' }
    store.rates = { CNY: 1.0, USD: 9.99, CUSTOM: 1.23 }
    localStorage.setItem('minefolio_cached_rates', JSON.stringify(store.rates))
    localStorage.setItem('minefolio_cached_indices', JSON.stringify([{ symbol: '^GSPC' }]))
    localStorage.setItem('minefolio_cached_clocks', JSON.stringify([{ market: 'US' }]))

    const dRates = deferred<Record<string, number>>()
    ;(marketApi.getExchangeRates as any).mockReturnValueOnce(dRates.promise)
    const pRates = store.fetchRates()

    store.reset()

    expect(store.prevPrices).toEqual({})
    expect(store.priceDirections).toEqual({})
    expect(store.baseCurrency).toBe('CNY')
    expect(store.rates).not.toHaveProperty('CUSTOM')
    expect(store.rates.USD).not.toBe(9.99)
    expect(localStorage.getItem('minefolio_base_currency')).toBeNull()
    expect(localStorage.getItem('minefolio_cached_rates')).toBeNull()
    expect(localStorage.getItem('minefolio_cached_indices')).toBeNull()
    expect(localStorage.getItem('minefolio_cached_clocks')).toBeNull()

    dRates.resolve({ USD: 8.88, STALE: 3.21 })
    await pRates

    expect(store.rates).not.toHaveProperty('STALE')
    expect(store.rates.USD).not.toBe(8.88)
    expect(store.lastSyncAt).toBeNull()
    expect(localStorage.getItem('minefolio_cached_rates')).toBeNull()

    // Also guard syncExchangeRates across reset()
    const dSync = deferred<Record<string, number>>()
    ;(marketApi.syncExchangeRates as any).mockReturnValueOnce(dSync.promise)
    const pSync = store.syncExchangeRates()
    store.reset()
    dSync.resolve({ USD: 7.77, STALE2: 4.56 })
    const syncOk = await pSync

    expect(syncOk).toBe(false)
    expect(store.rates).not.toHaveProperty('STALE2')
    expect(store.rates.USD).not.toBe(7.77)
    expect(localStorage.getItem('minefolio_cached_rates')).toBeNull()
  })

  it('market store: stale syncAllQuotes() settling after reset() does not clear session B isSyncing', async () => {
    const store = useMarketStore()

    // Session A starts syncAllQuotes()
    const dFxA = deferred<Record<string, number>>()
    const dAllA = deferred<any>()
    ;(marketApi.syncExchangeRates as any).mockReturnValueOnce(dFxA.promise)
    ;(marketApi.syncAll as any).mockReturnValueOnce(dAllA.promise)

    const pA = store.syncAllQuotes()
    expect(store.isSyncing).toBe(true)

    // Session A logs out -> reset()
    store.reset()
    expect(store.isSyncing).toBe(false)

    // Session B logs in and starts a fresh syncAllQuotes()
    const dFxB = deferred<Record<string, number>>()
    const dAllB = deferred<any>()
    const dRatesB = deferred<Record<string, number>>()
    const dHoldB = deferred<any>()
    const dHistB = deferred<any[]>()
    ;(marketApi.syncExchangeRates as any).mockReturnValueOnce(dFxB.promise)
    ;(marketApi.syncAll as any).mockReturnValueOnce(dAllB.promise)
    ;(marketApi.getExchangeRates as any).mockReturnValueOnce(dRatesB.promise)
    ;(marketApi.getMultiCurrencySummary as any).mockReturnValueOnce(dHoldB.promise)
    ;(marketApi.getFxHistory as any).mockReturnValueOnce(dHistB.promise)

    const pB = store.syncAllQuotes()
    expect(store.isSyncing).toBe(true)

    // Session A's requests settle late -> must be discarded and must NOT clear session B's isSyncing
    dFxA.resolve({ USD: 9.99 })
    dAllA.resolve({ synced: 1 })
    const resA = await pA

    expect(resA).toBe(false)
    expect(store.isSyncing).toBe(true)
    expect(store.lastSyncAt).toBeNull()

    // Session B settles normally
    dFxB.resolve({ USD: 7.25 })
    dAllB.resolve({ synced: 2 })
    await Promise.resolve()
    dRatesB.resolve({ USD: 7.25 })
    dHoldB.resolve({ total_cny: 1000 })
    dHistB.resolve([])
    const resB = await pB

    expect(resB).toBe(true)
    expect(store.isSyncing).toBe(false)
    expect(store.lastSyncAt).not.toBeNull()
  })

  it('useFinancialEntities: discards stale loadEntities after resetFinancialEntities() and lets new epoch load proceed', async () => {
    const dAcc1 = deferred<any[]>()
    const dAsset1 = deferred<any>()
    const dHold1 = deferred<any>()
    ;(accountsApi.list as any).mockReturnValueOnce(dAcc1.promise)
    ;(assetsApi.list as any).mockReturnValueOnce(dAsset1.promise)
    ;(reportsApi.holdings as any).mockReturnValueOnce(dHold1.promise)

    const { loadEntities, getEntityDetail, linkFinancialEntities } = useFinancialEntities()
    const p1 = loadEntities()

    // User logs out mid-flight
    resetFinancialEntities()

    // New user logs in and starts loadEntities() immediately while old request is still pending
    const dAcc2 = deferred<any[]>()
    const dAsset2 = deferred<any>()
    const dHold2 = deferred<any>()
    ;(accountsApi.list as any).mockReturnValueOnce(dAcc2.promise)
    ;(assetsApi.list as any).mockReturnValueOnce(dAsset2.promise)
    ;(reportsApi.holdings as any).mockReturnValueOnce(dHold2.promise)

    const p2 = loadEntities()

    // New user's request resolves first
    dAcc2.resolve([{ id: 2, name: '新用户招行卡', institution: 'bank_card' }])
    dAsset2.resolve({ list: [{ id: 20, name: '新用户腾讯', asset_type: 'stock', currency: 'HKD', current_value: 5000 }], total: 1 })
    dHold2.resolve({ holdings: [] })
    await p2

    // Old user's stale request resolves late -> must be discarded
    dAcc1.resolve([{ id: 1, name: '旧用户私密卡', institution: 'bank_card' }])
    dAsset1.resolve({ list: [{ id: 10, name: '旧用户茅台', asset_type: 'stock', currency: 'CNY', current_value: 9999 }], total: 1 })
    dHold1.resolve({ holdings: [] })
    await p1

    expect(accountsApi.list).toHaveBeenCalledTimes(2)
    expect(getEntityDetail('account', 2)?.name).toBe('新用户招行卡')
    expect(getEntityDetail('asset', 20)?.name).toBe('新用户腾讯')
    expect(getEntityDetail('account', 1)).toBeNull()
    expect(getEntityDetail('asset', 10)).toBeNull()
    expect(linkFinancialEntities('<p>旧用户茅台 新用户腾讯</p>')).not.toContain('旧用户茅台</span>')
    expect(linkFinancialEntities('<p>旧用户茅台 新用户腾讯</p>')).toContain('data-entity-id="20"')
  })

  it('auth store: logout() clears chat markdown caches (sharedMdCache and linkedMdCache)', async () => {
    sharedMdCache.set('raw', '<p>raw</p>')
    linkedMdCache.set('1::raw', '<p><span class="mf-entity-badge">raw</span></p>')

    const authStore = useAuthStore()
    authStore.token = 'tok-123'
    await authStore.logout()

    expect(sharedMdCache.size).toBe(0)
    expect(linkedMdCache.size).toBe(0)
  }, 60000)
})
