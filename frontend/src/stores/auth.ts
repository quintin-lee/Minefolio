/**
 * @file 用户认证与安全状态管理 Store
 * @description 管理用户登录鉴权 Token、当前登录用户信息、系统初始化状态、RSA 密码传输加密及 2FA 两步验证流程
 */

import { defineStore } from 'pinia'
import { computed, ref } from 'vue'
import { authApi, type User } from '@/api/auth'
import { systemApi } from '@/api/system'
import { encryptPassword, clearCryptoKeyCache } from '@/utils/crypto'
import { clearSwrCache } from '@/utils/swr-cache'

// RSA-OAEP 密码加密统一由 utils/crypto.ts 提供 (公钥拉取 + 加密实现已去重)

/**
 * 用户认证与权限 Pinia Store
 */
export const useAuthStore = defineStore('auth', () => {
  /** 当前用户 JWT 鉴权令牌 */
  const token = ref<string>(localStorage.getItem('token') || '')
  /** 当前登录用户信息 */
  const user = ref<User | null>(null)
  /** 当前登录用户是否为系统管理员 */
  const isAdmin = computed(() => user.value?.role === 'admin' || user.value?.is_admin === true)
  /** 系统是否完成首次初始化安装 (null 表示尚未检测) */
  const isInitialized = ref<boolean | null>(null)

  /**
   * 检查系统初始化状态 (未初始化时引导进入 /setup 页面)
   * @returns 系统是否已初始化
   */
  async function checkSystemStatus(options?: { timeout?: number }) {
    try {
      const res = options?.timeout
        ? await systemApi.status({ timeout: options.timeout })
        : await systemApi.status()
      isInitialized.value = res.initialized
      return res.initialized
    } catch {
      isInitialized.value = true
      return true
    }
  }

  /**
   * 系统安装首次初始化并创建管理员用户
   * 数据库连接由部署侧环境变量 MINEFOLIO_DB_DRIVER / MINEFOLIO_DB_DSN 提供，此处不下发。
   * @param username 管理员用户名
   * @param password 明文密码（内部完成 RSA 加密；加密或网络失败时抛出的错误带 crypto_failed 标识）
   */
  async function setup(username: string, password: string) {
    let password_enc: string
    try {
      password_enc = await encryptPassword(password)
    } catch (e: any) {
      // 区分 WebCrypto 缺失 (ensureSubtleCrypto 抛错) 与网络/RSA 接口错误：
      // 后者必须保留原错误 (含 fetch 无 response 属性) 让上层 isNetworkError 正确判定，
      // 不能一律打 crypto_failed 标签，否则会把"无法连接服务端"误诊为"WebCrypto 不支持"
      if (e?.message && /WebCrypto|Crypto|subtle/i.test(e.message)) {
        throw Object.assign(e, { crypto_failed: true })
      }
      throw e
    }
    const res = await systemApi.setup({ username, password_enc }) as { token: string; expires_in: number }
    token.value = res.token
    localStorage.setItem('token', res.token)
    isInitialized.value = true
    await fetchUser()
  }

  /**
   * 注册新用户并自动登录保存 Token
   * @param username 用户名
   * @param password 明文密码（内部完成 RSA 加密；加密或网络失败时抛出的错误带 crypto_failed 标识）
   */
  async function register(username: string, password: string) {
    let password_enc: string
    try {
      password_enc = await encryptPassword(password)
    } catch (e: any) {
      if (e?.message && /WebCrypto|Crypto|subtle/i.test(e.message)) {
        throw Object.assign(e, { crypto_failed: true })
      }
      throw e
    }
    const raw = (await authApi.register(username, password_enc)) as unknown
    const res = (raw && typeof raw === 'object' && 'data' in raw ? (raw as { data: { token: string } }).data : raw) as { token: string }
    if (res && res.token) {
      token.value = res.token
      localStorage.setItem('token', res.token)
    }
    await fetchUser()
    await reinitLocalDb()
  }

  /**
   * 重新初始化移动端离线本地库（幂等，已有实例时直接返回）。
   *
   * 登出后 resetLocalDb() 会把本地库置空并清理 localStorage，重新登录后
   * 移动端离线功能依赖本方法恢复；仅移动端入口（main-mobile）会调用，
   * 桌面端构建不会初始化本地库。
   */
  async function reinitLocalDb() {
    if (!__IS_MOBILE__) return
    try {
      const { initLocalDb } = await import('@/db/local')
      await initLocalDb()
    } catch {
      // 离线库初始化失败不影响在线登录主流程（降级为纯在线模式）
    }
  }

  /**
   * 用户登录 (支持可选 2FA 流程)
   * @param username 用户名
   * @param password 明文密码（内部完成 RSA 加密；加密或网络失败时抛出的错误带 crypto_failed 标识）
   * @param totpCode 6 位 TOTP 动态验证码 (可选)
   * @returns 登录结果 (包含 require_2fa 或 token)
   */
  async function login(username: string, password: string, totpCode?: string) {
    let password_enc: string
    try {
      password_enc = await encryptPassword(password)
    } catch (e: any) {
      if (e?.message && /WebCrypto|Crypto|subtle/i.test(e.message)) {
        throw Object.assign(e, { crypto_failed: true })
      }
      throw e
    }
    const raw = (await authApi.login(username, password_enc, totpCode)) as unknown
    const res = (raw && typeof raw === 'object' && 'data' in raw ? (raw as { data: any }).data : raw) as any
    if (res?.require_2fa) {
      token.value = ''
      localStorage.removeItem('token')
      return { require_2fa: true, temp_token: res.temp_token as string }
    }
    if (res && res.token) {
      token.value = res.token
      localStorage.setItem('token', res.token)
      await fetchUser()
      // 登出后本地库已被 reset，重新登录必须恢复离线功能，否则移动端
      // 离线降级与同步队列推送全部失效直至重启 App（幂等，桌面端不受影响）
      await reinitLocalDb()
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
      try {
        await fetchUser()
      } catch (err) {
        console.warn('[auth] fetchUser failed after 2fa verify, continuing login:', err)
      }
      // 同 login：恢复登出时被重置的离线本地库
      await reinitLocalDb()
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
    // 保留原实现 (调用方自行 try/catch)，不在此处重包 crypto_failed
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
   *
   * Token/用户态的清理是同步的；聊天等按账号缓存的内存状态随后通过动态 import 清理，
   * 避免把 AI 聊天模块静态打入首屏包，同时防止上一账号的会话/消息/草稿泄露给下一位用户。
   */
  async function logout() {
    token.value = ''
    user.value = null
    if (typeof localStorage !== 'undefined') {
      localStorage.removeItem('token')
      localStorage.removeItem('minefolio_active_ledger_id')
      localStorage.removeItem('minefolio_pinned_sessions')
      localStorage.removeItem('minefolio_pinned_workflows')
      localStorage.removeItem('minefolio_2fa_temp_session')
      // 清理按用户保存的聊天草稿 (minefolio:chat:draft:*) 
      const draftKeys: string[] = []
      for (let i = 0; i < localStorage.length; i++) {
        const key = localStorage.key(i)
        if (key && key.startsWith('minefolio:chat:draft:')) draftKeys.push(key)
      }
      draftKeys.forEach((key) => localStorage.removeItem(key))
      clearSwrCache()
    }
    if (typeof sessionStorage !== 'undefined') {
      try {
        sessionStorage.removeItem('minefolio_2fa_temp_token')
      } catch {}
    }
    clearCryptoKeyCache()
    try {
      // 动态 import 各 store：避免把聊天/账本/分类/同步模块静态打入首屏包
      const [{ useChatStore }, { useLedgerStore }, { useCategoryStore }, { useMarketStore }, { resetFinancialEntities }, syncMod] = await Promise.all([
        import('./chat'),
        import('./ledger'),
        import('./category'),
        import('./market'),
        import('@/composables/useFinancialEntities'),
        __IS_MOBILE__ ? import('./sync') : Promise.resolve(null),
      ])
      useChatStore().resetState()
      useLedgerStore().reset()
      useCategoryStore().reset()
      useMarketStore().reset()
      resetFinancialEntities()
      syncMod?.useSyncStore().reset()
    } catch {
      // 清理失败不影响登出主流程
    }
  }

  return {
    token,
    user,
    isAdmin,
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
    reinitLocalDb,
  }
})
