/**
 * @file Axios HTTP 统一网络客户端
 * @description 封装 Axios 实例，自动解包响应数据外壳 ({ code, message, data } -> data)，自动注入 JWT Bearer Token、X-Ledger-Id 与 X-CSRF-Token，
 * 统一处理 1001 鉴权失效重定向与错误防抖 Toast 提示，并支持桌面端与移动端多平台适配。
 */

import axios from 'axios'
import type { AxiosInstance, AxiosResponse, AxiosRequestConfig } from 'axios'

/** 后端统一响应信封 */
export interface ApiEnvelope<T = unknown> {
  code: number
  message: string
  data: T
}

/**
 * 解包后端响应信封 ({ code, message, data } -> data)。
 * 全站唯一的网络边界类型断点：调用方以 http.get<T>/post<T>/put<T>/delete<T> 声明解包后载荷类型，
 * 此处 unknown -> T 断言承载该约定。当存在显式 data 字段（包括 null、0、false、""）时返回 data，
 * 仅在 data 字段缺失或为 undefined 时回退返回信封整体。
 */
export function unwrapEnvelope<T>(body: unknown): T {
  if (body && typeof body === 'object' && 'data' in body) {
    const data = (body as { data?: unknown }).data
    if (data !== undefined) {
      return data as T
    }
  }
  return body as T
}
import { useAuthStore } from '@/stores/auth'
import { ElMessage } from 'element-plus'

/** 移动端运行模式标识 */
let mobileMode = false

/**
 * 设置移动端模式开关 (关闭桌面式全局 401 路由强行跳转与网络错误 Toast，交由离线同步机制处理)
 * @param on 是否启用移动端模式
 */
export function setMobileMode(on: boolean): void {
  mobileMode = on
}

/**
 * 获取当前是否处于移动端运行模式
 */
export function isMobileMode(): boolean {
  return mobileMode
}

/**
 * 判断错误是否为无服务端响应的纯网络/超时故障（排除已有 HTTP response 及代码调用 TypeError）
 */
export function isNetworkError(err: unknown): boolean {
  if (!err || typeof err !== 'object') return false
  const e = err as { response?: unknown; message?: unknown }
  if (e.response !== undefined && e.response !== null) return false
  if (err instanceof TypeError && String(e.message || '').includes('is not a function')) {
    return false
  }
  return true
}

/**
 * 移动端未接入离线本地库的只读视图在请求失败时调用：抑制重复弹窗并在断网时给出明确离线提示
 */
export function notifyMobileReadError(err: unknown, moduleLabel: string): void {
  if (!isNetworkError(err)) {
    return
  }
  showDebouncedError(`${moduleLabel}加载失败：当前离线或无法连接服务端`)
}

/** 上次错误提示内容 */
let lastErrorMsg = ''
/** 上次错误提示时间戳 */
let lastErrorTime = 0

/**
 * 带有防抖功能的错误消息弹出提示 (1.5秒内相同错误信息不重复弹窗)
 * @param msg 错误文本
 */
function showDebouncedError(msg: string) {
  const now = Date.now()
  if (msg === lastErrorMsg && now - lastErrorTime < 1500) {
    return
  }
  lastErrorMsg = msg
  lastErrorTime = now
  ElMessage.error(msg)
}

/** 是否正在处理 401 鉴权未授权跳转 */
let isHandlingAuthError = false

/** 自定义 401 鉴权失效处理钩子 (由 main 或 main-mobile 注入特定平台的 router 导航) */
type AuthErrorHandler = () => void
let customAuthErrorHandler: AuthErrorHandler | null = null

/**
 * 注入自定义 401 鉴权失效处理钩子
 * @param handler 回调函数
 */
export function setAuthErrorHandler(handler: AuthErrorHandler | null): void {
  customAuthErrorHandler = handler
}

/**
 * 处理 1001 登录鉴权失效：注销状态并安全导航回登录页
 */
export function handleAuthError() {
  if (isHandlingAuthError) return
  isHandlingAuthError = true
  useAuthStore().logout()

  if (customAuthErrorHandler) {
    try {
      customAuthErrorHandler()
    } finally {
      setTimeout(() => {
        isHandlingAuthError = false
      }, 1000)
    }
    return
  }

  if (!mobileMode) {
    if (typeof window !== 'undefined' && window.location.pathname !== '/login') {
      window.location.href = '/login'
    }
  }
  setTimeout(() => {
    isHandlingAuthError = false
  }, 1000)
}

export {
  STORAGE_KEY_SERVER_URL,
  onServerUrlChange,
  normalizeServerUrl,
  isValidServerUrl,
  getServerUrl,
  isCustomServerUrl,
  setServerUrl,
  resetServerUrl,
  buildApiUrl,
  getAxiosBaseUrl,
  testServerConnection,
  type ServerConnectionTestResult,
  type ServerUrlChangeHook,
} from './server-url'
import { getAxiosBaseUrl, onServerUrlChange } from './server-url'



/**
 * 从 document.cookie 中安全提取指定 Cookie 值
 * @description 使用正则匹配完整 Cookie 名称与值，避免因 HttpOnly / SameSite 等顺序变化导致的分隔符解析失败
 * @param name Cookie 键名 (如 'csrf_token')
 * @returns 解码后的 Cookie 字符串值，若不存在返回 null
 */
export function getCookie(name: string): string | null {
  const cookie = document.cookie || ''
  const re = new RegExp('(?:^|;\\s*)' + name.replace(/[.*+?^${}()|[\\]\\.]/g, '\\$&') + '=([^;]*)')
  const match = cookie.match(re)
  return match && match[1] ? decodeURIComponent(match[1]) : null
}

