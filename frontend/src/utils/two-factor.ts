/**
 * @file 两步验证 (2FA / TOTP) 移动端与桌面端统一辅助工具
 * @description 提供 TOTP 动态码与应急备用码规范化清洗、临时凭证持久化及过期管理
 */

export interface Stored2FaSession {
  tempToken: string
  expiresAt: number
}

export const STORAGE_KEY_2FA_SESSION = 'minefolio_2fa_temp_session'
export const STORAGE_KEY_2FA_LEGACY = 'minefolio_2fa_temp_token'

/**
 * 规范化两步验证码（支持 6 位 TOTP 动态码与 8~9 位应急备用码）
 *
 * 彻底解决移动端特有输入问题：
 * 1. 中文输入法全角数字 ０-９ 转半角 0-9
 * 2. 中文输入法全角字母 ａ-ｚ/Ａ-Ｚ 转半角小写 a-z
 * 3. 移动端软键盘/自动更正生成的各类破折号 (—, –, −, －) 统一转为 ASCII 连字符 '-'
 * 4. 移动端剪贴板复制附带的零宽空格 (\u200B-\u200F)、BOM (\uFEFF) 及不可见控制字符清理
 * 5. 验证器 App 格式化空格 (如 "123 456"、\u00A0 不换行空格、\u3000 全角空格) 清理
 *
 * @param input 用户原始输入
 * @returns 规范化后的纯 ASCII 字符串
 */
export function normalize2FaCode(input: string): string {
  if (!input) return ''
  return input
    // 1. 全角数字转半角 ０-９ -> 0-9
    .replace(/[\uFF10-\uFF19]/g, (ch) => String.fromCharCode(ch.charCodeAt(0) - 0xFEE0))
    // 2. 全角字母转半角 ａ-ｚ, Ａ-Ｚ
    .replace(/[\uFF21-\uFF3A]/g, (ch) => String.fromCharCode(ch.charCodeAt(0) - 0xFEE0))
    .replace(/[\uFF41-\uFF5A]/g, (ch) => String.fromCharCode(ch.charCodeAt(0) - 0xFEE0))
    // 3. 各类破折号、连接符、减号统一规整为 ASCII 减号 '-'
    .replace(/[\u2010-\u2015\u2212\uFE58\uFE63\uFF0D]/g, '-')
    // 4. 清除所有零宽字符、不可见控制字符及 BOM
    .replace(/[\u200B-\u200F\u2028-\u202F\u2060-\u206F\uFEFF]/g, '')
    // 5. 清除所有空白字符（含全角空格 \u3000、不换行空格 \u00A0）
    .replace(/\s+/g, '')
    .trim()
    .toLowerCase()
}

/**
 * 将 2FA 临时凭证安全持久化到 localStorage（带 TTL 过期机制）
 *
 * 移动端从 Minefolio 切到 Google Authenticator / 1Password 复制动态码时，
 * Android / iOS 系统的 WebView 进程常因内存压力被系统回收重建。
 * sessionStorage 在进程重建时必定丢失，导致切回后凭证失效；
 * 使用带 5 分钟 (300s) TTL 的 localStorage 可确保切应用不丢状态，且过期自动失效。
 *
 * @param tempToken 后端颁发的 2fa_pending 临时凭证
 * @param ttlMs 有效期毫秒数（默认 300,000ms = 5分钟）
 */
export function save2FaTempSession(tempToken: string, ttlMs = 300000): void {
  if (typeof localStorage === 'undefined') return
  try {
    if (!tempToken) {
      clear2FaTempSession()
      return
    }
    const session: Stored2FaSession = {
      tempToken,
      expiresAt: Date.now() + ttlMs,
    }
    localStorage.setItem(STORAGE_KEY_2FA_SESSION, JSON.stringify(session))
    if (typeof sessionStorage !== 'undefined') {
      try {
        sessionStorage.setItem(STORAGE_KEY_2FA_LEGACY, tempToken)
      } catch {}
    }
  } catch {}
}

/**
 * 读取未过期的 2FA 临时凭证
 * @returns 有效的 tempToken，若无或已过期返回 null
 */
export function load2FaTempSession(): string | null {
  if (typeof localStorage !== 'undefined') {
    try {
      const raw = localStorage.getItem(STORAGE_KEY_2FA_SESSION)
      if (raw) {
        const parsed = JSON.parse(raw) as Stored2FaSession
        if (parsed && parsed.tempToken && typeof parsed.expiresAt === 'number') {
          if (parsed.expiresAt > Date.now()) {
            return parsed.tempToken
          }
          localStorage.removeItem(STORAGE_KEY_2FA_SESSION)
        }
      }
    } catch {}
  }
  if (typeof sessionStorage !== 'undefined') {
    try {
      const legacy = sessionStorage.getItem(STORAGE_KEY_2FA_LEGACY)
      if (legacy) return legacy
    } catch {}
  }
  return null
}

/**
 * 清除本地保存的 2FA 临时凭证
 */
export function clear2FaTempSession(): void {
  if (typeof localStorage !== 'undefined') {
    try {
      localStorage.removeItem(STORAGE_KEY_2FA_SESSION)
    } catch {}
  }
  if (typeof sessionStorage !== 'undefined') {
    try {
      sessionStorage.removeItem(STORAGE_KEY_2FA_LEGACY)
    } catch {}
  }
}
