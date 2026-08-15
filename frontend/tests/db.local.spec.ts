import { describe, it, expect, beforeEach, vi } from 'vitest'
import { initLocalDb, getDb, run, query, rowsFrom, resetLocalDb, persist } from '@/db/local'

beforeEach(() => {
  resetLocalDb()
})

// 真实 App 重启语义：重置模块状态（db 置空）但保留 polyfill 的 localStorage，
// 从而验证 persist() → 重新 initLocalDb() 能从 localStorage 恢复数据。
async function reloadDb() {
  vi.resetModules()
  return await import('@/db/local')
}

describe('local db', () => {
  it('creates tables and inserts a daily_expense', async () => {
    await initLocalDb()
    run('INSERT INTO daily_expenses (id, user_id, category_id, asset_id, expense_type, amount, currency, expense_date) VALUES (?,?,?,?,?,?,?,?)',
      [1, 1, 10, 20, 'expense', 12.5, 'CNY', '2026-08-13'])
    const res = query('SELECT * FROM daily_expenses WHERE id = 1')
    const rows = rowsFrom(res)
    expect(rows.length).toBe(1)
    expect(rows[0].amount).toBe(12.5)
    expect(rows[0].__deleted).toBe(0)
  })

  it('soft-deletes without physical removal', async () => {
    await initLocalDb()
    run('INSERT INTO daily_expenses (id, user_id, category_id, asset_id, expense_type, amount, currency, expense_date) VALUES (?,?,?,?,?,?,?,?)',
      [2, 1, 10, 20, 'expense', 5, 'CNY', '2026-08-13'])
    run('UPDATE daily_expenses SET __deleted = 1, updated_at = NULL WHERE id = 2')
    const res = query('SELECT * FROM daily_expenses WHERE id = 2')
    expect(rowsFrom(res)[0].__deleted).toBe(1)
  })

  it('persists and reloads from localStorage', async () => {
    await initLocalDb()
    run('INSERT INTO sync_meta (key, value) VALUES (?,?)', ['last_sync_at', '2026-08-13T00:00:00Z'])
    persist()

    const fresh = await reloadDb()
    await fresh.initLocalDb()
    const res = fresh.query("SELECT value FROM sync_meta WHERE key = 'last_sync_at'")
    expect(fresh.rowsFrom(res)[0].value).toBe('2026-08-13T00:00:00Z')
  })
})
