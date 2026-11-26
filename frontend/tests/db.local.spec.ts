import { describe, it, expect, beforeEach, vi } from 'vitest'
import { initLocalDb, getDb, run, query, rowsFrom, resetLocalDb, persist, persistAsync, flushPersist, getLocalDbStatus } from '@/db/local'

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

  it('migrates legacy sync_queue to add remote_payload without data loss', async () => {
    await initLocalDb()
    // 模拟老库：重建不含 remote_payload 列的 sync_queue 并写入一条待推送项
    getDb().run('DROP TABLE sync_queue')
    getDb().run(`CREATE TABLE sync_queue (
      id            INTEGER PRIMARY KEY AUTOINCREMENT,
      table_name    TEXT NOT NULL,
      record_id     INTEGER NOT NULL,
      ledger_id     INTEGER DEFAULT 0,
      operation     TEXT NOT NULL CHECK(operation IN ('create','update','delete')),
      payload       TEXT NOT NULL,
      local_version TEXT NOT NULL,
      synced        INTEGER DEFAULT 0,
      conflict      INTEGER DEFAULT 0
    )`)
    run('INSERT INTO sync_queue (table_name, record_id, operation, payload, local_version) VALUES (?,?,?,?,?)',
      ['assets', 101, 'create', '{}', '2026-11-19T00:00:00Z'])
    persist()
    await flushPersist()

    const fresh = await reloadDb()
    await fresh.initLocalDb()
    const rows = fresh.rowsFrom(fresh.query('SELECT * FROM sync_queue WHERE record_id = 101'))
    expect(rows.length).toBe(1)
    expect(rows[0].table_name).toBe('assets')
    expect(rows[0].remote_payload).toBeNull()
    // 重复初始化幂等，不重复加列
    await fresh.initLocalDb()
    expect(fresh.rowsFrom(fresh.query('SELECT * FROM sync_queue WHERE record_id = 101')).length).toBe(1)
  })

  it('does not write plaintext b64 synchronously before encryption when WebCrypto is available', async () => {
    await initLocalDb()
    run('INSERT INTO sync_meta (key, value) VALUES (?,?)', ['sec_test', 'v1'])
    persist()
    const immediate = localStorage.getItem('minefolio_local_db')
    expect(immediate?.startsWith('b64:')).not.toBe(true)

    await flushPersist()
    const afterFlush = localStorage.getItem('minefolio_local_db')
    expect(afterFlush?.startsWith('enc:v1:')).toBe(true)
  })

  it('preserves locked backup and sets decryptFailed when enc:v1 restore fails', async () => {
    const lockedPayload = 'enc:v1:badiv:badcipher'
    localStorage.setItem('minefolio_local_db', lockedPayload)
    await initLocalDb()

    expect(localStorage.getItem('minefolio_local_db_locked_backup')).toBe(lockedPayload)
    expect(getLocalDbStatus().decryptFailed).toBe(true)
    expect(getLocalDbStatus().hasLockedBackup).toBe(true)
  })

  it('adds a missing account_id column to a legacy assets table without losing rows', async () => {
    await initLocalDb()
    getDb().run('DROP TABLE assets')
    getDb().run(`CREATE TABLE assets (
      id INTEGER PRIMARY KEY,
      category_id INTEGER NOT NULL,
      name TEXT NOT NULL,
      currency TEXT DEFAULT 'CNY',
      __deleted INTEGER DEFAULT 0
    )`)
    run('INSERT INTO assets (id, category_id, name) VALUES (?,?,?)', [101, 1, 'legacy asset'])
    persist()
    await flushPersist()

    const fresh = await reloadDb()
    await fresh.initLocalDb()
    const row = fresh.rowsFrom(fresh.query('SELECT id, name, account_id FROM assets WHERE id = 101'))[0]
    expect(row.name).toBe('legacy asset')
    expect(row.account_id).toBeNull()
  })

  it('flags quota exhaustion and emits an event when the snapshot cannot be persisted', async () => {
    await initLocalDb()
    run('INSERT INTO sync_meta (key, value) VALUES (?,?)', ['quota', '1'])
    const onQuota = vi.fn()
    window.addEventListener('minefolio:localdb-quota-exceeded', onQuota)
    // tests/setup.ts installs a MemoryStorage instance, not a Storage.prototype subclass.
    const originalSetItem = localStorage.setItem.bind(localStorage)
    vi.spyOn(localStorage, 'setItem').mockImplementation((key: string, value: string) => {
      if (key === 'minefolio_local_db') {
        throw new DOMException('quota full', 'QuotaExceededError')
      }
      return originalSetItem(key, value)
    })

    try {
      await persistAsync()
      expect(getLocalDbStatus().quotaExceeded).toBe(true)
      expect(onQuota).toHaveBeenCalledTimes(1)
    } finally {
      vi.restoreAllMocks()
      window.removeEventListener('minefolio:localdb-quota-exceeded', onQuota)
    }
  })
})
