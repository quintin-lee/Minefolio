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

// Response interceptor: unwrap the { code, message, data } envelope
http.interceptors.response.use(
  (res) => res.data.data,
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
