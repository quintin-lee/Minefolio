import { describe, it, expect, beforeEach, vi } from 'vitest'
import { initLocalDb, getDb, query, run, rowsFrom, resetLocalDb, persist, flushPersist } from '@/db/local'
import { SyncEngine } from '@/sync/SyncEngine'

const httpMock = vi.hoisted(() => ({
  get: vi.fn(),
  post: vi.fn(),
  put: vi.fn(),
  delete: vi.fn(),
}))

vi.mock('@/utils/http', () => ({
  default: httpMock,
}))

import { categoriesApi } from '@/api/categories'
import { tagsApi } from '@/api/tags'

const COMPLETE_EXPENSE = {
  category_id: 1,
  asset_id: 1,
  expense_type: 'expense',
  amount: 10,
  currency: 'CNY',
  expense_date: '2026-11-26',
}

beforeEach(async () => {
  vi.resetAllMocks()
  httpMock.get.mockResolvedValue([])
  httpMock.post.mockResolvedValue({ id: 999 })
  httpMock.put.mockResolvedValue(undefined)
  httpMock.delete.mockResolvedValue(undefined)
  localStorage.clear()
  resetLocalDb()
  await initLocalDb()
  const engine = SyncEngine.getInstance()
  engine.init()
  engine.clearPushErrors()
})

