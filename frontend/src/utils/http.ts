/**
 * @file Axios HTTP 统一网络客户端
 * @description 封装 Axios 实例，自动解包响应数据外壳 ({ code, message, data } -> data)，自动注入 JWT Bearer Token、X-Ledger-Id 与 X-CSRF-Token，
 * 统一处理 1001 鉴权失效重定向与错误防抖 Toast 提示，并支持桌面端与移动端多平台适配。
 */

import axios from 'axios'
import type { AxiosInstance, AxiosResponse } from 'axios'
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

/**
 * 处理 1001 登录鉴权失效：注销状态并安全导航回登录页
 */
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
 * 构造跨环境规范化 API 完整 URL 地址
 * @description 智能处理 VITE_API_URL 前缀、消除意外的多余 `/api/api/` 重复，并兼容桌面端、Docker 部署与移动端 Capacitor
 * @param path 相对接口路径 (如 '/ai/chat' 或 '/auth/login')
 * @returns 规范拼接后的完整 API 请求 URL
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

/**
 * 获取 Axios 实例的基础 BaseURL 地址
 * @returns BaseURL 字符串
 */
function getAxiosBaseUrl(): string {
  const base = (import.meta.env.VITE_API_URL || '').replace(/\/+$/, '')
  if (!base) return '/api'
  if (base.endsWith('/api')) return base
  return `${base}/api`
}

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

/**
 * 创建并配置预置拦截器的 Axios 实例
 * @returns AxiosInstance
 */
function createHttp(): AxiosInstance {
  const instance = axios.create({
    baseURL: getAxiosBaseUrl(),
    timeout: 10000,
    withCredentials: true,
  })

  // 请求拦截器：自动注入 JWT Bearer Token、X-Ledger-Id 及 CSRF Token
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

  // 响应拦截器：自动解包 { code, message, data } 结构，并处理业务异常与鉴权失败
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

