import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { initLocalDb, query, run, rowsFrom, resetLocalDb } from '@/db/local'
import { useSyncStore, parseSyncTimestamp } from '@/stores/sync'

vi.mock('@/api/daily_expenses', () => ({
  dailyExpensesApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) },
}))
vi.mock('@/api/transactions', () => ({ transactionsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) } }))
vi.mock('@/api/assets', () => ({ assetsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) } }))
vi.mock('@/api/accounts', () => ({ accountsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue([]) } }))
vi.mock('@/api/categories', () => ({ categoriesApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue([]) } }))
vi.mock('@/api/tags', () => ({ tagsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue([]) } }))

import { dailyExpensesApi } from '@/api/daily_expenses'
import { assetsApi } from '@/api/assets'
import { categoriesApi } from '@/api/categories'
import { tagsApi } from '@/api/tags'

beforeEach(async () => {
  setActivePinia(createPinia())
  localStorage.clear()
  vi.clearAllMocks()
  resetLocalDb()
  await initLocalDb()
})

describe('sync store', () => {
  it('enqueues a create op and pushes it on syncNow', async () => {
    const store = useSyncStore()
    store.init()
    store.enqueue('daily_expenses', 99, 'create', { amount: 10, expense_type: 'expense' })
    expect(store.pendingCount).toBe(1)
    await store.syncNow()
    expect(dailyExpensesApi.create).toHaveBeenCalled()
    expect(store.pendingCount).toBe(0)
  })

  it('marks conflict when server rejects', async () => {
    ;(dailyExpensesApi.create as any).mockRejectedValue({ response: { data: { code: 1004 } } })
    const store = useSyncStore()
    store.init()
    store.enqueue('daily_expenses', 100, 'create', { amount: 1 })
    await store.syncNow()
    expect(store.pendingCount).toBe(1) // 仍待处理但已标记
  })

  it('remaps dependent rows and queued payloads after an offline-created asset syncs', async () => {
    const store = useSyncStore()
    store.init()
    const localAssetId = 1752000000000 // 模拟 writeLocal 生成的客户端占位 id
    // 模拟离线期间产生的本地状态：资产行 + 引用该资产的支出行 + 两条待推送队列项
    run('INSERT INTO assets (id, user_id, category_id, name, currency) VALUES (?,?,?,?,?)', [localAssetId, 1, 1, '测试账户', 'CNY'])
    run('INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date) VALUES (?,?,?,?,?,?,?)',
      [9001, 1, localAssetId, 'expense', 10, 'CNY', '2026-08-13'])
    store.enqueue('assets', localAssetId, 'create', { user_id: 1, category_id: 1, name: '测试账户', currency: 'CNY' })
    store.enqueue('daily_expenses', 9001, 'create', { category_id: 1, asset_id: localAssetId, expense_type: 'expense', amount: 10, currency: 'CNY', expense_date: '2026-08-13' })

    ;(assetsApi.create as any).mockResolvedValue({ id: 88 })
    ;(dailyExpensesApi.create as any).mockResolvedValue({ id: 9002 })
    await store.syncNow()

    // 资产本地行被重映射为服务端 id
    expect(rowsFrom(query('SELECT * FROM assets WHERE id = 88')).length).toBe(1)
    expect(rowsFrom(query('SELECT * FROM assets WHERE id = ?', [localAssetId])).length).toBe(0)
    // 依赖支出行被重映射
    expect(rowsFrom(query('SELECT * FROM daily_expenses WHERE asset_id = 88')).length).toBe(1)
    // 依赖队列 payload 使用服务端 id 推送，而不是过期的占位 id
    expect(dailyExpensesApi.create).toHaveBeenCalledWith(expect.objectContaining({ asset_id: 88 }))
    expect(store.pendingCount).toBe(0)
  })

  it('pushes an offline-created record after a queued update already remapped its id', async () => {
    const store = useSyncStore()
    store.init()
    const localId = 1752111111111
    run('INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date) VALUES (?,?,?,?,?,?,?)',
      [localId, 1, 1, 'expense', 10, 'CNY', '2026-08-13'])
    store.enqueue('daily_expenses', localId, 'create', { category_id: 1, asset_id: 1, expense_type: 'expense', amount: 10, currency: 'CNY', expense_date: '2026-08-13' })
    store.enqueue('daily_expenses', localId, 'update', { category_id: 1, asset_id: 1, expense_type: 'expense', amount: 20, currency: 'CNY', expense_date: '2026-08-13' })

    ;(dailyExpensesApi.create as any).mockResolvedValue({ id: 301 })
    ;(dailyExpensesApi.update as any).mockResolvedValue(undefined)
    await store.syncNow()

    // create 先推：本地行与后续 update 队列项的 record_id 一起重映射到 301
    expect(rowsFrom(query('SELECT * FROM daily_expenses WHERE id = 301')).length).toBe(1)
    expect(dailyExpensesApi.update).toHaveBeenCalledWith(301, expect.objectContaining({ amount: 20 }))
    expect(store.pendingCount).toBe(0)
  })

  it('reset clears queued ops and local db so next account starts clean', async () => {
    const store = useSyncStore()
    store.init()
    store.enqueue('daily_expenses', 55, 'create', { amount: 5 })
    expect(store.pendingCount).toBe(1)
    store.reset()
    expect(store.pendingCount).toBe(0)
    expect(store.queue).toEqual([])
    expect(store.lastSyncAt).toBeNull()
    // 本地库已关闭丢弃：直接查抛未初始化
    expect(() => query('SELECT * FROM sync_queue')).toThrow()
    // 下次登录 init() 重建空库
    await initLocalDb()
    expect(rowsFrom(query('SELECT * FROM sync_queue')).length).toBe(0)
  })

  it('enqueues with active ledger_id and saves to sync_queue.ledger_id', async () => {
    localStorage.setItem('minefolio_active_ledger_id', '42')
    const store = useSyncStore()
    store.init()
    store.enqueue('daily_expenses', 101, 'create', { amount: 50 })
    const res = query('SELECT ledger_id FROM sync_queue WHERE record_id = 101')
    expect(rowsFrom(res)[0]?.ledger_id).toBe(42)
  })

  it('injects X-Ledger-Id header matching item.ledger_id in pushLocal even if active ledger differs', async () => {
    localStorage.setItem('minefolio_active_ledger_id', '99')
    const store = useSyncStore()
    store.init()
    store.enqueue('daily_expenses', 102, 'create', { amount: 100 }, 2)
    ;(dailyExpensesApi.create as any).mockResolvedValue({ id: 102 })
    await store.syncNow()
    expect(dailyExpensesApi.create).toHaveBeenCalledWith(
      expect.objectContaining({ amount: 100 }),
      { headers: { 'X-Ledger-Id': '2' } }
    )
  })

  it('pullRemote fetches categories and tags along with other entities', async () => {
    ;(categoriesApi.list as any).mockResolvedValue([
      { id: 10, name: '餐饮', type: 'expense', updated_at: '2026-08-01T00:00:00Z' }
    ])
    ;(tagsApi.list as any).mockResolvedValue([
      { id: 20, name: '工作餐', updated_at: '2026-08-01T00:00:00Z' }
    ])
    const store = useSyncStore()
    store.init()
    await store.pullRemote()
    expect(categoriesApi.list).toHaveBeenCalled()
    expect(tagsApi.list).toHaveBeenCalled()
    expect(rowsFrom(query('SELECT * FROM categories WHERE id = 10')).length).toBe(1)
    expect(rowsFrom(query('SELECT * FROM tags WHERE id = 20')).length).toBe(1)
  })

  it('reconciles remote deletions during pullRemote while preserving pending local items', async () => {
    const store = useSyncStore()
    store.init()
    // 1. 本地已有远端同步过的支出(id=11,12)及一条尚未推送成功的本地待推记录(id=13)
    run('INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date, __deleted) VALUES (?,?,?,?,?,?,?,0)',
      [11, 1, 1, 'expense', 10, 'CNY', '2026-08-10'])
    run('INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date, __deleted) VALUES (?,?,?,?,?,?,?,0)',
      [12, 1, 1, 'expense', 20, 'CNY', '2026-08-11'])
    run('INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date, __deleted) VALUES (?,?,?,?,?,?,?,0)',
      [13, 1, 1, 'expense', 30, 'CNY', '2026-08-12'])
    store.enqueue('daily_expenses', 13, 'create', { amount: 30 })

    // 远端列表只剩 id=11（说明 id=12 已在服务端被删）；pending id=13 尚未入库
    ;(dailyExpensesApi.list as any).mockResolvedValue({
      list: [{ id: 11, category_id: 1, asset_id: 1, expense_type: 'expense', amount: 10, currency: 'CNY', expense_date: '2026-08-10', updated_at: '2026-08-10T00:00:00Z' }],
      total: 1,
    })

    await store.pullRemote()

    expect(rowsFrom(query('SELECT * FROM daily_expenses WHERE id = 11')).length).toBe(1)
    expect(rowsFrom(query('SELECT * FROM daily_expenses WHERE id = 12')).length).toBe(0)
    expect(rowsFrom(query('SELECT * FROM daily_expenses WHERE id = 13')).length).toBe(1)
  })

  it('skips conflicted items on subsequent syncNow calls and supports retry/discard', async () => {
    ;(dailyExpensesApi.create as any).mockRejectedValueOnce({ response: { data: { code: 1004 } } })
    const store = useSyncStore()
    store.init()
    store.enqueue('daily_expenses', 200, 'create', { amount: 42 })

    await store.syncNow()
    expect(store.conflicts.length).toBe(1)
    expect(store.activePendingCount).toBe(0)
    expect(dailyExpensesApi.create).toHaveBeenCalledTimes(1)

    // Subsequent syncNow must NOT keep retrying the conflicted item automatically
    await store.syncNow()
    expect(dailyExpensesApi.create).toHaveBeenCalledTimes(1)

    // Manual retry clears conflict and pushes again
    ;(dailyExpensesApi.create as any).mockResolvedValueOnce({ id: 201 })
    await store.resolveConflictRetry(store.conflicts[0]!.id)
    expect(dailyExpensesApi.create).toHaveBeenCalledTimes(2)
    expect(store.conflicts.length).toBe(0)
    expect(store.pendingCount).toBe(0)

    // Enqueue another failing op and discard it manually
    ;(dailyExpensesApi.create as any).mockRejectedValueOnce({ response: { data: { code: 1004 } } })
    store.enqueue('daily_expenses', 202, 'create', { amount: 9 })
    await store.syncNow()
    expect(store.conflicts.length).toBe(1)
    store.resolveConflictDiscard(store.conflicts[0]!.id)
    expect(store.conflicts.length).toBe(0)
    expect(store.pendingCount).toBe(0)
  })

  it('parses naive UTC timestamps consistently with Z-suffixed ISO strings', () => {
    const naive = parseSyncTimestamp('2026-11-21 12:00:00')
    const isoZ = parseSyncTimestamp('2026-11-21T12:00:00Z')
    expect(naive).toBe(isoZ)
  })
})
