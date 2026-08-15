import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { initLocalDb, resetLocalDb } from '@/db/local'
import { useSyncStore } from '@/stores/sync'

vi.mock('@/api/daily_expenses', () => ({
  dailyExpensesApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) },
}))
vi.mock('@/api/transactions', () => ({ transactionsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) } }))
vi.mock('@/api/assets', () => ({ assetsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) } }))

import { dailyExpensesApi } from '@/api/daily_expenses'

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
})
