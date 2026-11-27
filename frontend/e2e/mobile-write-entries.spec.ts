/**
 * 移动端写入口端到端验证
 * 通过请求拦截模拟断网/恢复，验证离线新增资产、分类、标签、交易后的入队与推送。
 * 所有 /api 请求均被拦截，不依赖真实后端。
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
  // wait for that signal so offline writes never race DB initialization.
  await page.locator('#mf-boot-splash').waitFor({ state: 'detached', timeout: 30_000 })
}

test('断网新增资产/分类/标签/交易 → 恢复后按序推送并使用真实 ID', async ({ page }) => {
  await seedAuth(page)
  let online = false
  const createdIds: Record<string, number> = {}

  // 兜底：所有未被特异性 mock 的 /api 请求必须返回成功。
  // 启动期的 /api/auth/me、/api/ledgers 等若返回 401，会触发
  // handleAuthError() → auth.logout() → SyncEngine.reset() → resetLocalDb()，
  // 导致用例中途 db 被置空（`localDb not initialized`）并与同步流程竞态。
  await page.route((url) => url.pathname.startsWith('/api/'), (route) =>
    route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({ code: 0, data: { list: [], total: 0 } }),
    })
  )
  await page.route((url) => url.pathname === '/api/system/status', (route) =>
    route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({ code: 0, data: { initialized: true } }),
    })
  )
  await page.route((url) => url.pathname === '/api/auth/me', (route) =>
    route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({ code: 0, data: { id: 1, username: 'e2e-user', role: 'user' } }),
    })
  )

  // 写入口与同步端点：断网时 abort，恢复后返回新 ID。
  // 注册顺序在兜底 stub 之后：Playwright 后注册的 route 优先匹配。
  await page.route(/\/api\/(assets|categories|tags|transactions)(?:\/|$)/, async (route) => {
    if (!online) return route.abort('failed')
    const path = new URL(route.request().url()).pathname.replace(/^\/api\//, '').replace(/\/$/, '')
    if (route.request().method() === 'POST') {
      createdIds[path] = (createdIds[path] ?? 1000) + 1
      return route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ code: 0, data: { id: createdIds[path] } }),
      })
    }
    return route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({ code: 0, data: { list: [], total: 0 } }),
    })
  })

  await page.goto(`${BASE}/m/dashboard`)
  await waitForAppReady(page)

  const results = await page.evaluate(async () => {
    const { offlineApi } = await import('/src/utils/offline-http.ts')
    return [
      await offlineApi.post('/categories', { name: '离线分类', type: 'expense', currency: 'CNY' }),
      await offlineApi.post('/tags', { name: '离线标签', color: '#fff' }),
      await offlineApi.post('/assets', { name: '离线资产', category_id: Date.now(), currency: 'CNY' }),
      await offlineApi.post('/transactions', {
        asset_id: Date.now(), transaction_type: 'deposit', amount: 100, transaction_date: '2026-11-26',
      }),
    ]
  })
  expect(results.every((r: any) => r.offline)).toBe(true)

  const queued = await page.evaluate(async () => {
    const { query } = await import('/src/db/local.ts')
    const rows = (query('SELECT table_name FROM sync_queue WHERE synced = 0 ORDER BY id')[0] as any).values
    return rows.map((v: any[]) => v[0])
  })
  expect(queued).toEqual(['categories', 'tags', 'assets', 'transactions'])

  online = true
  await page.evaluate(() => {
    Object.defineProperty(document, 'visibilityState', { value: 'visible', configurable: true })
    document.dispatchEvent(new Event('visibilitychange'))
  })
  await expect.poll(async () => page.evaluate(async () => {
    const { query } = await import('/src/db/local.ts')
    return (query('SELECT COUNT(*) FROM sync_queue WHERE synced = 0')[0] as any).values[0][0] as number
  }), { timeout: 10000 }).toBe(0)

  const localIds = await page.evaluate(async () => {
    const { query } = await import('/src/db/local.ts')
    return ['categories', 'tags', 'assets', 'transactions'].map((name) =>
      (query(`SELECT id FROM ${name} ORDER BY id DESC LIMIT 1`)[0] as any).values[0][0] as number
    )
  })
  expect(localIds.every((id) => id > 0 && id < 10000)).toBe(true)
})
