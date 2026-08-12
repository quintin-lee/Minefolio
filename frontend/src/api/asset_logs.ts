// frontend/src/api/asset_logs.ts
import http from '@/utils/http'
import type { AssetBalanceLog, PageResult } from '@/types'

export const assetLogsApi = {
  list: (params?: { asset_id?: string; page?: number; page_size?: number }) =>
    http.get<PageResult<AssetBalanceLog>, PageResult<AssetBalanceLog>>('/asset-balance-logs', {
      params,
    }),
}
