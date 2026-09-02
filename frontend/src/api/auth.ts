// frontend/src/api/auth.ts
import http from '@/utils/http'

export interface LoginResponse {
  token?: string
  expires_in?: number
  require_2fa?: boolean
  temp_token?: string
}

export interface TwoFactorSetupResponse {
  secret: string
  otpauth_url: string
}

export interface TwoFactorEnableResponse {
  backup_codes: string[]
}

export interface User {
  id: number
  username: string
  created_at: string
}

export const authApi = {
  login: (username: string, password_enc: string, totp_code?: string) =>
    http.post<LoginResponse, any, { username: string; password_enc: string; totp_code?: string }>('/auth/login', {
      username,
      password_enc,
      totp_code,
    }),
  register: (username: string, password_enc: string) =>
    http.post<LoginResponse, any, { username: string; password_enc: string }>('/auth/register', { username, password_enc }),
  me: () => http.get<User, any, void>('/auth/me'),
  changePassword: (old_password_enc: string, new_password_enc: string) =>
    http.put<void, any, { old_password_enc: string; new_password_enc: string }>('/auth/password', {
      old_password_enc,
      new_password_enc,
    }),
  get2FaStatus: () => http.get<{ enabled: boolean }, any, void>('/auth/2fa/status'),
  setup2Fa: () => http.post<TwoFactorSetupResponse, any, void>('/auth/2fa/setup'),
  enable2Fa: (code: string) =>
    http.post<TwoFactorEnableResponse, any, { code: string }>('/auth/2fa/enable', { code }),
  disable2Fa: () => http.post<void, any, void>('/auth/2fa/disable'),
  verify2FaLogin: (temp_token: string, code: string) =>
    http.post<LoginResponse, any, { temp_token: string; code: string }>('/auth/2fa/verify-login', { temp_token, code }),
  getOAuthProviders: () =>
    http.get<{ providers: import('@/types').OAuthProvider[] }, { providers: import('@/types').OAuthProvider[] }>('/auth/oauth/providers'),
  oauthCallback: (data: { provider: string; code?: string; oauth_id?: string; username?: string }) =>
    http.post<LoginResponse, any, typeof data>('/auth/oauth/callback', data),
}
