// frontend/src/api/categories.ts
import http from '@/utils/http'
import type { Category } from '@/types'

export const categoriesApi = {
  list: () => http.get<Category[]>('/categories'),
  create: (data: any) => http.post<void>('/categories', data),
  update: (id: number, data: any) => http.put<void>(`/categories/${id}`, data),
  delete: (id: number) => http.delete<void>(`/categories/${id}`),
}
