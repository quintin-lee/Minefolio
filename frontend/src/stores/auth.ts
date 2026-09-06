/**
 * @file 用户认证与安全状态管理 Store
 * @description 管理用户登录鉴权 Token、当前登录用户信息、系统初始化状态、RSA 密码传输加密及 2FA 两步验证流程
 */

import { defineStore } from 'pinia'
import { ref } from 'vue'
import { authApi } from '@/api/auth'
import { systemApi } from '@/api/system'
import { encryptPassword } from '@/utils/crypto'

/**
 * 用户信息接口
 */
interface User {
  /** 用户唯一标识 ID */
  id: number
  /** 用户名 */
  username: string
  /** 注册创建时间 (ISO 8601 字符串) */
  created_at: string
}

// RSA-OAEP 密码加密统一由 utils/crypto.ts 提供 (公钥拉取 + 加密实现已去重)

/**
 * 用户认证与权限 Pinia Store
 */
export const useAuthStore = defineStore('auth', () => {
  /** 当前用户 JWT 鉴权令牌 */
  const token = ref<string>(localStorage.getItem('token') || '')
  /** 当前登录用户信息 */
  const user = ref<User | null>(null)
  /** 系统是否完成首次初始化安装 (null 表示尚未检测) */
  const isInitialized = ref<boolean | null>(null)

  /**
   * 检查系统初始化状态 (未初始化时引导进入 /setup 页面)
   * @returns 系统是否已初始化
   */
  async function checkSystemStatus() {
    try {
      const res = await systemApi.status()
      isInitialized.value = res.initialized
      return res.initialized
    } catch {
      isInitialized.value = true
      return true
    }
  }

  /**
   * 执行系统安装首次初始化并创建管理员用户
   * @param username 管理员用户名
   * @param password 管理员密码
   * @param dbConfig 数据库配置 (可选)
   */
  async function setup(username: string, password: string, dbConfig?: { db_driver?: string; db_dsn?: string }) {
    const password_enc = await encryptPassword(password)
    const res = await systemApi.setup({ username, password_enc, ...dbConfig }) as { token: string; expires_in: number }
    token.value = res.token
    localStorage.setItem('token', res.token)
    isInitialized.value = true
    await fetchUser()
  }

  /**
   * 注册新用户并自动登录保存 Token
   * @param username 用户名
   * @param password 密码
   */
  async function register(username: string, password: string) {
    const password_enc = await encryptPassword(password)
    const raw = (await authApi.register(username, password_enc)) as unknown
    const res = (raw && typeof raw === 'object' && 'data' in raw ? (raw as { data: { token: string } }).data : raw) as { token: string }
    if (res && res.token) {
      token.value = res.token
      localStorage.setItem('token', res.token)
    }
    await fetchUser()
  }

  /**
   * 用户登录 (支持可选 2FA 流程)
   * @param username 用户名
   * @param password 密码
   * @param totpCode 6 位 TOTP 动态验证码 (可选)
   * @returns 登录结果 (包含 require_2fa 或 token)
   */
  async function login(username: string, password: string, totpCode?: string) {
    const password_enc = await encryptPassword(password)
    const raw = (await authApi.login(username, password_enc, totpCode)) as unknown
    const res = (raw && typeof raw === 'object' && 'data' in raw ? (raw as { data: any }).data : raw) as any
    if (res?.require_2fa) {
      return { require_2fa: true, temp_token: res.temp_token as string }
    }
    if (res && res.token) {
      token.value = res.token
      localStorage.setItem('token', res.token)
      await fetchUser()
    }
    return res
  }

  /**
   * 2FA 二次验证登录并换取正式 Token
   * @param temp_token 临时鉴权 Token
   * @param code 6 位动态验证码或备用码
   * @returns 登录结果
   */
  async function verify2FaLogin(temp_token: string, code: string) {
    const raw = (await authApi.verify2FaLogin(temp_token, code)) as unknown
    const res = (raw && typeof raw === 'object' && 'data' in raw ? (raw as { data: any }).data : raw) as any
    if (res && res.token) {
      token.value = res.token
      localStorage.setItem('token', res.token)
      await fetchUser()
    }
    return res
  }

  /**
   * 获取当前登录用户的最新个人资料
   */
  async function fetchUser() {
    const res = await authApi.me()
    user.value = res
  }

  /**
   * 修改当前用户密码
   * @param oldPassword 当前原密码
   * @param newPassword 新设密码
   */
  async function changePassword(oldPassword: string, newPassword: string) {
    const old_enc = await encryptPassword(oldPassword)
    const new_enc = await encryptPassword(newPassword)
    await authApi.changePassword(old_enc, new_enc)
  }

  /**
   * 手动设置/更新 JWT Token
   * @param newToken 新的 Token 字符串
   */
  function setToken(newToken: string) {
    token.value = newToken
    if (newToken) localStorage.setItem('token', newToken)
    else localStorage.removeItem('token')
  }

  /**
   * 手动设置/更新用户信息
   * @param newUser 用户信息对象
   */
  function setUser(newUser: User | null) {
    user.value = newUser
  }

  /**
   * 退出登录 (清除本地 Token、用户信息并重置状态)
   */
  function logout() {
    token.value = ''
    user.value = null
    localStorage.removeItem('token')
  }

  return {
    token,
    user,
    isInitialized,
    checkSystemStatus,
    setup,
    login,
    verify2FaLogin,
    register,
    logout,
    fetchUser,
    changePassword,
    setToken,
    setUser,
  }
})


