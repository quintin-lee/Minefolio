/**
 * @file 用户认证与安全 API 接口模块
 * @description 提供登录、注册、用户信息、修改密码、两步验证 (TOTP 2FA) 及 OAuth 第三方登录相关接口
 */

// frontend/src/api/auth.ts
import http from '@/utils/http'

/**
 * 登录认证响应结果
 */
export interface LoginResponse {
  /** 鉴权 JWT Token 字符串 (登录成功时返回) */
  token?: string
  /** Token 有效期 (秒) */
  expires_in?: number
  /** 是否需要进行两步验证 (TOTP 2FA) */
  require_2fa?: boolean
  /** 两步验证临时令牌 (当 require_2fa 为 true 时提供) */
  temp_token?: string
}

/**
 * TOTP 两步验证初始化配置响应
 */
export interface TwoFactorSetupResponse {
  /** TOTP 共享密钥 Base32 字符串 */
  secret: string
  /** 生成客户端认证器二维码的 otpauth URL */
  otpauth_url: string
}

/**
 * 启用两步验证成功响应
 */
export interface TwoFactorEnableResponse {
  /** 备用应急恢复码列表 (用于丢失认证器时紧急登录) */
  backup_codes: string[]
}

/**
 * 当前登录用户信息
 */
export interface User {
  /** 用户唯一标识 ID */
  id: number
  /** 用户名 */
  username: string
  /** 用户注册创建时间 (ISO 8601 字符串) */
  created_at: string
}

/**
 * 用户认证 API 服务对象
 */
export const authApi = {
  /**
   * 用户登录认证
   * @route POST /api/auth/login
   * @param username 用户名
   * @param password_enc 经 RSA-OAEP 加密后的密码字符串
   * @param totp_code 6位动态验证码 (若开启了 2FA 可在此一次性提供)
   * @returns 登录结果响应 (包含 JWT 或 2FA 临时 Token)
   */
  login: (username: string, password_enc: string, totp_code?: string) =>
    http.post<LoginResponse, any, { username: string; password_enc: string; totp_code?: string }>('/auth/login', {
      username,
      password_enc,
      totp_code,
    }),

  /**
   * 注册新用户
   * @route POST /api/auth/register
   * @param username 用户名
   * @param password_enc 经 RSA-OAEP 加密后的密码字符串
   * @returns 注册并登录后的结果响应 (包含 JWT Token)
   */
  register: (username: string, password_enc: string) =>
    http.post<LoginResponse, any, { username: string; password_enc: string }>('/auth/register', { username, password_enc }),

  /**
   * 获取当前登录用户的详细信息
   * @route GET /api/auth/me
   * @returns 用户信息实体对象
   */
  me: () => http.get<User, any, void>('/auth/me'),

  /**
   * 修改当前用户登录密码
   * @route PUT /api/auth/password
   * @param old_password_enc 经 RSA-OAEP 加密的旧密码
   * @param new_password_enc 经 RSA-OAEP 加密的新密码
   * @returns 操作结果
   */
  changePassword: (old_password_enc: string, new_password_enc: string) =>
    http.put<void, any, { old_password_enc: string; new_password_enc: string }>('/auth/password', {
      old_password_enc,
      new_password_enc,
    }),

  /**
   * 查询当前用户的 2FA 两步验证开启状态
   * @route GET /api/auth/2fa/status
   * @returns 2FA 状态对象 ({ enabled: boolean })
   */
  get2FaStatus: () => http.get<{ enabled: boolean }, any, void>('/auth/2fa/status'),

  /**
   * 初始化两步验证 (生成 TOTP 密钥与二维码 URL)
   * @route POST /api/auth/2fa/setup
   * @returns 2FA 初始化配置数据 (含 secret 与 otpauth_url)
   */
  setup2Fa: () => http.post<TwoFactorSetupResponse, any, void>('/auth/2fa/setup'),

  /**
   * 验证动态码并正式启用 2FA
   * @route POST /api/auth/2fa/enable
   * @param code 认证器 App 生成的 6 位动态验证码
   * @returns 启用结果 (返回备用应急码列表)
   */
  enable2Fa: (code: string) =>
    http.post<TwoFactorEnableResponse, any, { code: string }>('/auth/2fa/enable', { code }),

  /**
   * 关闭两步验证
   * @route POST /api/auth/2fa/disable
   * @returns 操作结果
   */
  disable2Fa: () => http.post<void, any, void>('/auth/2fa/disable'),

  /**
   * 两步验证第二阶段登录认证 (使用临时 Token + 6 位 TOTP 动态码换取正式 JWT)
   * @route POST /api/auth/2fa/verify-login
   * @param temp_token 第一阶段返回的临时 Token
   * @param code 认证器 App 生成的 6 位动态码或应急备用码
   * @returns 最终登录结果 (包含正式 JWT Token)
   */
  verify2FaLogin: (temp_token: string, code: string) =>
    http.post<LoginResponse, any, { temp_token: string; code: string }>('/auth/2fa/verify-login', { temp_token, code }),

  /**
   * 获取系统启用的 OAuth 2.0 / OIDC 第三方登录提供商列表
   * @route GET /api/auth/oauth/providers
   * @returns OAuth 提供商配置列表
   */
  getOAuthProviders: () =>
    http.get<{ providers: import('@/types').OAuthProvider[] }, { providers: import('@/types').OAuthProvider[] }>('/auth/oauth/providers'),

  /**
   * OAuth 登录回调认证处理
   * @route POST /api/auth/oauth/callback
   * @param data 回调参数 (含授权码 code 或第三方用户凭证)
   * @returns 登录结果响应 (包含 JWT Token)
   */
  oauthCallback: (data: { provider: string; code?: string; oauth_id?: string; username?: string }) =>
    http.post<LoginResponse, any, typeof data>('/auth/oauth/callback', data),
}

