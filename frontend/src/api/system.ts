import { http } from '@/utils/http'

export interface SystemStatus {
  initialized: boolean
  user_count: number
}

export const systemApi = {
  status: () => http.get<SystemStatus, SystemStatus>('/system/status'),
  setup: (data: { username: string; password: string }) =>
    http.post<{ token: string; expires_in: number }, any>('/system/setup', data),
}
