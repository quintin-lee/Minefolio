// frontend/src/api/transactions.ts
import http from '@/utils/http'
import type { PageResult, Transaction, TransactionMonthly } from '@/types'

export const transactionsApi = {
  list: (params?: {
    asset_id?: string
    category_id?: string
    type?: string
    start_date?: string
    end_date?: string
    page?: number
    page_size?: number
  }) => http.get<PageResult<Transaction>, PageResult<Transaction>>('/transactions', { params }),
  monthly: (month: string) =>
    http.get<TransactionMonthly, TransactionMonthly>('/transactions/monthly', {
      params: { month },
    }),
  create: (data: any) => http.post<void, void>('/transactions', data),
  update: (id: number, data: any) => http.put<void, void>(`/transactions/${id}`, data),
  delete: (id: number) => http.delete<void, void>(`/transactions/${id}`),
  exportCsv: () => http.get('/export/transactions', { responseType: 'blob' }) as unknown as Promise<Blob>,
  importCsv: (text: string) => http.post<{ imported: number; errors: number; errors_detail?: string }>('/import/transactions', text, {
    headers: { 'Content-Type': 'text/csv; charset=utf-8' },
  }),
}
