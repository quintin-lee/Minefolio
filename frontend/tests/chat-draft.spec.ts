import { describe, it, expect, beforeEach, vi } from 'vitest'
import { initLocalDb, query, resetLocalDb, flushPersist } from '@/db/local'
import { loadDraft, saveDraft, clearDraft } from '@/utils/chat-draft'

async function reloadDb() {
  vi.resetModules()
  return await import('@/db/local')
}

beforeEach(() => {
  resetLocalDb()
})

describe('chat drafts', () => {
  it('saves, loads and clears a draft by scope key', async () => {
    await initLocalDb()
    expect(loadDraft('new')).toBe('')
    saveDraft('new', '第一版草稿')
    expect(loadDraft('new')).toBe('第一版草稿')
    saveDraft('new', '第二版草稿')
    expect(loadDraft('new')).toBe('第二版草稿')
    saveDraft('session:7', '另一会话')
    clearDraft('new')
    expect(loadDraft('new')).toBe('')
    expect(loadDraft('session:7')).toBe('另一会话')
  })

  it('survives a local db reload and never enters sync_queue', async () => {
    await initLocalDb()
    saveDraft('session:9', '重载后保留的内容')
    await flushPersist()

    const fresh = await reloadDb()
    await fresh.initLocalDb()
    const draft = fresh.rowsFrom(fresh.query('SELECT content FROM chat_drafts WHERE scope_key = ?', ['session:9']))
    expect(draft[0].content).toBe('重载后保留的内容')
    expect(fresh.rowsFrom(fresh.query('SELECT * FROM sync_queue')).length).toBe(0)
  })

  it('does not throw when the local db is unavailable', async () => {
    expect(loadDraft('new')).toBe('')
  })
})
