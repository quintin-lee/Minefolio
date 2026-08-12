// frontend/src/api/daily_expenses.ts
import http from '@/utils/http'
import type { DailyExpense, ExpenseMonthly, PageResult } from '@/types'

export const dailyExpensesApi = {
  list: (params?: {
    expense_type?: string
    category_id?: string
    tag_ids?: string
    start_date?: string
    end_date?: string
    page?: number
    page_size?: number
  }) =>
    http.get<PageResult<DailyExpense>, PageResult<DailyExpense>>('/daily-expenses', {
      params,
    }),
  create: (data: any) => http.post<void, void>('/daily-expenses', data),
  update: (id: number, data: any) => http.put<void, void>(`/daily-expenses/${id}`, data),
  delete: (id: number) => http.delete<void, void>(`/daily-expenses/${id}`),
  monthly: (year: number, month: number) =>
    http.get<ExpenseMonthly, ExpenseMonthly>(`/daily-expenses/monthly`, {
      params: { year, month },
    }),
}
