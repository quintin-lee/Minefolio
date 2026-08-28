import { http } from '@/utils/http'
import type { DcaPlan, DcaExecution } from '@/types'

export const dcaApi = {
  listPlans(): Promise<DcaPlan[]> {
    return http.get('/dca/plans')
  },

  createPlan(data: Partial<DcaPlan>): Promise<{ id: number }> {
    return http.post('/dca/plans', data)
  },

  getPlan(id: number): Promise<DcaPlan> {
    return http.get(`/dca/plans/${id}`)
  },

  updatePlan(id: number, data: Partial<DcaPlan>): Promise<void> {
    return http.put(`/dca/plans/${id}`, data)
  },

  setPlanStatus(id: number, status: 'active' | 'paused' | 'completed'): Promise<void> {
    return http.put(`/dca/plans/${id}/status`, { status })
  },

  deletePlan(id: number): Promise<void> {
    return http.delete(`/dca/plans/${id}`)
  },

  listExecutions(planId: number): Promise<DcaExecution[]> {
    return http.get(`/dca/plans/${planId}/executions`)
  },

  listPendingExecutions(): Promise<DcaExecution[]> {
    return http.get('/dca/executions/pending')
  },

  confirmExecution(
    id: number,
    data?: { actual_amount?: number; executed_price?: number }
  ): Promise<{ transaction_id: number; actual_amount: number; executed_price: number; executed_quantity: number }> {
    return http.post(`/dca/executions/${id}/confirm`, data || {})
  },

  skipExecution(id: number): Promise<void> {
    return http.post(`/dca/executions/${id}/skip`)
  }
}
