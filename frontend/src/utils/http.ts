import axios from 'axios'
import type { AxiosInstance, AxiosResponse } from 'axios'
import { useAuthStore } from '@/stores/auth'
import { ElMessage } from 'element-plus'

/**
 * Wraps axios to unwrap the { code, message, data } envelope automatically.
 * Returns T (the first type parameter) instead of AxiosResponse<T>.
 */
let mobileMode = false
/** 移动端适配：关闭桌面式登录跳转与网络错误 toast，交由 offline-http 处理。 */
export function setMobileMode(on: boolean): void {
  mobileMode = on
}

let lastErrorMsg = ''
let lastErrorTime = 0
function showDebouncedError(msg: string) {
  const now = Date.now()
  if (msg === lastErrorMsg && now - lastErrorTime < 1500) {
    return
  }
  lastErrorMsg = msg
  lastErrorTime = now
  ElMessage.error(msg)
}

let isHandlingAuthError = false
function handleAuthError() {
  if (isHandlingAuthError) return
  isHandlingAuthError = true
  useAuthStore().logout()
  if (!mobileMode) {
    import('@/router')
      .then(({ default: router }) => {
        if (router && router.currentRoute?.value?.path !== '/login') {
          router.push('/login').finally(() => {
            setTimeout(() => {
              isHandlingAuthError = false
            }, 1000)
          })
        } else {
          setTimeout(() => {
            isHandlingAuthError = false
          }, 1000)
        }
      })
      .catch(() => {
        if (window.location.pathname !== '/login') {
          window.location.href = '/login'
        }
        setTimeout(() => {
          isHandlingAuthError = false
        }, 1000)
      })
  } else {
    setTimeout(() => {
      isHandlingAuthError = false
    }, 1000)
  }
}

/**
 * Helper to build a normalized API URL from path, handling VITE_API_URL prefix,
 * preventing accidental duplicate `/api/api/` occurrences, and supporting both
 * absolute URLs and relative paths across desktop, docker, and mobile Capacitor.
 */
export function buildApiUrl(path: string): string {
  const base = (import.meta.env.VITE_API_URL || '').replace(/\/+$/, '')
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

function getAxiosBaseUrl(): string {
  const base = (import.meta.env.VITE_API_URL || '').replace(/\/+$/, '')
  if (!base) return '/api'
  if (base.endsWith('/api')) return base
  return `${base}/api`
}

/**
 * Robust cookie parser — handles all standard Set-Cookie attribute orders
 * and avoids fragile split-based approaches that break when HttpOnly / SameSite
 * attributes are added by the server.
 */
export function getCookie(name: string): string | null {
  const cookie = document.cookie || ''
  const re = new RegExp('(?:^|;\\s*)' + name.replace(/[.*+?^${}()|[\\]\\.]/g, '\\$&') + '=([^;]*)')
  const match = cookie.match(re)
  return match && match[1] ? decodeURIComponent(match[1]) : null
}

function createHttp(): AxiosInstance {
  const instance = axios.create({
    baseURL: getAxiosBaseUrl(),
    timeout: 10000,
    withCredentials: true,
  })
  // Request interceptor: inject token + CSRF header
  instance.interceptors.request.use((config) => {
    const auth = useAuthStore()
    if (auth.token) {
      config.headers.Authorization = `Bearer ${auth.token}`
    }
    const activeLedgerId = localStorage.getItem('minefolio_active_ledger_id')
    if (activeLedgerId) {
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

  // Response interceptor: unwrap { code, message, data } → data
  instance.interceptors.response.use(
    (res: AxiosResponse) => {
      const body = res.data
      if (body && typeof body === 'object' && typeof body.code === 'number' && body.code !== 0) {
        if (body.code === 1001) {
          handleAuthError()
        } else {
          showDebouncedError(body.message || '请求失败')
        }
        return Promise.reject({ response: { data: body } })
      }
      return (body?.data ?? body) as any
    },
    (err) => {
      if (err.response) {
        const code = err.response.data?.code
        if (code === 1001) {
          handleAuthError()
        } else if (code) {
          showDebouncedError(err.response.data.message || '请求失败')
        }
      } else if (!mobileMode) {
        showDebouncedError('网络错误')
      }
      return Promise.reject(err)
    }
  )

  return instance
}

export default createHttp()
