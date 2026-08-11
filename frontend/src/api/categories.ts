// frontend/src/api/categories.ts
import http from '@/utils/http'
import type { Category } from '@/types'

export const categoriesApi = {
  list: () => http.get<Category[], Category[]>('/categories'),
  create: (data: any) => http.post<void, void>('/categories', data),
  update: (id: number, data: any) => http.put<void, void>(`/categories/${id}`, data),
  delete: (id: number) => http.delete<void, void>(`/categories/${id}`),
}
