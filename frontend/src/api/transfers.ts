// frontend/src/api/transfers.ts
import http from '@/utils/http'

export interface TransferRequest {
  from_asset_id: number
  to_asset_id: number
  amount: number
  transfer_date: string
  note?: string
  currency?: string
}

export const transfersApi = {
  create: (data: TransferRequest) => http.post<void, void>('/transfers', data),
}
