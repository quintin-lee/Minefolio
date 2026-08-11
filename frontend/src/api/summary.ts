// frontend/src/api/summary.ts
import http from '@/utils/http'
import type { Summary } from '@/types'

export const summaryApi = {
  get: () => http.get<Summary>('/summary'),
}
