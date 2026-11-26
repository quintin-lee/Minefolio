import { describe, it, expect, beforeEach, vi } from 'vitest'
import { initLocalDb, query, run, rowsFrom, resetLocalDb } from '@/db/local'
import { SyncEngine } from '@/sync/SyncEngine'

vi.mock('@/api/daily_expenses', () => ({ dailyExpensesApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) } }))
vi.mock('@/api/transactions', () => ({ transactionsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) } }))
vi.mock('@/api/assets', () => ({ assetsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) } }))
vi.mock('@/api/accounts', () => ({ accountsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue([]) } }))
vi.mock('@/api/categories', () => ({ categoriesApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue([]) } }))
vi.mock('@/api/tags', () => ({ tagsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue([]) } }))

import { assetsApi } from '@/api/assets'
import { dailyExpensesApi } from '@/api/daily_expenses'

const COMPLETE_EXPENSE = {
  category_id: 1, asset_id: 1, expense_type: 'expense', amount: 10,
  currency: 'CNY', expense_date: '2026-08-13',
}

beforeEach(async () => {
  vi.clearAllMocks()
  localStorage.clear()
  resetLocalDb()
  await initLocalDb()
  const engine = SyncEngine.getInstance()
  engine.init()
  engine.clearPushErrors()
})

function engine(): SyncEngine {
  return SyncEngine.getInstance()
}

describe('SyncEngine', () => {
  it('is a singleton and shares one queue', () => {
    expect(SyncEngine.getInstance()).toBe(SyncEngine.getInstance())
  })

  it('throws on unregistered table without leaving an orphan local row', () => {
    expect(() => engine().enqueue('unknown_table', 1, 'create', {})).toThrow(/未注册/)
    expect(engine().getState().queue.length).toBe(0)
    expect(rowsFrom(query("SELECT name FROM sqlite_master WHERE type = 'table' AND name = 'unknown_table'")).length).toBe(0)
  })

  it('restores pre-existing sync_queue rows from the local SQLite db', () => {
    run('INSERT INTO sync_queue (table_name, record_id, operation, payload, local_version) VALUES (?,?,?,?,?)',
      ['daily_expenses', 777, 'create', '{}', '2026-11-26T00:00:00Z'])
    engine().init()
    expect(engine().getState().queue.length).toBe(1)
    expect(engine().getState().queue[0].record_id).toBe(777)
  })

  it('marks code:1004 as conflict and keeps the queue item', async () => {
    ;(dailyExpensesApi.create as any).mockRejectedValueOnce({ response: { data: { code: 1004, message: '冲突' } } })
    engine().enqueue('daily_expenses', 101, 'create', COMPLETE_EXPENSE)
    await engine().syncNow()
    expect(engine().getConflicts().length).toBe(1)
    expect(engine().getState().queue.length).toBe(1)
    expect(engine().getPushErrors()).toEqual([])
  })

  it('treats validation codes as permanent failures: drops queue item and records pushErrors', async () => {
    ;(dailyExpensesApi.create as any).mockRejectedValueOnce({ response: { data: { code: 1003, message: '资源不存在' } } })
    engine().enqueue('daily_expenses', 102, 'create', COMPLETE_EXPENSE)
    await engine().syncNow()
    expect(engine().getState().queue.length).toBe(0)
    expect(engine().getConflicts().length).toBe(0)
    expect(engine().getPushErrors()).toEqual([
      expect.objectContaining({ table: 'daily_expenses', message: '资源不存在' }),
    ])
  })

  it('persists remote_payload across a fresh init and conflict resolution', () => {
    engine().enqueue('daily_expenses', 103, 'create', COMPLETE_EXPENSE)
    const id = engine().getState().queue[0].id
    run('UPDATE sync_queue SET conflict = 1, remote_payload = ? WHERE id = ?', [JSON.stringify({ amount: 99 }), id])

    engine().init()
    const conflict = engine().getConflicts()[0]
    expect(conflict?.remote_payload).toEqual({ amount: 99 })

    void engine().resolveConflict(id, 'discard')
    expect(engine().getConflicts().length).toBe(0)
  })

  it('sends X-Ledger-Id from the queued snapshot even after active ledger changes', async () => {
    localStorage.setItem('minefolio_active_ledger_id', '99')
    engine().enqueue('daily_expenses', 104, 'create', COMPLETE_EXPENSE, 2)
    localStorage.setItem('minefolio_active_ledger_id', '77')
    ;(dailyExpensesApi.create as any).mockResolvedValueOnce({ id: 104 })
    await engine().syncNow()
    expect(dailyExpensesApi.create).toHaveBeenCalledWith(
      expect.objectContaining({ amount: 10 }),
      { headers: { 'X-Ledger-Id': '2' } }
    )
  })

  it('guards against concurrent syncNow calls', async () => {
    let resolveCreate!: (v: unknown) => void
    ;(dailyExpensesApi.create as any).mockImplementationOnce(() => new Promise((r) => { resolveCreate = r }))
    engine().enqueue('daily_expenses', 105, 'create', COMPLETE_EXPENSE)
    const first = engine().syncNow()
    const second = engine().syncNow()
    expect(dailyExpensesApi.create).toHaveBeenCalledTimes(1)
    resolveCreate({ id: 105 })
    await Promise.all([first, second])
  })

  it('remaps only queue items after the created asset (three-arg handler)', async () => {
    const localAssetId = 1_752_120_000_000
    // 早于资产 create 的冲突项：不会被 afterQueueId 过滤掉，但应保持不变
    run(
      "INSERT INTO sync_queue (table_name, record_id, operation, payload, local_version, synced, conflict) VALUES ('daily_expenses', 1, 'create', ?, ?, 0, 1)",
      [JSON.stringify({ ...COMPLETE_EXPENSE, asset_id: localAssetId }), '2026-11-26T00:00:00Z']
    )
    run('INSERT INTO assets (id, category_id, name, currency) VALUES (?,?,?,?)', [localAssetId, 1, '占位资产', 'CNY'])
    run('INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date) VALUES (?,?,?,?,?,?,?)',
      [9001, 1, localAssetId, 'expense', 10, 'CNY', '2026-08-13'])
    engine().init()

    engine().enqueue('assets', localAssetId, 'create', { name: '占位资产', category_id: 1, currency: 'CNY' })
    engine().enqueue('daily_expenses', 9001, 'create', { ...COMPLETE_EXPENSE, asset_id: localAssetId })
    ;(assetsApi.create as any).mockResolvedValueOnce({ id: 88 })
    ;(dailyExpensesApi.create as any).mockResolvedValueOnce({ id: 9002 })

    await engine().syncNow()

    expect(rowsFrom(query('SELECT id FROM assets WHERE id = 88')).length).toBe(1)
    expect(rowsFrom(query('SELECT id FROM daily_expenses WHERE asset_id = 88')).length).toBe(1)
    expect(dailyExpensesApi.create).toHaveBeenCalledWith(expect.objectContaining({ asset_id: 88 }))
    const earlier = rowsFrom(query('SELECT payload FROM sync_queue WHERE conflict = 1 AND record_id = 1'))
    expect(JSON.parse(String(earlier[0].payload)).asset_id).toBe(localAssetId)
  })
})
