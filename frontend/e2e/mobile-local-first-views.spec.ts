/**
 * 移动端本地优先数据层端到端验证
 * 在真实浏览器 + sql.js 环境中验证本地月报聚合、持仓派生与视图缓存，
 * 不依赖登录/导航时序，避免 CI 上的竞态。
 */
import { test, expect } from '@playwright/test'

const BASE = 'http://localhost:5173'

test('本地报表聚合、持仓派生与视图缓存在浏览器中可用', async ({ page }) => {
  await page.goto(`${BASE}/index.mobile.html`)
  await page.waitForTimeout(4000)

  const result = await page.evaluate(async () => {
    const { initLocalDb, run, persist, flushPersist } = await import('/src/db/local.ts')
    const { aggregateMonthlyReport } = await import('/src/composables/mobile/useLocalMonthlyReport.ts')
    const { deriveLocalHoldings } = await import('/src/composables/mobile/useLocalHoldings.ts')
    const { saveViewCache, loadViewCache } = await import('/src/composables/mobile/useViewCache.ts')

    await initLocalDb()
    localStorage.setItem('minefolio_active_ledger_id', '1')
    run('INSERT OR REPLACE INTO categories (id, name, type, asset_type, currency) VALUES (1, ?, ?, ?, ?)', ['餐饮', 'expense', 'cash', 'CNY'])
    run('INSERT OR REPLACE INTO categories (id, name, type, asset_type, currency) VALUES (2, ?, ?, ?, ?)', ['股票', 'asset', 'stock', 'CNY'])
    run('INSERT OR REPLACE INTO categories (id, name, type, asset_type, currency) VALUES (3, ?, ?, ?, ?)', ['现金', 'asset', 'cash', 'CNY'])
    run('INSERT OR REPLACE INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date, ledger_id) VALUES (?,?,?,?,?,?,?,?)', [1, 1, 1, 'expense', 42, 'CNY', '2026-11-10', 1])
    run('INSERT OR REPLACE INTO tags (id, name, color) VALUES (?,?,?)', [5, '午餐', '#fff'])
    run('INSERT OR REPLACE INTO expense_tags (expense_id, tag_id, __deleted) VALUES (?,?,0)', [1, 5])
    run('INSERT OR REPLACE INTO assets (id, category_id, name, currency, quantity, net_value, cost_basis, ledger_id) VALUES (?,?,?,?,?,?,?,?)', [11, 2, '投资标的', 'CNY', 10, 12, 100, 1])
    run('INSERT OR REPLACE INTO assets (id, category_id, name, currency, quantity, net_value, cost_basis, ledger_id) VALUES (?,?,?,?,?,?,?,?)', [12, 3, '现金账户', 'CNY', 100, 1, 100, 1])
    saveViewCache('dca_plans', [{ id: 1, name: '缓存定投计划' }])
    persist()
    await flushPersist()

    const report = aggregateMonthlyReport(2026, 11)
    const holdings = deriveLocalHoldings()
    const cache = loadViewCache<{ name: string }[]>('dca_plans')
    return {
      totalExpense: report.total_expense,
      category: report.by_category[0]?.name,
      tag: report.by_tag[0]?.tag_name,
      holdingsCount: holdings.report.holdings.length,
      holdingName: holdings.report.holdings[0]?.name,
      currentValue: holdings.report.holdings[0]?.current_value,
      approximate: holdings.approximate,
      cacheName: cache.data?.[0]?.name,
      cacheUpdatedAt: cache.updatedAt,
    }
  })

  expect(result.totalExpense).toBe(42)
  expect(result.category).toBe('餐饮')
  expect(result.tag).toBe('午餐')
  expect(result.holdingsCount).toBe(1)
  expect(result.holdingName).toBe('投资标的')
  expect(result.currentValue).toBe(120)
  expect(result.approximate).toBe(true)
  expect(result.cacheName).toBe('缓存定投计划')
  expect(result.cacheUpdatedAt).toBeTruthy()
})
