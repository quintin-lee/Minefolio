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
  login: (username: string, password: string) =>
    http.post<LoginResponse, LoginResponse>('/auth/login', { username, password }),
  register: (username: string, password: string) =>
    http.post<LoginResponse, LoginResponse>('/auth/register', { username, password }),
  me: () => http.get<User, User>('/auth/me'),
}
