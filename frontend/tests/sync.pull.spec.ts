import { describe, it, expect, beforeEach, vi } from 'vitest'
import { initLocalDb, query, run, rowsFrom, resetLocalDb } from '@/db/local'
import { SyncEngine } from '@/sync/SyncEngine'

vi.mock('@/api/daily_expenses', () => ({ dailyExpensesApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() } }))
vi.mock('@/api/transactions', () => ({ transactionsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() } }))
vi.mock('@/api/assets', () => ({ assetsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() } }))
vi.mock('@/api/accounts', () => ({ accountsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() } }))
vi.mock('@/api/categories', () => ({ categoriesApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() } }))
vi.mock('@/api/tags', () => ({ tagsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() } }))

import { dailyExpensesApi } from '@/api/daily_expenses'

const EXPENSE_COLS = 'id, category_id, asset_id, expense_type, amount, currency, expense_date, updated_at'
const COMPLETE_EXPENSE = {
  category_id: 1, asset_id: 1, expense_type: 'expense',
  amount: 10, currency: 'CNY', expense_date: '2026-11-26',
}

function remoteRow(id: number, amount: number, updatedAt: string) {
  return { ...COMPLETE_EXPENSE, id, amount, updated_at: updatedAt }
}

function insertExpense(id: number, amount: number, updatedAt: string) {
  run(
    `INSERT INTO daily_expenses (${EXPENSE_COLS}) VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
    [id, 1, 1, 'expense', amount, 'CNY', '2026-11-26', updatedAt]
  )
}

beforeEach(async () => {
  vi.resetAllMocks()
  localStorage.clear()
  resetLocalDb()
  await initLocalDb()
  const engine = SyncEngine.getInstance()
  engine.init()
  engine.clearPushErrors()
})

describe('SyncEngine.pullRemote', () => {
  it('stops paginating once the reported total is reached', async () => {
    const rows = Array.from({ length: 500 }, (_, i) =>
      remoteRow(i + 1, i + 1, '2026-11-26T10:00:00Z')
    )
    ;(dailyExpensesApi.list as any).mockResolvedValueOnce({ list: rows, total: 500 })

    await SyncEngine.getInstance().syncNow()

    expect(dailyExpensesApi.list).toHaveBeenCalledTimes(1)
    expect(dailyExpensesApi.list).toHaveBeenCalledWith({ page: 1, page_size: 500 })
    expect(rowsFrom(query('SELECT id FROM daily_expenses')).length).toBe(500)
  })

  it('fetches the next page while the server reports more rows', async () => {
    const first = Array.from({ length: 500 }, (_, i) =>
      remoteRow(i + 1, 1, '2026-11-26T10:00:00Z')
    )
    ;(dailyExpensesApi.list as any)
      .mockResolvedValueOnce({ list: first, total: 501 })
      .mockResolvedValueOnce({ list: [remoteRow(501, 2, '2026-11-26T10:00:00Z')], total: 501 })

    await SyncEngine.getInstance().syncNow()

    expect(dailyExpensesApi.list).toHaveBeenCalledTimes(2)
    expect(dailyExpensesApi.list).toHaveBeenLastCalledWith({ page: 2, page_size: 500 })
    expect(rowsFrom(query('SELECT amount FROM daily_expenses WHERE id = 501'))[0].amount).toBe(2)
  })

  it('keeps a locally newer row instead of overwriting it with an older remote row', async () => {
    insertExpense(1, 10, '2026-11-26T12:00:00Z')
    ;(dailyExpensesApi.list as any).mockResolvedValueOnce({
      list: [remoteRow(1, 99, '2026-11-26T10:00:00Z')],
      total: 1,
    })

    await SyncEngine.getInstance().syncNow()

    expect(rowsFrom(query('SELECT amount FROM daily_expenses WHERE id = 1'))[0].amount).toBe(10)
  })

  it('marks a conflict when the remote row wins over an unsynced local change', async () => {
    insertExpense(1, 10, '2026-11-26T10:00:00Z')
    SyncEngine.getInstance().enqueue('daily_expenses', 1, 'update', COMPLETE_EXPENSE)
    ;(dailyExpensesApi.update as any).mockRejectedValueOnce({ response: { status: 429 } })
    ;(dailyExpensesApi.list as any).mockResolvedValueOnce({
      list: [remoteRow(1, 99, '2026-11-26T11:00:00Z')],
      total: 1,
    })

    await SyncEngine.getInstance().syncNow()

    const conflicts = SyncEngine.getInstance().getConflicts()
    expect(conflicts.length).toBe(1)
    expect(conflicts[0].remote_payload?.amount).toBe(99)
  })

  it('deletes local rows missing from a fully fetched remote list but keeps seen rows', async () => {
    insertExpense(1, 10, '2026-11-26T10:00:00Z')
    insertExpense(2, 20, '2026-11-26T10:00:00Z')
    ;(dailyExpensesApi.list as any).mockResolvedValueOnce({
      list: [remoteRow(2, 20, '2026-11-26T10:00:00Z')],
      total: 1,
    })

    await SyncEngine.getInstance().syncNow()

    const ids = rowsFrom(query('SELECT id FROM daily_expenses')).map((r) => Number(r.id))
    expect(ids).toEqual([2])
  })
})
