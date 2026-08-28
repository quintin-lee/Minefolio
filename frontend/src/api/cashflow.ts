import { http } from '@/utils/http'
import type { CashflowSchedule, MonthlyCashflowSummary } from '@/types'

export const cashflowApi = {
  listSchedules(): Promise<CashflowSchedule[]> {
    return http.get('/cashflow/schedules')
  },

  createSchedule(data: Partial<CashflowSchedule>): Promise<{ id: number }> {
    return http.post('/cashflow/schedules', data)
  },

  getSchedule(id: number): Promise<CashflowSchedule> {
    return http.get(`/cashflow/schedules/${id}`)
  },

  updateSchedule(id: number, data: Partial<CashflowSchedule>): Promise<void> {
    return http.put(`/cashflow/schedules/${id}`, data)
  },

  deleteSchedule(id: number): Promise<void> {
    return http.delete(`/cashflow/schedules/${id}`)
  },

  getCalendar(year?: number, month?: number): Promise<MonthlyCashflowSummary> {
    return http.get('/cashflow/calendar', {
      params: { year, month }
    })
  },

  confirmIncome(data: {
    target_asset_id: number
    source_asset_id?: number
    amount: number
    date: string
    name?: string
    note?: string
  }): Promise<{ transaction_id: number; amount: number }> {
    return http.post('/cashflow/confirm', data)
  }
}
