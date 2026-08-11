import axios from 'axios'
import { useAuthStore } from '@/stores/auth'
import { ElMessage } from 'element-plus'

const http = axios.create({
  baseURL: import.meta.env.VITE_API_URL,
  timeout: 10000,
})

// Request interceptor: inject token + CSRF header (double-submit)
http.interceptors.request.use((config) => {
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

// Response interceptor: unwrap the { code, message, data } envelope.
// The backend returns HTTP 200 even for business errors, so code !== 0 must
// be treated as a failure here — otherwise callers can't distinguish them.
http.interceptors.response.use(
  (res) => {
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
    return body?.data
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

export default http
