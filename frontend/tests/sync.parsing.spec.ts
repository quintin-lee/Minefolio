import { describe, it, expect, beforeEach, vi } from 'vitest'
import { initLocalDb, query, run, resetLocalDb, rowsFrom } from '@/db/local'
import { parseSyncTimestamp, SyncEngine } from '@/sync/SyncEngine'

vi.mock('@/api/daily_expenses', () => ({ dailyExpensesApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) } }))
vi.mock('@/api/transactions', () => ({ transactionsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) } }))
vi.mock('@/api/assets', () => ({ assetsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue({ list: [] }) } }))
vi.mock('@/api/accounts', () => ({ accountsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue([]) } }))
vi.mock('@/api/categories', () => ({ categoriesApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue([]) } }))
vi.mock('@/api/tags', () => ({ tagsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn().mockResolvedValue([]) } }))

import { dailyExpensesApi } from '@/api/daily_expenses'

function engine(): SyncEngine {
  return SyncEngine.getInstance()
}

beforeEach(async () => {
  vi.clearAllMocks()
  localStorage.clear()
  resetLocalDb()
  await initLocalDb()
  engine().init()
})

describe('sync timestamp parsing', () => {
  it('treats naive and space-separated timestamps as UTC', () => {
    const expected = Date.parse('2026-11-26T10:00:00Z')
    expect(parseSyncTimestamp('2026-11-26T10:00:00')).toBe(expected)
    expect(parseSyncTimestamp('2026-11-26 10:00:00')).toBe(expected)
    expect(parseSyncTimestamp('  2026-11-26T10:00:00  ')).toBe(expected)
  })

  it('keeps fractional seconds and explicit timezone offsets', () => {
    expect(parseSyncTimestamp('2026-11-26 10:00:00.123')).toBe(Date.parse('2026-11-26T10:00:00.123Z'))
    expect(parseSyncTimestamp('2026-11-26T10:00:00+08:00')).toBe(Date.parse('2026-11-26T10:00:00+08:00'))
  })

  it('maps missing or invalid values to zero', () => {
    expect(parseSyncTimestamp('')).toBe(0)
    expect(parseSyncTimestamp('   ')).toBe(0)
    expect(parseSyncTimestamp(null)).toBe(0)
    expect(parseSyncTimestamp(123)).toBe(0)
    expect(parseSyncTimestamp('not-a-date')).toBe(0)
  })
})

describe('sync payload parsing', () => {
  it('falls back to an empty payload when a queued payload is not JSON', async () => {
    ;(dailyExpensesApi.create as any).mockResolvedValueOnce({ id: 1 })
    run(
      "INSERT INTO sync_queue (table_name, record_id, operation, payload, local_version) VALUES ('daily_expenses', 1, 'create', ?, ?)",
      ['{broken', '2026-11-26T00:00:00Z']
    )
    engine().init()

    await engine().syncNow()

    expect(dailyExpensesApi.create).toHaveBeenCalledWith({})
  })

  it('tolerates malformed, empty and null remote_payload values on load', () => {
    const ids = ['{broken', '', null].map((payload, i) => {
      run(
        'INSERT INTO sync_queue (table_name, record_id, operation, payload, local_version, synced, conflict, remote_payload) VALUES (?, ?, ?, ?, ?, 0, 1, ?)',
        ['daily_expenses', 10 + i, 'create', '{}', '2026-11-26T00:00:00Z', payload]
      )
      return i
    })
    expect(ids.length).toBe(3)

    engine().init()

    const conflicts = engine().getConflicts()
    expect(conflicts.length).toBe(3)
    expect(conflicts.every((c) => c.remote_payload === undefined)).toBe(true)
    expect(rowsFrom(query('SELECT id FROM sync_queue WHERE conflict = 1')).length).toBe(3)
  })
})
