// frontend/src/api/daily_expenses.ts
import http from '@/utils/http'
import type { DailyExpense, ExpenseMonthly } from '@/types'

export const dailyExpensesApi = {
  list: (params?: {
    expense_type?: string
    category_id?: string
    tag_ids?: string
    start_date?: string
    end_date?: string
  }) => http.get<DailyExpense[]>('/daily-expenses', { params }),
  create: (data: any) => http.post<void>('/daily-expenses', data),
  update: (id: number, data: any) => http.put<void>(`/daily-expenses/${id}`, data),
  delete: (id: number) => http.delete<void>(`/daily-expenses/${id}`),
  monthly: (year: number, month: number) =>
    http.get<ExpenseMonthly>(`/daily-expenses/monthly`, {
      params: { year, month },
    }),
}
