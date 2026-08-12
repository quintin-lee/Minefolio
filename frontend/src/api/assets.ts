// frontend/src/api/assets.ts
import http from '@/utils/http'
import type { Asset, PageResult } from '@/types'

export const assetsApi = {
  list: (params?: { category_id?: string; page?: number; page_size?: number }) =>
    http.get<PageResult<Asset>, PageResult<Asset>>('/assets', { params }),
  create: (data: any) => http.post<void, void>('/assets', data),
  update: (id: number, data: any) => http.put<void, void>(`/assets/${id}`, data),
  delete: (id: number) => http.delete<void, void>(`/assets/${id}`),
  detail: (id: number) => http.get<Asset, Asset>(`/assets/${id}`),
}
