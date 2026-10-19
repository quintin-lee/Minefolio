import { describe, it, expect, beforeEach } from 'vitest'
import { getSwrCache, setSwrCache, isSwrStale, clearSwrCache } from '@/utils/swr-cache'

describe('swr-cache utility', () => {
  beforeEach(() => {
    localStorage.clear()
  })

  it('returns fallback value when key does not exist', () => {
    const val = getSwrCache('non_existent', { a: 1 })
    expect(val).toEqual({ a: 1 })
  })

  it('stores and retrieves data with metadata', () => {
    const testData = [{ id: 1, name: 'Food' }]
    setSwrCache('test_key', testData)

    const retrieved = getSwrCache('test_key', [])
    expect(retrieved).toEqual(testData)
  })

  it('handles corrupted JSON in localStorage gracefully without throwing', () => {
    localStorage.setItem('corrupted_key', '{bad-json...')
    const fallback = ['default']
    const result = getSwrCache('corrupted_key', fallback)
    expect(result).toEqual(fallback)
  })

  it('accurately identifies stale cache based on maxAgeMs', () => {
    setSwrCache('stale_test', 'value')
    expect(isSwrStale('stale_test', 10000)).toBe(false)

    // Simulate an old timestamp in cache
    const raw = JSON.parse(localStorage.getItem('stale_test') || '{}')
    raw.cachedAt = Date.now() - 20000
    localStorage.setItem('stale_test', JSON.stringify(raw))

    expect(isSwrStale('stale_test', 10000)).toBe(true)
  })

  it('clears specific cache keys or prefix matching keys', () => {
    setSwrCache('minefolio_cache_a', 1)
    setSwrCache('minefolio_cache_b', 2)
    setSwrCache('other_key', 3)

    clearSwrCache('minefolio_cache_a')
    expect(getSwrCache('minefolio_cache_a', null)).toBeNull()
    expect(getSwrCache('minefolio_cache_b', null)).toBe(2)

    clearSwrCache('minefolio_cache_')
    expect(getSwrCache('minefolio_cache_b', null)).toBeNull()
    expect(getSwrCache('other_key', null)).toBe(3)
  })
})
