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

async function waitForAppReady(page: import('@playwright/test').Page) {
  // Bootstrap awaits initLocalDb() before mounting and dismissing the splash;
  // wait for that signal so the offline write never races DB initialization.
  await page.locator('#mf-boot-splash').waitFor({ state: 'detached', timeout: 30_000 })
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

  // Default stub for every unhandled API call: a 401 from any boot-time
  // request triggers auth.logout(), which resets the local DB mid-scenario.
  await page.route((url) => url.pathname.startsWith('/api/'), (route) =>
    route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ code: 0, data: { list: [], total: 0 } }) })
  )
  await page.route((url) => url.pathname === '/api/system/status', (route) =>
    route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ code: 0, data: { initialized: true } }) })
  )
  await page.route((url) => url.pathname === '/api/auth/me', (route) =>
    route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ code: 0, data: { id: 1, username: 'e2e-user', role: 'user' } }) })
  )

  await page.route((url) => url.pathname === '/api/daily-expenses', async (route) => {
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
  await page.goto(`${BASE}/m/dashboard`)
  await waitForAppReady(page)

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

  // 制造业务冲突：服务端返回 code:1004。等上一轮 pullRemote 完成后再触发，
  // 否则 syncNow 的并发守卫会直接丢弃本次 visibilitychange。
  conflictMode = true
  await expect.poll(async () => page.evaluate(async () => {
    const { SyncEngine } = await import('/src/sync/SyncEngine.ts')
    return SyncEngine.getInstance().getState().syncing
  }), { timeout: 10000 }).toBe(false)
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
  // Client-side navigation: a second full reload during an active conflict
  // re-runs the encrypted local-db bootstrap and stalls in headless CI.
  await page.evaluate(() => {
    window.history.pushState({}, '', '/m/settings')
    window.dispatchEvent(new PopStateEvent('popstate'))
  })
  await expect(page.getByText(/同步冲突项/)).toBeVisible()
  // 保留本地会重推队列项；恢复服务端健康状态使重试成功。
  conflictMode = false
  await page.getByRole('button', { name: '保留本地' }).first().click()
  await expect.poll(async () => page.evaluate(async () => {
    const { SyncEngine } = await import('/src/sync/SyncEngine.ts')
    return SyncEngine.getInstance().getConflicts().length
  }), { timeout: 10000 }).toBe(0)
})
