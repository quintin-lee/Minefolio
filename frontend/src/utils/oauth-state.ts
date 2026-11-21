const OAUTH_STATE_STORAGE_KEY = 'minefolio_oauth_state'

function generateOAuthState(): string {
  if (typeof globalThis.crypto !== 'undefined' && typeof globalThis.crypto.getRandomValues === 'function') {
    const bytes = new Uint8Array(16)
    globalThis.crypto.getRandomValues(bytes)
    return Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('')
  }
  return `${Date.now().toString(36)}_${Math.random().toString(36).slice(2)}`
}

/**
 * 为第三方 OAuth 授权跳转 URL 附加单次防 CSRF `state` 参数并持久化至 sessionStorage
 */
export function buildOAuthAuthUrl(rawUrl: string): string {
  const state = generateOAuthState()
  try {
    sessionStorage.setItem(OAUTH_STATE_STORAGE_KEY, state)
  } catch {
    // 忽略无痕模式等存储受限错误
  }

  try {
    const base = typeof window !== 'undefined' ? window.location.origin : 'http://localhost'
    const parsed = new URL(rawUrl, base)
    parsed.searchParams.set('state', state)
    return /^https?:\/\//i.test(rawUrl) ? parsed.toString() : `${parsed.pathname}${parsed.search}${parsed.hash}`
  } catch {
    const sep = rawUrl.includes('?') ? '&' : '?'
    return `${rawUrl}${sep}state=${encodeURIComponent(state)}`
  }
}

/**
 * 消费并校验 OAuth 回调携带的 `state` 参数，防止登录 CSRF 攻击
 */
export function consumeAndVerifyOAuthState(receivedState: string): boolean {
  let expectedState: string | null = null
  try {
    expectedState = sessionStorage.getItem(OAUTH_STATE_STORAGE_KEY)
    sessionStorage.removeItem(OAUTH_STATE_STORAGE_KEY)
  } catch {
    return false
  }
  if (!expectedState || !receivedState) {
    return false
  }
  return receivedState === expectedState
}
