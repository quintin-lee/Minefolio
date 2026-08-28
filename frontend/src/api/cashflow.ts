import http from '@/utils/http'
import type { CashflowSchedule, MonthlyCashflowSummary } from '@/types'

export const cashflowApi = {
  listSchedules: () =>
    http.get<CashflowSchedule[], CashflowSchedule[]>('/cashflow/schedules'),

  createSchedule: (data: Partial<CashflowSchedule>) =>
    http.post<{ id: number }, { id: number }>('/cashflow/schedules', data),

  getSchedule: (id: number) =>
    http.get<CashflowSchedule, CashflowSchedule>(`/cashflow/schedules/${id}`),

  updateSchedule: (id: number, data: Partial<CashflowSchedule>) =>
    http.put<void, void>(`/cashflow/schedules/${id}`, data),

  deleteSchedule: (id: number) =>
    http.delete<void, void>(`/cashflow/schedules/${id}`),

  getCalendar: (year?: number, month?: number) =>
    http.get<MonthlyCashflowSummary, MonthlyCashflowSummary>('/cashflow/calendar', {
      params: { year, month }
    }),

  confirmIncome: (data: {
    target_asset_id: number
    source_asset_id?: number
    amount: number
    date: string
    name?: string
    note?: string
  }) =>
    http.post<{ transaction_id: number; amount: number }, { transaction_id: number; amount: number }>(
      '/cashflow/confirm',
      data
    )
}
