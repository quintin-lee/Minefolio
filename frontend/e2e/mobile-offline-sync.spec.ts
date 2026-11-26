/**
 * 移动端离线同步端到端验证
 * 前置：本地已启动 Vite 移动端开发服务（npm run dev:mobile，端口 5173）。
 * 该用例通过请求拦截模拟断网、恢复与 code:1004 业务冲突，不依赖真实后端数据。
 */
import { test, expect } from '@playwright/test'

const BASE = 'http://localhost:5173'

async function seedAuth(page: import('@playwright/test').Page) {
  await page.addInitScript(() => {
    localStorage.setItem('token', 'e2e-offline-token')
    localStorage.setItem('minefolio_active_ledger_id', '1')
  })
}

async function pendingCount(page: import('@playwright/test').Page): Promise<number> {
  return page.evaluate(async () => {
    const { query } = await import('/src/db/local.ts')
    return (query('SELECT COUNT(*) AS n FROM sync_queue WHERE synced = 0')[0] as any).values[0][0] as number
  })
}

test('断网写入 → 恢复推送 → 409 类业务码冲突 → 保留本地', async ({ page }) => {
  await seedAuth(page)
  let online = false
  let conflictMode = false

  await page.route('**/api/daily-expenses', async (route) => {
    if (!online) return route.abort('failed')
    if (conflictMode) {
      return route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ code: 1004, message: '资源冲突', data: null }),
      })
    }
    return route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({ code: 0, data: { id: 9001 } }),
    })
  })
  // 拉取类请求统一返回空列表，避免影响断言
  await page.route('**/api/(accounts|categories|tags|assets|transactions)**', (route) =>
    route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ code: 0, data: { list: [], total: 0 } }) })
  )

  await page.goto(`${BASE}/m/dashboard`)

  // 断网写入：应落库入队而非失败
  const offlineRes = await page.evaluate(async () => {
    const { offlineApi } = await import('/src/utils/offline-http.ts')
    return offlineApi.post('/daily-expenses', {
      category_id: 1, asset_id: 1, expense_type: 'expense',
      amount: 12.5, currency: 'CNY', expense_date: '2026-11-26',
    })
  })
  expect(offlineRes.offline).toBe(true)
  expect(await pendingCount(page)).toBe(1)

  // 恢复网络 + 前后台切换 → 自动推送
  online = true
  await page.evaluate(() => {
    Object.defineProperty(document, 'visibilityState', { value: 'visible', configurable: true })
    document.dispatchEvent(new Event('visibilitychange'))
  })
  await expect.poll(async () => pendingCount(page), { timeout: 10000 }).toBe(0)

  // 制造业务冲突：服务端返回 code:1004
  conflictMode = true
  await page.evaluate(async () => {
    const { SyncEngine } = await import('/src/sync/SyncEngine.ts')
    SyncEngine.getInstance().enqueue('daily_expenses', Date.now(), 'create', {
      category_id: 1, asset_id: 1, expense_type: 'expense',
      amount: 99, currency: 'CNY', expense_date: '2026-11-26',
    })
    Object.defineProperty(document, 'visibilityState', { value: 'visible', configurable: true })
    document.dispatchEvent(new Event('visibilitychange'))
  })
  await expect.poll(async () => page.evaluate(async () => {
    const { SyncEngine } = await import('/src/sync/SyncEngine.ts')
    return SyncEngine.getInstance().getConflicts().length
  }), { timeout: 10000 }).toBe(1)

  // 设置页展示冲突两版差异，手动“保留本地”清除冲突
  await page.goto(`${BASE}/m/settings`)
  await expect(page.getByText(/同步冲突项/)).toBeVisible()
  await page.getByRole('button', { name: '保留本地' }).first().click()
  await expect.poll(async () => page.evaluate(async () => {
    const { SyncEngine } = await import('/src/sync/SyncEngine.ts')
    return SyncEngine.getInstance().getConflicts().length
  }), { timeout: 10000 }).toBe(0)
})
