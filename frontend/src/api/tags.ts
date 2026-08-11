// frontend/src/api/tags.ts
import http from '@/utils/http'
import type { Tag } from '@/types'

export const tagsApi = {
  list: () => http.get<Tag[]>('/tags'),
  create: (data: { name: string; color?: string }) =>
    http.post<void>('/tags', data),
  update: (id: number, data: { name?: string; color?: string }) =>
    http.put<void>(`/tags/${id}`, data),
  delete: (id: number) => http.delete<void>(`/tags/${id}`),
  suggestions: (q?: string) =>
    http.get<Tag[]>('/tags/suggestions', { params: { q } }),
}
