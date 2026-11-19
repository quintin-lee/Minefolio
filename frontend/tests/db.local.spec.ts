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

  it('migrates legacy db to add ledger_id columns to all sync tables', async () => {
    await initLocalDb()
    run('INSERT INTO assets (id, category_id, name, ledger_id) VALUES (?,?,?,?)', [101, 1, '测试资产', 2])
    run('INSERT INTO transactions (id, asset_id, transaction_type, amount, transaction_date, ledger_id) VALUES (?,?,?,?,?,?)', [201, 101, 'income', 50, '2026-11-19', 2])
    run('INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, expense_date, ledger_id) VALUES (?,?,?,?,?,?,?)', [301, 1, 101, 'expense', 15, '2026-11-19', 2])
    run('INSERT INTO sync_queue (table_name, record_id, operation, payload, local_version, ledger_id) VALUES (?,?,?,?,?,?)', ['assets', 101, 'create', '{}', '2026-11-19T00:00:00Z', 2])

    const assetRow = rowsFrom(query('SELECT ledger_id FROM assets WHERE id = 101'))[0]
    expect(assetRow.ledger_id).toBe(2)

    const txRow = rowsFrom(query('SELECT ledger_id FROM transactions WHERE id = 201'))[0]
    expect(txRow.ledger_id).toBe(2)

    const expRow = rowsFrom(query('SELECT ledger_id FROM daily_expenses WHERE id = 301'))[0]
    expect(expRow.ledger_id).toBe(2)

    const qRow = rowsFrom(query('SELECT ledger_id FROM sync_queue WHERE record_id = 101'))[0]
    expect(qRow.ledger_id).toBe(2)
  })
})
