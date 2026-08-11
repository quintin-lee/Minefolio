// frontend/src/api/assets.ts
import http from '@/utils/http'
import type { Asset } from '@/types'

export const assetsApi = {
  list: (params?: { category_id?: string }) =>
    http.get<Asset[]>('/assets', { params }),
  create: (data: any) => http.post<void>('/assets', data),
  update: (id: number, data: any) => http.put<void>(`/assets/${id}`, data),
  delete: (id: number) => http.delete<void>(`/assets/${id}`),
  detail: (id: number) => http.get<Asset>(`/assets/${id}`),
}
