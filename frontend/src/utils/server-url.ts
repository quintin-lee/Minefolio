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

/**
 * 规范化服务端 URL 格式
 * @description 去除首尾空白，若缺少 http/https 协议则默认补齐 http://，去除末尾斜杠
 * @param url 待规范化的 URL 字符串
 * @returns 规范化后的 URL 字符串
 */
export function normalizeServerUrl(url: string): string {
  let trimmed = (url || '').trim()
  if (!trimmed) return ''
  if (!/^https?:\/\//i.test(trimmed)) {
    trimmed = `http://${trimmed}`
  }
  return trimmed.replace(/\/+$/, '')
}

/**
 * 校验服务端 URL 字符串是否有效
 * @param url URL 字符串
 * @returns 是否为有效格式 (空字符串视为使用默认地址，返回 true)
 */
export function isValidServerUrl(url: string): boolean {
  const trimmed = (url || '').trim()
  if (!trimmed) return true
  try {
    const normalized = normalizeServerUrl(trimmed)
    const parsed = new URL(normalized)
    return Boolean(parsed.host)
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
    return { ok: false, message: err.message || '网络连接失败，请确认服务端地址与网络', latency }
  }
}