function isBlobLike(data: unknown): data is Blob {
  return (
    (typeof Blob !== 'undefined' && data instanceof Blob) ||
    Object.prototype.toString.call(data) === '[object Blob]'
  )
}

async function readBlobText(blob: Blob): Promise<string> {
  if (typeof blob.text === 'function') {
    return blob.text()
  }
  return new Promise<string>((resolve, reject) => {
    const reader = new FileReader()
    reader.onload = () => resolve(String(reader.result ?? ''))
    reader.onerror = () => reject(reader.error ?? new Error('Failed to read blob'))
    reader.readAsText(blob)
  })
}

async function parseBlobErrorBody(data: unknown): Promise<ApiEnvelope | undefined> {
  if (isBlobLike(data) && typeof data.type === 'string' && data.type.includes('application/json')) {
    try {
      const text = await readBlobText(data)
      return JSON.parse(text) as ApiEnvelope
    } catch {
      return undefined
    }
  }
  return undefined
}

/**
 * 创建并配置预置拦截器的 Axios 实例
 * @returns AxiosInstance
 */
function createHttp(): AxiosInstance {
  const instance = axios.create({
    baseURL: getAxiosBaseUrl(),
    timeout: 10000,
    // 鉴权走 JWT Bearer (Authorization 头)，CSRF 走自定义 X-CSRF-Token 头 (拦截器手动注入)，
    // 均不依赖 cookie。withCredentials=true 会强制 CORS 响应必须带
    // Access-Control-Allow-Credentials + 精确 origin，在 Capacitor WebView 下反向代理
    // 未配置该响应头时请求被 CORS 拦截 (抛无 response 的 TypeError)，表现为
    // "测试连接成功 (fetch 不受 withCredentials 约束) 但登录失败 (axios 被拦)"。
    // 桌面浏览器与移动端统一关闭，彻底消除该差异。
    withCredentials: false,
  })

  // 请求拦截器：自动注入最新 baseURL、JWT Bearer Token、X-Ledger-Id 及 CSRF Token
  instance.interceptors.request.use((config) => {
    config.baseURL = getAxiosBaseUrl()
    const auth = useAuthStore()
    if (auth.token) {
      config.headers.Authorization = `Bearer ${auth.token}`
    }
    const activeLedgerId = localStorage.getItem('minefolio_active_ledger_id')
    const hasLedgerHeader = (config.headers as any)?.['X-Ledger-Id'] || (config.headers as any)?.['x-ledger-id']
    if (activeLedgerId && !hasLedgerHeader) {
      config.headers['X-Ledger-Id'] = activeLedgerId
    }
    const method = (config.method || 'get').toUpperCase()
    if (method !== 'GET' && method !== 'HEAD' && method !== 'OPTIONS') {
      const csrf = getCookie('csrf_token')
      if (csrf) {
        config.headers['X-CSRF-Token'] = csrf
      }
    }
    return config
  })

  // 响应拦截器：统一处理业务异常与鉴权失败；信封解包由 http.get/post/put/delete 包装函数完成
  instance.interceptors.response.use(
    async (res: AxiosResponse) => {
      const parsedBlob = await parseBlobErrorBody(res.data)
      const body = (parsedBlob ?? res.data) as ApiEnvelope | undefined
      if (body && typeof body === 'object' && typeof body.code === 'number' && body.code !== 0) {
        if (body.code === 1001) {
          handleAuthError()
        } else {
          showDebouncedError(body.message || '请求失败')
        }
        return Promise.reject({ response: { data: body } })
      }
      return res
    },
    async (err) => {
      if (err.response) {
        const parsedBlob = await parseBlobErrorBody(err.response.data)
        if (parsedBlob) {
          err.response.data = parsedBlob
        }
        const code = err.response.data?.code
        if (code === 1001 || err.response.status === 401) {
          handleAuthError()
        } else if (code) {
          showDebouncedError(err.response.data.message || '请求失败')
        } else if (err?.code !== 'ERR_CANCELED') {
          const msg =
            err.response.data?.message ||
            err.response.statusText ||
            (err.response.status ? `请求失败 (${err.response.status})` : '请求失败')
          showDebouncedError(msg)
        }
      } else if (!mobileMode && err?.code !== 'ERR_CANCELED') {
        showDebouncedError('网络错误')
      }
      return Promise.reject(err)
    }
  )

  return instance
}

const httpInstance: AxiosInstance = createHttp()

// 当服务端地址发生变更时自动更新 Axios 实例的默认 baseURL
onServerUrlChange(() => {
  httpInstance.defaults.baseURL = getAxiosBaseUrl()
})

/**
 * 类型化请求包装：返回解包后的 data（Promise<T>），调用方以单一泛型声明载荷类型。
 * 解包约定见 unwrapEnvelope；拦截器仅负责错误映射，不再改写响应体。
 */
const http = {
  get: <T>(url: string, config?: AxiosRequestConfig) =>
    httpInstance.get<T>(url, config).then((res) => unwrapEnvelope<T>(res.data)),
  post: <T>(url: string, data?: unknown, config?: AxiosRequestConfig) =>
    httpInstance.post<T>(url, data, config).then((res) => unwrapEnvelope<T>(res.data)),
  put: <T>(url: string, data?: unknown, config?: AxiosRequestConfig) =>
    httpInstance.put<T>(url, data, config).then((res) => unwrapEnvelope<T>(res.data)),
  delete: <T>(url: string, config?: AxiosRequestConfig) =>
    httpInstance.delete<T>(url, config).then((res) => unwrapEnvelope<T>(res.data)),
  request: <T>(config: AxiosRequestConfig) =>
    httpInstance.request<T>(config).then((res) => unwrapEnvelope<T>(res.data)),
}

export default http

