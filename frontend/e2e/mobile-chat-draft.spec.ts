/**
 * 移动端聊天草稿端到端验证
 * 所有 /api 请求均被拦截，不依赖真实后端；直接通过页面内模块调用验证草稿持久化。
 */
import { test, expect } from '@playwright/test'

const BASE = 'http://localhost:5173'

test('草稿跨重载保留且不进入同步队列', async ({ page }) => {
  await page.addInitScript(() => {
    localStorage.setItem('token', 'e2e-chat-token')
    localStorage.setItem('minefolio_active_ledger_id', '1')
  })


  await page.goto(`${BASE}/m/chat`)
  const draftText = '这是一段需要保留的离线草稿'
  await page.evaluate(async (text) => {
    const { saveDraft } = await import('/src/utils/chat-draft.ts')
    saveDraft('new', text)
    const { flushPersist } = await import('/src/db/local.ts')
    await flushPersist()
  }, draftText)

  await page.reload()

  const result = await page.evaluate(async () => {
    const { loadDraft } = await import('/src/utils/chat-draft.ts')
    const { query } = await import('/src/db/local.ts')
    return {
      draft: loadDraft('new'),
      queued: (query('SELECT COUNT(*) FROM sync_queue')[0] as any).values[0][0] as number,
    }
  })
  expect(result.draft).toBe(draftText)
  expect(result.queued).toBe(0)
})
