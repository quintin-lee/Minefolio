/**
 * @file 移动端聊天草稿本地存储
 * @description 草稿仅存本地 SQLite，不进入 SyncEngine 同步队列、不上传服务端。
 */

import { query, run, rowsFrom, persist, nowIso } from '@/db/local'

/** 读取草稿；无记录返回空串 */
export function loadDraft(scopeKey: string): string {
  try {
    const rows = rowsFrom(query('SELECT content FROM chat_drafts WHERE scope_key = ?', [scopeKey]))
    return rows.length > 0 ? String(rows[0]?.content ?? '') : ''
  } catch {
    return ''
  }
}

/** 保存（upsert）草稿并持久化 */
export function saveDraft(scopeKey: string, content: string): void {
  try {
    run(
      `INSERT INTO chat_drafts (scope_key, content, updated_at) VALUES (?, ?, ?)
       ON CONFLICT(scope_key) DO UPDATE SET content = excluded.content, updated_at = excluded.updated_at`,
      [scopeKey, content, nowIso()]
    )
    persist()
  } catch {
    // localDb 未初始化或写入失败时静默降级，避免阻断输入/卸载流程
  }
}

/** 删除草稿并持久化 */
export function clearDraft(scopeKey: string): void {
  try {
    run('DELETE FROM chat_drafts WHERE scope_key = ?', [scopeKey])
    persist()
  } catch {
    // localDb 未初始化时静默降级
  }
}
