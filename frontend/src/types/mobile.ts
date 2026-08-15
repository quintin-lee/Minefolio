// Mobile sync / local-db types.
export interface SyncQueueItem {
  id: number
  table_name: string
  record_id: number
  operation: 'create' | 'update' | 'delete'
  payload: string // JSON 完整记录
  local_version: string // ISO 8601
  synced: number // 0|1
  conflict: number // 0|1
}

export interface SyncMeta {
  key: string
  value: string
}

// 本地查询结果行（业务表 + __deleted）
export type LocalRow = Record<string, unknown> & { __deleted?: number }

export type SqlValue = string | number | null | Uint8Array

export interface DbResult {
  columns: string[]
  values: SqlValue[][]
}
