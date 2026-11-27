/**
 * @file 移动端视图读缓存（仅本地，不进入同步队列）
 */

import { nowIso, persist, query, rowsFrom, run } from '@/db/local'

export interface ViewCache<T> {
  data: T | null
  updatedAt: string | null
}

/** 读取缓存；无记录或解析失败返回 null */
export function loadViewCache<T>(key: string): ViewCache<T> {
  try {
    const rows = rowsFrom(query('SELECT payload, updated_at FROM view_cache WHERE key = ?', [key]))
    if (rows.length === 0) return { data: null, updatedAt: null }
    return {
      data: JSON.parse(String(rows[0]?.payload)) as T,
      updatedAt: String(rows[0]?.updated_at ?? ''),
    }
  } catch {
    return { data: null, updatedAt: null }
  }
}

/** 写入/覆盖缓存并持久化 */
export function saveViewCache<T>(key: string, data: T): void {
  try {
    run(
      `INSERT INTO view_cache (key, payload, updated_at) VALUES (?, ?, ?)
       ON CONFLICT(key) DO UPDATE SET payload = excluded.payload, updated_at = excluded.updated_at`,
      [key, JSON.stringify(data), nowIso()]
    )
    persist()
  } catch {
    // localDb 未初始化或写入失败时静默降级
  }
}
