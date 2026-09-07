import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { initLocalDb, query, run, rowsFrom, resetLocalDb } from '@/db/local'
import { useSyncStore } from '@/stores/sync'

vi.mock('@/api/daily_expenses', () => ({
  dailyExpensesApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) },
}))
vi.mock('@/api/transactions', () => ({ transactionsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) } }))
vi.mock('@/api/assets', () => ({ assetsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) } }))

import { dailyExpensesApi } from '@/api/daily_expenses'
import { assetsApi } from '@/api/assets'

beforeEach(async () => {
  setActivePinia(createPinia())
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
    ;(dailyExpensesApi.create as any).mockRejectedValue({ response: { data: { code: 1003 } } })
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
})
