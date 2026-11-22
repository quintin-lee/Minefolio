/**
 * @file 系统数据库灾备与备份管理 API 模块
 * @description 提供系统数据库存储指标查询、在线热备份触发、备份历史下载与删除等能力
 */

import http from '@/utils/http'
import { resolveMaybeAsync, type AsyncTaskSubmission, type PollTaskOptions } from './tasks'

export interface BackupItem {
  filename: string
  size: number
  created_at: number
}

export interface BackupStatus {
  db_driver: string
  db_size: number
  wal_size: number
  disk_free: number
  backups: BackupItem[]
}

export interface BackupCreateResult {
  filename: string
  size: number
  created_at: number
}

export const backupApi = {
  /**
   * 获取数据库存储指标及备份历史
   */
  getStatus: () => http.get<BackupStatus>('/system/backup/status'),

  /**
   * 手动触发一次在线热备份
   */
  createBackup: async (options?: PollTaskOptions) => {
    const res = await http.post<BackupCreateResult | AsyncTaskSubmission>('/system/backup/create')
    return resolveMaybeAsync<BackupCreateResult>(res, { timeoutMs: 120000, ...options })
  },

  /**
   * 下载指定的数据库备份文件
   */
  downloadBackup: (filename: string) =>
    http.get<Blob>(`/system/backup/download/${encodeURIComponent(filename)}`, {
      responseType: 'blob',
    }),

  /**
   * 删除指定的备份文件
   */
  deleteBackup: (filename: string) =>
    http.delete<{ success: boolean }>(`/system/backup/${encodeURIComponent(filename)}`),
}

export default backupApi
