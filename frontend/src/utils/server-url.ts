/**
 * @file 服务端地址管理工具模块
 * @description 提供服务端 URL 的本地持久化、格式规范化校验、变更事件分发及连通性探测功能
 */

/** 自定义服务端地址本地存储键名 */
export const STORAGE_KEY_SERVER_URL = 'minefolio_server_url'

/** 服务端地址变更监听钩子类型 */
export type ServerUrlChangeHook = (newUrl: string) => void
const serverUrlChangeHooks: ServerUrlChangeHook[] = []

/**
 * 注册服务端地址变更监听钩子
 * @param fn 变更回调函数
 */
export function onServerUrlChange(fn: ServerUrlChangeHook): void {
  serverUrlChangeHooks.push(fn)
}

/**
 * 触发服务端地址变更监听钩子
 * @param newUrl 变更后的服务端地址
 */
function notifyServerUrlChange(newUrl: string): void {
  for (const fn of serverUrlChangeHooks) {
    try {
      fn(newUrl)
    } catch (e) {
      console.error('[server-url] error in serverUrlChangeHook:', e)
    }
  }

  if (typeof window !== 'undefined') {
    try {
      window.dispatchEvent(new CustomEvent('minefolio:server-url-changed', { detail: newUrl }))
    } catch {
      // ignore
    }
  }
}

function isPrivateOrLocalHost(hostname: string): boolean {
  if (!hostname) return false
  const lower = hostname.toLowerCase()
  if (lower === 'localhost' || lower === '127.0.0.1' || lower === '::1' || lower === '[::1]') {
    return true
  }
  if (
    lower.endsWith('.local') ||
    lower.endsWith('.lan') ||
    lower.endsWith('.internal') ||
    lower.endsWith('.bar')
  ) {
    return true
  }
  // 检查 IPv4 私有网段
  const ipv4Match = lower.match(/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/)
  if (ipv4Match && ipv4Match[1] && ipv4Match[2] && ipv4Match[3] && ipv4Match[4]) {
    const a = parseInt(ipv4Match[1], 10)
    const b = parseInt(ipv4Match[2], 10)
    const c = parseInt(ipv4Match[3], 10)
    const d = parseInt(ipv4Match[4], 10)
    if (a > 255 || b > 255 || c > 255 || d > 255) return false
    // 127.0.0.0/8 回环
    if (a === 127) return true
    // 10.0.0.0/8 私网
    if (a === 10) return true
    // 172.16.0.0/12 私网 (172.16 - 172.31)
    if (a === 172 && b >= 16 && b <= 31) return true
    // 192.168.0.0/16 私网
    if (a === 192 && b === 168) return true
    // 169.254.0.0/16 链路本地
    if (a === 169 && b === 254) return true
    return false // 公网 IPv4
  }
  // 已知公网服务域名禁止纯明文 HTTP
  if (lower.endsWith('.minefolio.com') || lower === 'minefolio.com') {
    return false
  }
  // 无点的单标签主机名 (如局域网 NAS "home-server")
  if (!lower.includes('.')) {
    return true
  }
  return false
}

/**
 * 规范化服务端 URL 格式
 * @description 去除首尾空白，若缺少 http/https 协议则默认补齐 http://，去除末尾斜杠
 * @param url 待规范化的 URL 字符串
 * @returns 规范化后的 URL 字符串
 */
export function normalizeServerUrl(url: string): string {
  let trimmed = (url || '').trim()
  if (!trimmed) return ''

  // 若显式包含危险伪协议 (如 javascript:, file:, data:)，保留原格式以便校验阻断
  if (/^(javascript|data|file|vbscript|about|blob):/i.test(trimmed)) {
    return trimmed
  }
  if (!/^https?:\/\//i.test(trimmed)) {
    trimmed = `http://${trimmed}`
  }
  return trimmed.replace(/\/+$/, '')
}

/**
 * 校验服务端 URL 字符串是否有效且符合安全规范
 * @param url URL 字符串
 * @returns 是否为有效格式 (空字符串视为使用默认地址，返回 true)
 */
export function isValidServerUrl(url: string): boolean {
  const trimmed = (url || '').trim()
  if (!trimmed) return true

  // 1. 严格阻断伪协议注入
  if (/^(javascript|data|file|vbscript|about|blob):/i.test(trimmed)) {
    return false
  }

  try {
    const normalized = normalizeServerUrl(trimmed)
    const parsed = new URL(normalized)
    if (!parsed.host) return false
    if (parsed.protocol !== 'http:' && parsed.protocol !== 'https:') {
      return false
    }

    // 2. 对明文 HTTP 协议限制仅限本地回环或局域网私有地址，公网主机强制要求 HTTPS
    if (parsed.protocol === 'http:') {
      return isPrivateOrLocalHost(parsed.hostname)
    }

    return true
  } catch {
    return false
  }
}

/**
 * 获取当前配置的服务端基础地址
 * @description 优先读取 localStorage 中的自定义设置，若未配置则回退到 VITE_API_URL 编译常量
 * @returns 服务端基础地址 (未配置时返回空字符串)
 */
