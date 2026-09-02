// frontend/src/api/receipts.ts
import http from '@/utils/http'

export interface ReceiptScanResult {
  date: string
  amount: number
  type: 'expense' | 'income'
  category_id: number
  category_name: string
  counterparty: string
  description: string
  currency: string
  confidence: number
}

export interface ReceiptScanPayload {
  image: string
  model?: string
  provider?: string
}

export const receiptsApi = {
  scan: (data: ReceiptScanPayload) =>
    http.post<ReceiptScanResult, any, ReceiptScanPayload>('/receipts/scan', data),
}
