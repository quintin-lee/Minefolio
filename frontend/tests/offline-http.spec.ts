import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { initLocalDb, query, run, rowsFrom, resetLocalDb } from '@/db/local'
import { useSyncStore } from '@/stores/sync'
import { offlineApi } from '@/utils/offline-http'

vi.mock('@/utils/http', async (importOriginal) => {
  const actual = (await importOriginal()) as any
  return {
    ...actual,
    default: {
      ...actual.default,
      request: vi.fn(),
    },
  }
})
import http from '@/utils/http'

beforeEach(async () => {
  setActivePinia(createPinia())
  vi.clearAllMocks()
  resetLocalDb()
  await initLocalDb()
  await useSyncStore().init()
})

describe('offline-http', () => {
  it('writes to local db and queues when http fails', async () => {
    ;(http.request as any).mockRejectedValue(new Error('Network Error'))
    const res = await offlineApi.post('/daily-expenses', { amount: 50, expense_type: 'expense', category_id: 1, asset_id: 1, currency: 'CNY', expense_date: '2026-08-13' })
    expect(res.offline).toBe(true)
    const rows = rowsFrom(query('SELECT * FROM daily_expenses WHERE amount = 50'))
    expect(rows.length).toBe(1)
    expect(rows[0].__deleted).toBe(0)
    const q = rowsFrom(query('SELECT * FROM sync_queue WHERE synced = 0'))
    expect(q.length).toBe(1)
  })

  it('keeps local row id identical to the queued record id on offline create', async () => {
    ;(http.request as any).mockRejectedValue(new Error('Network Error'))
    const res = await offlineApi.post('/daily-expenses', { amount: 50, expense_type: 'expense', category_id: 1, asset_id: 1, currency: 'CNY', expense_date: '2026-08-13' })
    expect(res.offline).toBe(true)
    expect(typeof res.id).toBe('number')
    // 本地行必须带上与同步队列一致的显式 id，否则同步重映射会失配
    const rows = rowsFrom(query('SELECT * FROM daily_expenses WHERE id = ?', [res.id as number]))
    expect(rows.length).toBe(1)
    const q = rowsFrom(query('SELECT * FROM sync_queue WHERE synced = 0'))
    expect(q.length).toBe(1)
    expect(Number(q[0].record_id)).toBe(res.id)
    // 队列载荷不应把客户端占位 id 发给服务端
    expect(JSON.parse(String(q[0].payload))).not.toHaveProperty('id')
  })

  it('updates the existing row on offline edit without duplicating it', async () => {
    ;(http.request as any).mockRejectedValue(new Error('Network Error'))
    const res = await offlineApi.post('/daily-expenses', { amount: 50, expense_type: 'expense', category_id: 1, asset_id: 1, currency: 'CNY', expense_date: '2026-08-13' })
    await offlineApi.put(`/daily-expenses/${res.id}`, { amount: 99, expense_type: 'expense', category_id: 1, asset_id: 1, currency: 'CNY', expense_date: '2026-08-13' })
    const rows = rowsFrom(query('SELECT * FROM daily_expenses'))
    expect(rows.length).toBe(1) // 编辑不能产生重复行
    expect(rows[0].amount).toBe(99)
    expect(rows[0].__deleted).toBe(0)
    const q = rowsFrom(query('SELECT * FROM sync_queue WHERE synced = 0 ORDER BY id ASC'))
    expect(q.length).toBe(2) // create + update 两条待推送
    expect(Number(q[0].record_id)).toBe(res.id)
    expect(Number(q[1].record_id)).toBe(res.id)
  })

  it('soft-deletes locally on delete failure', async () => {
    ;(http.request as any).mockRejectedValue(new Error('Network Error'))
    run('INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date) VALUES (?,?,?,?,?,?,?)',
      [7, 1, 1, 'expense', 5, 'CNY', '2026-08-13'])
    await offlineApi.delete('/daily-expenses/7')
    const rows = rowsFrom(query('SELECT * FROM daily_expenses WHERE id = 7'))
    expect(rows[0].__deleted).toBe(1)
  })

  it('strips unknown API fields against TABLE_COLUMNS during offline write while preserving queue payload', async () => {
    ;(http.request as any).mockRejectedValue(new Error('Network Error'))
    const res = await offlineApi.post('/daily-expenses', {
      amount: 66,
      expense_type: 'expense',
      category_id: 1,
      asset_id: 1,
      currency: 'CNY',
      expense_date: '2026-08-13',
      tags: [1, 2],
      nonexistent_col: 'should_not_fail_sqlite',
    })
    expect(res.offline).toBe(true)
    const rows = rowsFrom(query('SELECT * FROM daily_expenses WHERE id = ?', [res.id as number]))
    expect(rows.length).toBe(1)
    expect(rows[0].amount).toBe(66)
    const q = rowsFrom(query('SELECT * FROM sync_queue WHERE synced = 0'))
    expect(JSON.parse(String(q[0].payload)).tags).toEqual([1, 2])
  })

  it('offline-writes categories and tags through registered paths', async () => {
    ;(http.request as any).mockRejectedValue(new Error('Network Error'))
    const categoryRes = await offlineApi.post('/categories', { name: '离线分类', type: 'expense', currency: 'CNY' })
    const tagRes = await offlineApi.post('/tags', { name: '离线标签' })
    expect(categoryRes.offline).toBe(true)
    expect(tagRes.offline).toBe(true)
    expect(rowsFrom(query('SELECT * FROM categories WHERE name = ?', ['离线分类'])).length).toBe(1)
    expect(rowsFrom(query('SELECT * FROM tags WHERE name = ?', ['离线标签'])).length).toBe(1)
    const queued = rowsFrom(query('SELECT table_name FROM sync_queue WHERE synced = 0 ORDER BY id ASC'))
    expect(queued.map((q) => q.table_name)).toEqual(['categories', 'tags'])
  })

  it('rethrows HTTP 4xx/5xx and business errors instead of falling back to offline writes', async () => {
    ;(http.request as any).mockRejectedValue({
      response: { status: 403, data: { code: 1004, message: 'Forbidden' } },
    })
    await expect(
      offlineApi.post('/daily-expenses', {
        amount: 88,
        expense_type: 'expense',
        category_id: 1,
        asset_id: 1,
        currency: 'CNY',
        expense_date: '2026-08-13',
      })
    ).rejects.toMatchObject({ response: { status: 403 } })

    expect(rowsFrom(query('SELECT * FROM daily_expenses')).length).toBe(0)
    expect(rowsFrom(query('SELECT * FROM sync_queue')).length).toBe(0)
  })
})