export function getServerUrl(): string {
  try {
    const custom = localStorage.getItem(STORAGE_KEY_SERVER_URL)
    if (custom && custom.trim()) {
      return normalizeServerUrl(custom)
    }
  } catch {
    // 忽略 localStorage 读取异常
  }
  return (import.meta.env.VITE_API_URL || '').trim().replace(/\/+$/, '')
}

/**
 * 判断当前是否使用了自定义的服务端地址
 * @returns 是否自定义服务端地址
 */
export function isCustomServerUrl(): boolean {
  try {
    const custom = localStorage.getItem(STORAGE_KEY_SERVER_URL)
    return Boolean(custom && custom.trim())
  } catch {
    return false
  }
}

/**
 * 设置自定义服务端地址并分发变更通知
 * @param url 新的服务端地址 (传空字符串表示清除自定义设置)
 */
export function setServerUrl(url: string): void {
  const normalized = normalizeServerUrl(url)
  try {
    if (normalized) {
      localStorage.setItem(STORAGE_KEY_SERVER_URL, normalized)
    } else {
      localStorage.removeItem(STORAGE_KEY_SERVER_URL)
    }
  } catch {
    // 忽略 localStorage 写入异常
  }

  notifyServerUrlChange(normalized)
}

/**
 * 重置服务端地址为默认配置
 */
export function resetServerUrl(): void {
  setServerUrl('')
}

/**
 * 构造跨环境规范化 API 完整 URL 地址
 * @description 智能处理服务端地址前缀、消除意外的多余 `/api/api/` 重复，并兼容桌面端、Docker 部署与移动端 Capacitor
 * @param path 相对接口路径 (如 '/ai/chat' 或 '/auth/login')
 * @returns 规范拼接后的完整 API 请求 URL
 */
export function buildApiUrl(path: string): string {
  const base = getServerUrl()
  const cleanPath = path.startsWith('/') ? path : `/${path}`

  if (!base) {
    return cleanPath.startsWith('/api/') || cleanPath === '/api' ? cleanPath : `/api${cleanPath}`
  }

  if (base.endsWith('/api')) {
    const subPath = cleanPath.startsWith('/api/') ? cleanPath.slice(4) : (cleanPath === '/api' ? '' : cleanPath)
    return `${base}${subPath}`
  }

  const fullPath = cleanPath.startsWith('/api/') || cleanPath === '/api' ? cleanPath : `/api${cleanPath}`
  return `${base}${fullPath}`
}

/**
 * 获取 Axios 实例的基础 BaseURL 地址
 * @returns BaseURL 字符串
 */
export function getAxiosBaseUrl(): string {
  const base = getServerUrl()
  if (!base) return '/api'
  if (base.endsWith('/api')) return base
  return `${base}/api`
}

/**
 * 服务端连接测试结果结构
 */
export interface ServerConnectionTestResult {
  /** 连接是否成功 */
  ok: boolean
  /** 提示消息 */
  message: string
  /** 服务端版本号 (若返回) */
  version?: string
  /** 系统是否已完成首次初始化 (若返回) */
  initialized?: boolean
  /** 请求往返耗时 (毫秒) */
  latency?: number
}

/**
 * 测试指定服务端地址的网络可达性与 Minefolio 服务端有效性
 * @param targetUrl 待测试的服务端地址 (不传则测试当前配置的地址)
 * @returns 测试结果对象
 */
export async function testServerConnection(targetUrl?: string): Promise<ServerConnectionTestResult> {
  const norm = targetUrl !== undefined ? normalizeServerUrl(targetUrl) : getServerUrl()
  let url = ''
  if (!norm) {
    url = '/api/system/status'
  } else if (norm.endsWith('/api')) {
    url = `${norm}/system/status`
  } else {
    url = `${norm}/api/system/status`
  }

  const startTime = Date.now()
  try {
    const controller = new AbortController()
    const timer = setTimeout(() => controller.abort(), 6000)
    const res = await fetch(url, {
      method: 'GET',
      headers: { Accept: 'application/json' },
      signal: controller.signal,
    })
    clearTimeout(timer)
    const latency = Date.now() - startTime

    if (!res.ok) {
      return {
        ok: false,
        message: `HTTP 状态异常 (${res.status} ${res.statusText})`,
        latency,
      }
    }

    const data = await res.json()
    if (data && typeof data === 'object' && data.code === 0 && data.data) {
      return {
        ok: true,
        message: `连接成功 (${latency}ms${data.data.version ? `，版本 v${data.data.version}` : ''})`,
        version: data.data.version,
        initialized: data.data.initialized,
        latency,
      }
    }

    return {
      ok: false,
      message: '已收到响应，但不是有效的 Minefolio 服务端',
      latency,
    }
  } catch (err: any) {
    const latency = Date.now() - startTime
    if (err.name === 'AbortError') {
      return { ok: false, message: '连接超时 (超过 6 秒)，请检查服务端是否运行', latency }
    }
    if (err.message === 'Failed to fetch') {
      return {
        ok: false,
        message: '无法建立连接：请确认手机与服务端在同一 Wi-Fi，且服务端允许跨域访问',
        latency,
      }
    }
    return { ok: false, message: err.message || '网络连接失败，请确认服务端地址与网络', latency }
  }
}
