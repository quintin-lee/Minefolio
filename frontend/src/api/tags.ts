// frontend/src/api/tags.ts
import http from '@/utils/http'
import type { Tag } from '@/types'

export const tagsApi = {
  list: () => http.get<Tag[], Tag[]>('/tags'),
  create: (data: { name: string; color?: string }) =>
    http.post<void, void>('/tags', data),
  update: (id: number, data: { name?: string; color?: string }) =>
    http.put<void, void>(`/tags/${id}`, data),
  delete: (id: number) => http.delete<void, void>(`/tags/${id}`),
  suggestions: (q?: string) =>
    http.get<Tag[], Tag[]>('/tags/suggestions', { params: { q } }),
}
