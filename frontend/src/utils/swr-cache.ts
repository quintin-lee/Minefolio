interface SwrEnvelope<T> {
  data: T
  cachedAt: number
  ttl?: number
}

const DEFAULT_SWR_PREFIX = 'minefolio_cache_'

/**
 * 从本地存储读取 SWR 缓存数据，不存在或异常时回退到默认值
 * @param key 存储键名
 * @param fallback 默认回退值
 * @returns 缓存数据或回退值
 */
export function getSwrCache<T>(key: string, fallback: T): T {
  if (typeof localStorage === 'undefined') return fallback
  try {
    const raw = localStorage.getItem(key)
    if (!raw) return fallback
    const parsed = JSON.parse(raw) as SwrEnvelope<T>
    if (parsed && typeof parsed === 'object' && 'data' in parsed && 'cachedAt' in parsed) {
      return parsed.data
    }
    return (parsed as unknown as T) ?? fallback
  } catch (err) {
    console.warn(`[swr-cache] failed to parse cache for ${key}:`, err)
    return fallback
  }
}

/**
 * 写入数据到 SWR 缓存中，附带时间戳元数据
 * @param key 存储键名
 * @param data 业务数据
 * @param ttlMs 可选的过期毫秒数
 */
export function setSwrCache<T>(key: string, data: T, ttlMs?: number): void {
  if (typeof localStorage === 'undefined') return
  try {
    const envelope: SwrEnvelope<T> = {
      data,
      cachedAt: Date.now(),
      ttl: ttlMs,
    }
    localStorage.setItem(key, JSON.stringify(envelope))
  } catch (err) {
    console.warn(`[swr-cache] failed to set cache for ${key}:`, err)
  }
}

/**
 * 检查指定缓存是否已经超过指定时长
 * @param key 存储键名
 * @param maxAgeMs 最大允许有效毫秒数
 * @returns true 表示过期需要刷新，false 表示仍在保鲜期
 */
export function isSwrStale(key: string, maxAgeMs: number): boolean {
  if (typeof localStorage === 'undefined') return true
  try {
    const raw = localStorage.getItem(key)
    if (!raw) return true
    const parsed = JSON.parse(raw) as SwrEnvelope<unknown>
    if (!parsed || typeof parsed.cachedAt !== 'number') return true
    return Date.now() - parsed.cachedAt > maxAgeMs
  } catch {
    return true
  }
}

/**
 * 清除匹配指定前缀或键名的 SWR 缓存
 * @param prefixOrExact 键名或前缀 (省略时清除所有 minefolio_cache_ 前缀项)
 */
export function clearSwrCache(prefixOrExact?: string): void {
  if (typeof localStorage === 'undefined') return
  try {
    if (!prefixOrExact) {
      prefixOrExact = DEFAULT_SWR_PREFIX
    }
    const toRemove: string[] = []
    for (let i = 0; i < localStorage.length; i++) {
      const k = localStorage.key(i)
      if (k && (k === prefixOrExact || k.startsWith(prefixOrExact))) {
        toRemove.push(k)
      }
    }
    toRemove.forEach((k) => localStorage.removeItem(k))
  } catch (err) {
    console.warn('[swr-cache] failed to clear cache:', err)
  }
}
