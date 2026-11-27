import { describe, it, expect, beforeEach, vi } from 'vitest'
import { initLocalDb, query, run, rowsFrom, resetLocalDb } from '@/db/local'
import { SyncEngine } from '@/sync/SyncEngine'

vi.mock('@/api/daily_expenses', () => ({ dailyExpensesApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() } }))
vi.mock('@/api/transactions', () => ({ transactionsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() } }))
vi.mock('@/api/assets', () => ({ assetsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() } }))
vi.mock('@/api/accounts', () => ({ accountsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() } }))
vi.mock('@/api/categories', () => ({ categoriesApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() } }))
vi.mock('@/api/tags', () => ({ tagsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() } }))

import { accountsApi } from '@/api/accounts'
import { categoriesApi } from '@/api/categories'

beforeEach(async () => {
  vi.resetAllMocks()
  localStorage.clear()
  resetLocalDb()
  await initLocalDb()
  const engine = SyncEngine.getInstance()
  engine.init()
  engine.clearPushErrors()
})

describe('SyncEngine.pullRemote category tree & archived accounts', () => {
  it('flattens nested category trees and preserves existing child categories during reconciliation', async () => {
    run(
      `INSERT INTO categories (id, name, parent_id, type, updated_at, __deleted)
       VALUES (?, ?, ?, ?, ?, 0)`,
      [10, 'Child', 1, 'expense', '2026-11-26T09:00:00Z']
    )

    ;(categoriesApi.list as any).mockResolvedValueOnce([
      {
        id: 1,
        name: 'Root',
        parent_id: null,
        type: 'expense',
        updated_at: '2026-11-26T10:00:00Z',
        children: [
          {
            id: 10,
            name: 'Child',
            parent_id: 1,
            type: 'expense',
            updated_at: '2026-11-26T10:00:00Z',
            children: [
              {
                id: 100,
                name: 'Grandchild',
                parent_id: 10,
                type: 'expense',
                updated_at: '2026-11-26T10:00:00Z',
              },
            ],
          },
        ],
      },
    ])

    await SyncEngine.getInstance().pullRemote()

    const rows = rowsFrom(
      query('SELECT id, parent_id, __deleted FROM categories WHERE __deleted = 0 ORDER BY id ASC')
    )
    expect(rows.map((r) => Number(r.id))).toEqual([1, 10, 100])
    expect(rows.map((r) => (r.parent_id === null ? null : Number(r.parent_id)))).toEqual([null, 1, 10])
    expect(rows.every((r) => Number(r.__deleted) === 0)).toBe(true)
  })

  it('requests include_archived: 1 when pulling accounts', async () => {
    ;(accountsApi.list as any).mockResolvedValueOnce([
      {
        id: 5,
        name: 'Archived Card',
        institution: 'bank_card',
        archived: 1,
        updated_at: '2026-11-26T10:00:00Z',
      },
    ])

    await SyncEngine.getInstance().pullRemote()

    expect(accountsApi.list).toHaveBeenCalledWith(
      expect.objectContaining({ include_archived: 1 })
    )
    const rows = rowsFrom(query('SELECT id, archived, __deleted FROM accounts WHERE id = 5'))
    expect(rows.length).toBe(1)
    expect(Number(rows[0].archived)).toBe(1)
    expect(Number(rows[0].__deleted)).toBe(0)
  })
})