describe('SyncEngine conflict discard, validation cleanup, and ledger scoping', () => {
  it('forwards X-Ledger-Id config on categoriesApi and tagsApi create/update/delete', async () => {
    const cfg = { headers: { 'X-Ledger-Id': '42' } }
    const catPayload = { name: '餐饮', type: 'expense' as const, currency: 'CNY' }
    await categoriesApi.create(catPayload, cfg)
    expect(httpMock.post).toHaveBeenCalledWith('/categories', catPayload, cfg)

    await categoriesApi.update(10, catPayload, cfg)
    expect(httpMock.put).toHaveBeenCalledWith('/categories/10', catPayload, cfg)

    await categoriesApi.delete(10, cfg)
    expect(httpMock.delete).toHaveBeenCalledWith('/categories/10', cfg)

    const tagPayload = { name: '差旅', color: '#ff0000' }
    await tagsApi.create(tagPayload, cfg)
    expect(httpMock.post).toHaveBeenCalledWith('/tags', tagPayload, cfg)

    await tagsApi.update(20, tagPayload, cfg)
    expect(httpMock.put).toHaveBeenCalledWith('/tags/20', tagPayload, cfg)

    await tagsApi.delete(20, cfg)
    expect(httpMock.delete).toHaveBeenCalledWith('/tags/20', cfg)
  })

  it('persists categories.ledger_id and preserves other ledgers during pullRemote reconciliation', async () => {
    run(
      'INSERT INTO categories (id, ledger_id, name, type, currency, updated_at, __deleted) VALUES (?, ?, ?, ?, ?, ?, 0)',
      [10, 1, '账本1分类', 'expense', 'CNY', '2026-11-26T10:00:00Z']
    )
    run(
      'INSERT INTO categories (id, ledger_id, name, type, currency, updated_at, __deleted) VALUES (?, ?, ?, ?, ?, ?, 0)',
      [11, 2, '账本2分类', 'expense', 'CNY', '2026-11-26T10:00:00Z']
    )
    localStorage.setItem('minefolio_active_ledger_id', '1')
    httpMock.get.mockImplementation(async (url: string) => {
      if (url === '/categories') return []
      if (url === '/accounts' || url === '/tags') return []
      return { list: [], total: 0 }
    })

    await SyncEngine.getInstance().pullRemote()

    const remaining = rowsFrom(query('SELECT id, ledger_id FROM categories ORDER BY id'))
    expect(remaining).toEqual([{ id: 11, ledger_id: 2 }])
  })

  it('migrates legacy local categories table to add ledger_id column without data loss', async () => {
    getDb().run('DROP TABLE categories')
    getDb().run(`CREATE TABLE categories (
      id         INTEGER PRIMARY KEY,
      user_id    INTEGER DEFAULT 0,
      name       TEXT NOT NULL,
      parent_id  INTEGER,
      type       TEXT NOT NULL DEFAULT 'asset',
      asset_type TEXT DEFAULT 'cash',
      currency   TEXT DEFAULT 'CNY',
      icon       TEXT,
      sort_order INTEGER DEFAULT 0,
      created_at TEXT,
      updated_at TEXT,
      __deleted  INTEGER DEFAULT 0
    )`)
    run("INSERT INTO categories (id, name, type) VALUES (1, '旧分类', 'expense')")
    persist()
    await flushPersist()

    vi.resetModules()
    const fresh = await import('@/db/local')
    await fresh.initLocalDb()
    const rows = fresh.rowsFrom(fresh.query('SELECT id, name, ledger_id FROM categories WHERE id = 1'))
    expect(rows.length).toBe(1)
    expect(rows[0].name).toBe('旧分类')
    expect(rows[0].ledger_id).toBe(0)
  })

  it('restores remote_payload and resets __deleted = 0 when discarding an update conflict', async () => {
    run(
      'INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date, note, updated_at, __deleted) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0)',
      [501, 1, 1, 'expense', 25, 'CNY', '2026-11-26', '远端备注', '2026-11-26T10:00:00Z']
    )
    const engine = SyncEngine.getInstance()
    engine.enqueue('daily_expenses', 501, 'update', {
      ...COMPLETE_EXPENSE,
      amount: 88,
      note: '本地修改',
    })
    const queueId = engine.getState().queue[0].id
    const remotePayload = {
      id: 501,
      ...COMPLETE_EXPENSE,
      amount: 25,
      note: '远端备注',
      updated_at: '2026-11-26T11:00:00Z',
    }
    run('UPDATE sync_queue SET conflict = 1, remote_payload = ? WHERE id = ?', [
      JSON.stringify(remotePayload),
      queueId,
    ])
    engine.init()

    await engine.resolveConflict(queueId, 'discard')

    expect(engine.getConflicts().length).toBe(0)
    expect(engine.getState().queue.length).toBe(0)
    const row = rowsFrom(query('SELECT amount, note, __deleted FROM daily_expenses WHERE id = 501'))[0]
    expect(row.amount).toBe(25)
    expect(row.note).toBe('远端备注')
    expect(row.__deleted).toBe(0)
  })

  it('restores remote_payload and resets __deleted = 0 when discarding a delete conflict', async () => {
    run(
      'INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date, note, updated_at, __deleted) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0)',
      [502, 1, 1, 'expense', 30, 'CNY', '2026-11-26', '远端存在', '2026-11-26T10:00:00Z']
    )
    const engine = SyncEngine.getInstance()
    engine.enqueue('daily_expenses', 502, 'delete', {})
    expect(rowsFrom(query('SELECT __deleted FROM daily_expenses WHERE id = 502'))[0].__deleted).toBe(1)

    const queueId = engine.getState().queue[0].id
    const remotePayload = {
      id: 502,
      ...COMPLETE_EXPENSE,
      amount: 45,
      note: '远端更新',
      updated_at: '2026-11-26T11:00:00Z',
    }
    run('UPDATE sync_queue SET conflict = 1, remote_payload = ? WHERE id = ?', [
      JSON.stringify(remotePayload),
      queueId,
    ])
    engine.init()

    await engine.resolveConflict(queueId, 'discard')

    expect(engine.getConflicts().length).toBe(0)
    const row = rowsFrom(query('SELECT amount, note, __deleted FROM daily_expenses WHERE id = 502'))[0]
    expect(row.amount).toBe(45)
    expect(row.note).toBe('远端更新')
    expect(row.__deleted).toBe(0)
  })

  it('resets __deleted = 0 when discarding a delete conflict without remote_payload', async () => {
    run(
      'INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date, updated_at, __deleted) VALUES (?, ?, ?, ?, ?, ?, ?, ?, 0)',
      [504, 1, 1, 'expense', 12, 'CNY', '2026-11-26', '2026-11-26T10:00:00Z']
    )
    const engine = SyncEngine.getInstance()
    engine.enqueue('daily_expenses', 504, 'delete', {})
    const queueId = engine.getState().queue[0].id
    run('UPDATE sync_queue SET conflict = 1, remote_payload = NULL WHERE id = ?', [queueId])
    engine.init()

    await engine.resolveConflict(queueId, 'discard')

    const row = rowsFrom(query('SELECT __deleted FROM daily_expenses WHERE id = 504'))[0]
    expect(row.__deleted).toBe(0)
  })

  it('removes local placeholder row when discarding a create conflict', async () => {
    const localId = 1_752_200_000_001
    const engine = SyncEngine.getInstance()
    engine.enqueue('daily_expenses', localId, 'create', COMPLETE_EXPENSE)
    expect(rowsFrom(query('SELECT id FROM daily_expenses WHERE id = ?', [localId])).length).toBe(1)

    const queueId = engine.getState().queue[0].id
    run('UPDATE sync_queue SET conflict = 1 WHERE id = ?', [queueId])
    engine.init()

    await engine.resolveConflict(queueId, 'discard')

    expect(engine.getConflicts().length).toBe(0)
    expect(rowsFrom(query('SELECT id FROM daily_expenses WHERE id = ?', [localId])).length).toBe(0)
  })

  it('deletes local placeholder row when pushLocal drops a create item on code 1002', async () => {
    const localId = 1_752_200_000_002
    const engine = SyncEngine.getInstance()
    engine.enqueue('daily_expenses', localId, 'create', COMPLETE_EXPENSE)
    expect(rowsFrom(query('SELECT id FROM daily_expenses WHERE id = ?', [localId])).length).toBe(1)

    httpMock.post.mockRejectedValueOnce({
      response: { data: { code: 1002, message: '无权限操作该账本' } },
    })

    await engine.pushLocal()

    expect(engine.getState().queue.length).toBe(0)
    expect(engine.getPushErrors()).toEqual([
      expect.objectContaining({ table: 'daily_expenses', message: '无权限操作该账本' }),
    ])
    expect(rowsFrom(query('SELECT id FROM daily_expenses WHERE id = ?', [localId])).length).toBe(0)
  })

  it('marks dependent queued items as conflict when a create fails instead of cascading drops', async () => {
    const tempAssetId = -9001
    const tempExpenseId = -9002
    const engine = SyncEngine.getInstance()
    engine.enqueue('assets', tempAssetId, 'create', {
      name: 'Temp Wallet',
      category_id: 1,
      currency: 'CNY',
    })
    engine.enqueue('daily_expenses', tempExpenseId, 'create', {
      ...COMPLETE_EXPENSE,
      asset_id: tempAssetId,
    })

    httpMock.post.mockRejectedValueOnce({
      response: { data: { code: 1002, message: '资产参数错误' } },
    })

    await engine.pushLocal()

    // Root invalid create is removed, while dependent expense is flagged as conflict rather than pushed with tempAssetId
    expect(httpMock.post).toHaveBeenCalledTimes(1)
    expect(engine.getConflicts().length).toBe(1)
    expect(engine.getConflicts()[0].table_name).toBe('daily_expenses')
    expect(engine.getConflicts()[0].record_id).toBe(tempExpenseId)
  })
})
