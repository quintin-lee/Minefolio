import axios from 'axios'
import type { AxiosInstance, AxiosResponse } from 'axios'
import { useAuthStore } from '@/stores/auth'
import { ElMessage } from 'element-plus'

/**
 * Wraps axios to unwrap the { code, message, data } envelope automatically.
 * Returns T (the first type parameter) instead of AxiosResponse<T>.
 */
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
      const csrf = document.cookie.split('; ').find((r) => r.startsWith('csrf_token='))?.split('=')[1]
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
          window.location.href = '/login'
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
          window.location.href = '/login'
        } else if (code) {
          ElMessage.error(err.response.data.message || '请求失败')
        }
      } else {
        ElMessage.error('网络错误')
      }
      return Promise.reject(err)
    }
  )

  return instance
}

export default createHttp()
