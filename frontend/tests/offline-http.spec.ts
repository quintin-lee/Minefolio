import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { initLocalDb, query, run, rowsFrom, resetLocalDb } from '@/db/local'
import { useSyncStore } from '@/stores/sync'
import { offlineApi } from '@/utils/offline-http'

vi.mock('@/utils/http', () => ({ default: vi.fn() }))
import http from '@/utils/http'

beforeEach(async () => {
  setActivePinia(createPinia())
  resetLocalDb()
  await initLocalDb()
  await useSyncStore().init()
})

describe('offline-http', () => {
  it('writes to local db and queues when http fails', async () => {
    ;(http as any).mockRejectedValue(new Error('Network Error'))
    const res = await offlineApi.post('/daily-expenses', { amount: 50, expense_type: 'expense', category_id: 1, asset_id: 1, currency: 'CNY', expense_date: '2026-08-13' })
    expect(res.offline).toBe(true)
    const rows = rowsFrom(query('SELECT * FROM daily_expenses WHERE amount = 50'))
    expect(rows.length).toBe(1)
    expect(rows[0].__deleted).toBe(0)
    const q = rowsFrom(query('SELECT * FROM sync_queue WHERE synced = 0'))
    expect(q.length).toBe(1)
  })

  it('soft-deletes locally on delete failure', async () => {
    ;(http as any).mockRejectedValue(new Error('Network Error'))
    run('INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date) VALUES (?,?,?,?,?,?,?)',
      [7, 1, 1, 'expense', 5, 'CNY', '2026-08-13'])
    await offlineApi.delete('/daily-expenses/7')
    const rows = rowsFrom(query('SELECT * FROM daily_expenses WHERE id = 7'))
    expect(rows[0].__deleted).toBe(1)
  })
})
