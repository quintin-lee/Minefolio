import { describe, it, expect, beforeEach, vi } from 'vitest'
import { initLocalDb, resetLocalDb } from '@/db/local'
import { SyncEngine } from '@/sync/SyncEngine'

vi.mock('@/api/daily_expenses', () => ({ dailyExpensesApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) } }))
vi.mock('@/api/transactions', () => ({ transactionsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) } }))
vi.mock('@/api/assets', () => ({ assetsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) } }))
vi.mock('@/api/accounts', () => ({ accountsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue([]) } }))
vi.mock('@/api/categories', () => ({ categoriesApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue([]) } }))
vi.mock('@/api/tags', () => ({ tagsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue([]) } }))

import { dailyExpensesApi } from '@/api/daily_expenses'

const COMPLETE_EXPENSE = {
  category_id: 1, asset_id: 1, expense_type: 'expense',
  amount: 10, currency: 'CNY', expense_date: '2026-11-26',
}

function engine(): SyncEngine {
  return SyncEngine.getInstance()
}

beforeEach(async () => {
  vi.clearAllMocks()
  localStorage.clear()
  resetLocalDb()
  await initLocalDb()
  engine().init()
  engine().clearPushErrors()
  engine().setAuthFailHandler(null)
})

describe('SyncEngine failure semantics', () => {
  it('retrying a conflict re-pushes the item and clears it on success', async () => {
    ;(dailyExpensesApi.create as any)
      .mockRejectedValueOnce({ response: { data: { code: 1004, message: '冲突' } } })
      .mockResolvedValueOnce({ id: 201 })
    engine().enqueue('daily_expenses', 201, 'create', COMPLETE_EXPENSE)
    await engine().syncNow()
    expect(engine().getConflicts().length).toBe(1)

    await engine().resolveConflict(engine().getConflicts()[0].id, 'retry')

    expect(engine().getConflicts().length).toBe(0)
    expect(engine().getState().queue.length).toBe(0)
  })

  it('invokes the auth-fail handler on code 1001 and keeps the queued item', async () => {
    const onAuthFail = vi.fn()
    engine().setAuthFailHandler(onAuthFail)
    ;(dailyExpensesApi.create as any).mockRejectedValueOnce({ response: { data: { code: 1001, message: '登录失效' } } })
    engine().enqueue('daily_expenses', 202, 'create', COMPLETE_EXPENSE)

    await engine().syncNow()

    expect(onAuthFail).toHaveBeenCalled()
    expect(engine().getState().queue.length).toBe(1)
  })

  it.each([429, 500])('keeps the item unsynced on HTTP %i and retries it on the next cycle', async (status) => {
    ;(dailyExpensesApi.create as any)
      .mockRejectedValueOnce({ response: { status, data: { message: 'transient' } } })
      .mockResolvedValueOnce({ id: 203 })
    engine().enqueue('daily_expenses', 203, 'create', COMPLETE_EXPENSE)

    await engine().syncNow()
    const item = engine().getState().queue[0]
    expect(item.synced).toBe(0)
    expect(item.conflict).toBe(0)

    await engine().syncNow()
    expect(engine().getState().queue.length).toBe(0)
  })

  it('keeps rapid consecutive enqueues distinct and drains both in one cycle', async () => {
    engine().enqueue('daily_expenses', 301, 'create', COMPLETE_EXPENSE)
    engine().enqueue('daily_expenses', 302, 'create', COMPLETE_EXPENSE)

    const queue = engine().getState().queue
    expect(queue.length).toBe(2)
    expect(new Set(queue.map((q) => q.id)).size).toBe(2)

    ;(dailyExpensesApi.create as any)
      .mockResolvedValueOnce({ id: 999 })
      .mockResolvedValueOnce({ id: 1000 })
    await engine().syncNow()
    expect(engine().getState().queue.length).toBe(0)
  })
})
