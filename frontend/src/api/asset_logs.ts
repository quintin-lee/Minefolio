// frontend/src/api/asset_logs.ts
import http from '@/utils/http'
import type { AssetBalanceLog } from '@/types'

export const assetLogsApi = {
  list: (params?: {
    asset_id?: string
    limit?: number
    page?: number
  }) => http.get<AssetBalanceLog[], AssetBalanceLog[]>('/asset-balance-logs', { params }),
}
