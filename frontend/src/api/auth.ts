// frontend/src/api/auth.ts
import http from '@/utils/http'

export interface LoginResponse {
  token: string
  expires_in: number
}

export interface User {
  id: number
  username: string
  created_at: string
}

export const authApi = {
  login: (username: string, password_enc: string) =>
    http.post<LoginResponse, any, { username: string; password_enc: string }>('/auth/login', { username, password_enc }),
  register: (username: string, password: string) =>
    http.post<LoginResponse, any, { username: string; password: string }>('/auth/register', { username, password }),
  me: () => http.get<User, any, void>('/auth/me'),
  changePassword: (old_password_enc: string, new_password_enc: string) =>
    http.put<void, any, { old_password_enc: string; new_password_enc: string }>('/auth/password', {
      old_password_enc,
      new_password_enc,
    }),
}
