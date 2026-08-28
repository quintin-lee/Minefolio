import http from '@/utils/http'
import type { DcaPlan, DcaExecution } from '@/types'

export const dcaApi = {
  listPlans: () =>
    http.get<DcaPlan[], DcaPlan[]>('/dca/plans'),

  createPlan: (data: Partial<DcaPlan>) =>
    http.post<{ id: number }, { id: number }>('/dca/plans', data),

  getPlan: (id: number) =>
    http.get<DcaPlan, DcaPlan>(`/dca/plans/${id}`),

  updatePlan: (id: number, data: Partial<DcaPlan>) =>
    http.put<void, void>(`/dca/plans/${id}`, data),

  setPlanStatus: (id: number, status: 'active' | 'paused' | 'completed') =>
    http.put<void, void>(`/dca/plans/${id}/status`, { status }),

  deletePlan: (id: number) =>
    http.delete<void, void>(`/dca/plans/${id}`),

  listExecutions: (planId: number) =>
    http.get<DcaExecution[], DcaExecution[]>(`/dca/plans/${planId}/executions`),

  listPendingExecutions: () =>
    http.get<DcaExecution[], DcaExecution[]>('/dca/executions/pending'),

  confirmExecution: (
    id: number,
    data?: { actual_amount?: number; executed_price?: number }
  ) =>
    http.post<
      { transaction_id: number; actual_amount: number; executed_price: number; executed_quantity: number },
      { transaction_id: number; actual_amount: number; executed_price: number; executed_quantity: number }
    >(`/dca/executions/${id}/confirm`, data || {}),

  skipExecution: (id: number) =>
    http.post<void, void>(`/dca/executions/${id}/skip`)
}
