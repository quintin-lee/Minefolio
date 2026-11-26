import { describe, it, expect, beforeEach, vi } from 'vitest'
import { initLocalDb, query, resetLocalDb, rowsFrom, flushPersist, run } from '@/db/local'
import { aggregateMonthlyReport } from '@/composables/mobile/useLocalMonthlyReport'
import { deriveLocalHoldings } from '@/composables/mobile/useLocalHoldings'
import { loadViewCache, saveViewCache } from '@/composables/mobile/useViewCache'

async function reloadDb() {
  vi.resetModules()
  const fresh = await import('@/db/local')
  await fresh.initLocalDb()
  return fresh
}

beforeEach(async () => {
  localStorage.clear()
  localStorage.setItem('minefolio_active_ledger_id', '1')
  resetLocalDb()
  await initLocalDb()
})

function seedCategories() {
  run('INSERT INTO categories (id, name, type, asset_type, currency) VALUES (?,?,?,?,?)', [10, '餐饮', 'expense', 'cash', 'CNY'])
  run('INSERT INTO categories (id, name, type, asset_type, currency) VALUES (?,?,?,?,?)', [11, '工资', 'income', 'cash', 'CNY'])
  run('INSERT INTO categories (id, name, type, asset_type, currency) VALUES (?,?,?,?,?)', [12, '股票', 'asset', 'stock', 'CNY'])
  run('INSERT INTO categories (id, name, type, asset_type, currency) VALUES (?,?,?,?,?)', [13, '现金', 'asset', 'cash', 'CNY'])
}

describe('local-first views', () => {
  it('aggregates monthly report with categories, tags, soft-delete and ledger filters', () => {
    seedCategories()
    run('INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date, ledger_id) VALUES (?,?,?,?,?,?,?,?)', [1, 10, 1, 'expense', 30, 'CNY', '2026-11-01', 1])
    run('INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date, ledger_id) VALUES (?,?,?,?,?,?,?,?)', [2, 10, 1, 'expense', 20, 'CNY', '2026-11-02', 1])
    run('INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date, ledger_id) VALUES (?,?,?,?,?,?,?,?)', [3, 11, 1, 'income', 100, 'CNY', '2026-11-03', 1])
    run('INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date, ledger_id) VALUES (?,?,?,?,?,?,?,?)', [4, 10, 1, 'expense', 999, 'CNY', '2026-11-04', 2])
    run('INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date, __deleted) VALUES (?,?,?,?,?,?,?,?)', [5, 10, 1, 'expense', 888, 'CNY', '2026-11-05', 1])
    run('INSERT INTO tags (id, name, color) VALUES (?,?,?)', [20, '午餐', '#fff'])
    run('INSERT INTO expense_tags (expense_id, tag_id, __deleted) VALUES (?,?,0)', [1, 20])
    run('INSERT INTO expense_tags (expense_id, tag_id, __deleted) VALUES (?,?,1)', [2, 20])

    const report = aggregateMonthlyReport(2026, 11)
    expect(report.total_income).toBe(100)
    expect(report.total_expense).toBe(50)
    expect(report.balance).toBe(50)
    expect(report.by_category).toEqual([expect.objectContaining({ name: '餐饮', amount: 50, pct: 100 })])
    expect(report.by_tag).toEqual([expect.objectContaining({ tag_name: '午餐', amount: 30, count: 1 })])
    expect(report.daily_breakdown).toEqual([
      { date: '2026-11-01', income: 0, expense: 30 },
      { date: '2026-11-02', income: 0, expense: 20 },
      { date: '2026-11-03', income: 100, expense: 0 },
    ])
  })

  it('derives only investment holdings with approximate current value', () => {
    seedCategories()
    run('INSERT INTO assets (id, category_id, name, currency, quantity, net_value, cost_basis, ledger_id) VALUES (?,?,?,?,?,?,?,?)', [1, 12, '股票', 'CNY', 10, 12, 100, 1])
    run('INSERT INTO assets (id, category_id, name, currency, quantity, net_value, cost_basis, ledger_id) VALUES (?,?,?,?,?,?,?,?)', [2, 13, '现金', 'CNY', 100, 1, 100, 1])

    const { report, approximate } = deriveLocalHoldings()
    expect(approximate).toBe(true)
    expect(report.holdings.length).toBe(1)
    expect(report.holdings[0]).toEqual(expect.objectContaining({
      asset_id: 1, current_value: 120, floating_pnl: 20, floating_pct: 20, realized_pnl: 0,
    }))
    expect(report.summary.total_market_value).toBe(120)
    expect(report.summary.total_realized_pnl).toBe(0)
  })

  it('keeps view cache across reload and out of sync_queue', async () => {
    saveViewCache('dca_plans', { list: [1], total: 1 })
    await flushPersist()

    const fresh = await reloadDb()
    const freshCache = await import('@/composables/mobile/useViewCache')
    const cached = freshCache.loadViewCache<{ list: number[] }>('dca_plans')
    expect(cached.data?.list).toEqual([1])
    expect(cached.updatedAt).toBeTruthy()
    expect(fresh.rowsFrom(fresh.query('SELECT * FROM sync_queue')).length).toBe(0)
  })
})
