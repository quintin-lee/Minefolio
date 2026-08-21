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

/**
 * Robust cookie parser — handles all standard Set-Cookie attribute orders
 * and avoids fragile split-based approaches that break when HttpOnly / SameSite
 * attributes are added by the server.
 */
function getCookie(name: string): string | null {
  const cookie = document.cookie || ''
  const re = new RegExp('(?:^|;\\s*)' + name.replace(/[.*+?^${}()|[\\]\\.]/g, '\\$&') + '=([^;]*)')
  const match = cookie.match(re)
  return match && match[1] ? decodeURIComponent(match[1]) : null
}

function createHttp(): AxiosInstance {
  const instance = axios.create({
    baseURL: import.meta.env.VITE_API_URL,
    timeout: 10000,
  })

  // Request interceptor: inject token + CSRF header
  instance.interceptors.request.use((config) => {
    const auth = useAuthStore()
    if (auth.token) {
      config.headers.Authorization = `Bearer ${auth.token}`
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
          useAuthStore().logout()
          if (!mobileMode) window.location.href = '/login'
        } else {
          ElMessage.error(body.message || '请求失败')
        }
        return Promise.reject({ response: { data: body } })
      }
      return (body?.data ?? body) as any
    },
    (err) => {
      if (err.response) {
        const code = err.response.data?.code
        if (code === 1001) {
          useAuthStore().logout()
          if (!mobileMode) window.location.href = '/login'
        } else if (code) {
          ElMessage.error(err.response.data.message || '请求失败')
        }
      } else if (!mobileMode) {
        ElMessage.error('网络错误')
      }
      return Promise.reject(err)
    }
  )

  return instance
}

export default createHttp()
