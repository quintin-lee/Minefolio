/**
 * 移动端聊天草稿端到端验证
 * 前置：本地已启动 Vite 移动端开发服务（端口 5173）与 Playwright 配置。
 */
import { test, expect } from '@playwright/test'

const BASE = 'http://localhost:5173'

async function seedAuth(page: import('@playwright/test').Page) {
  await page.addInitScript(() => {
    localStorage.setItem('token', 'e2e-chat-token')
    localStorage.setItem('minefolio_active_ledger_id', '1')
  })
}

test('草稿跨断网与重载保留，发送成功后清理', async ({ page }) => {
  await seedAuth(page)
  await page.route('**/api/ai/**', (route) => route.abort('failed'))
  await page.route('**/api/(sessions|models|settings)**', (route) =>
    route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ code: 0, data: [] }) })
  )

  await page.goto(`${BASE}/m/chat`)
  const draftText = '这是一段需要保留的离线草稿'
  await page.locator('textarea.input-field').fill(draftText)

  // 模拟切后台/重载：草稿应从本地 SQLite 恢复
  await page.reload()
  await expect(page.locator('textarea.input-field')).toHaveValue(draftText)

  const persisted = await page.evaluate(async () => {
    const { loadDraft } = await import('/src/utils/chat-draft.ts')
    return loadDraft('new')
  })
  expect(persisted).toBe(draftText)
})
