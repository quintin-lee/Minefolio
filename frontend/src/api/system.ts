import http from '@/utils/http'

export interface SystemStatus {
  initialized: boolean
  user_count: number
}

export const systemApi = {
  status: () => http.get<SystemStatus, any, void>('/system/status'),
  setup: (data: { username: string; password_enc: string }) =>
    http.post<{ token: string; expires_in: number }, any, { username: string; password_enc: string }>('/system/setup', data),
}
