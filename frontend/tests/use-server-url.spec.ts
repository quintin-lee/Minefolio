import { describe, it, expect, beforeEach, afterEach } from 'vitest'
import { useServerUrl } from '@/composables/useServerUrl'
import { STORAGE_KEY_SERVER_URL } from '@/utils/http'

describe('useServerUrl composable', () => {
  const { serverUrl, isCustom, displayUrl, setUrl, resetUrl, isValidServerUrl, normalizeServerUrl } = useServerUrl()

  beforeEach(() => {
    localStorage.removeItem(STORAGE_KEY_SERVER_URL)
    resetUrl()
  })

  afterEach(() => {
    localStorage.removeItem(STORAGE_KEY_SERVER_URL)
    resetUrl()
  })

  it('provides default state correctly', () => {
    expect(serverUrl.value).toBe('')
    expect(isCustom.value).toBe(false)
    expect(displayUrl.value).toContain('默认')
  })

  it('updates state reactively when setUrl is called', () => {
    setUrl('http://192.168.1.120:8080')
    expect(serverUrl.value).toBe('http://192.168.1.120:8080')
    expect(isCustom.value).toBe(true)
    expect(displayUrl.value).toBe('http://192.168.1.120:8080')
  })

  it('resets state when resetUrl is called', () => {
    setUrl('http://192.168.1.120:8080')
    expect(isCustom.value).toBe(true)
    resetUrl()
    expect(serverUrl.value).toBe('')
    expect(isCustom.value).toBe(false)
  })

  it('normalizes missing protocol on setUrl', () => {
    setUrl('my-server.com:8080')
    expect(serverUrl.value).toBe('http://my-server.com:8080')
  })

  it('exposes validator and normalizer', () => {
    expect(isValidServerUrl('http://foo.bar:8080')).toBe(true)
    expect(isValidServerUrl('http://')).toBe(false)
    expect(normalizeServerUrl('foo.bar///')).toBe('http://foo.bar')
  })
})
