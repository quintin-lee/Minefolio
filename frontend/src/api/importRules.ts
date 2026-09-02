// frontend/src/api/importRules.ts
import http from '@/utils/http'

export interface ImportRule {
  id: number
  user_id: number
  keyword: string
  match_field: 'all' | 'description' | 'counterparty' | 'note'
  match_type: 'contains' | 'exact' | 'regex'
  category_id: number | null
  category_name?: string
  target_type: 'expense' | 'income' | 'transaction'
  priority: number
  is_active: boolean | number
  created_at: string
}

export interface ImportRuleCreatePayload {
  keyword: string
  match_field?: string
  match_type?: string
  category_id?: number
  target_type?: string
  priority?: number
  is_active?: boolean
}

export const importRulesApi = {
  list: () => http.get<ImportRule[], any, void>('/import-rules'),
  get: (id: number) => http.get<ImportRule, any, void>(`/import-rules/${id}`),
  create: (data: ImportRuleCreatePayload) =>
    http.post<{ id: number }, any, ImportRuleCreatePayload>('/import-rules', data),
  update: (id: number, data: ImportRuleCreatePayload) =>
    http.put<void, any, ImportRuleCreatePayload>(`/import-rules/${id}`, data),
  delete: (id: number) => http.delete<void, any, void>(`/import-rules/${id}`),
  resetDefaults: () => http.post<ImportRule[], any, void>('/import-rules/reset-defaults'),
}
