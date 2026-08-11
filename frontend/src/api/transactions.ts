// frontend/src/api/transactions.ts
import http from '@/utils/http'
import type { Transaction } from '@/types'

export const transactionsApi = {
  list: (params?: {
    asset_id?: string
    category_id?: string
    type?: string
    start_date?: string
    end_date?: string
  }) => http.get<Transaction[]>('/transactions', { params }),
  create: (data: any) => http.post<void>('/transactions', data),
  update: (id: number, data: any) => http.put<void>(`/transactions/${id}`, data),
  delete: (id: number) => http.delete<void>(`/transactions/${id}`),
}
