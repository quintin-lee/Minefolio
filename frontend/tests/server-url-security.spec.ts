import { describe, it, expect } from 'vitest'
import { isValidServerUrl } from '@/utils/server-url'

describe('server-url security validation', () => {
  it('allows empty url as default', () => {
    expect(isValidServerUrl('')).toBe(true)
    expect(isValidServerUrl('   ')).toBe(true)
  })

  it('rejects dangerous pseudo-protocols', () => {
    expect(isValidServerUrl('javascript:alert(1)')).toBe(false)
    expect(isValidServerUrl('data:text/html,bad')).toBe(false)
    expect(isValidServerUrl('file:///etc/passwd')).toBe(false)
  })

  it('allows localhost and local loopback with http or https', () => {
    expect(isValidServerUrl('http://localhost:8080')).toBe(true)
    expect(isValidServerUrl('https://localhost:8080')).toBe(true)
    expect(isValidServerUrl('http://127.0.0.1:8080')).toBe(true)
    expect(isValidServerUrl('http://127.0.0.1')).toBe(true)
  })

  it('allows LAN private IPs with http', () => {
    expect(isValidServerUrl('http://192.168.1.50:8080')).toBe(true)
    expect(isValidServerUrl('http://10.0.0.5:8080')).toBe(true)
    expect(isValidServerUrl('http://172.20.0.2:8080')).toBe(true)
    expect(isValidServerUrl('http://minefolio.local:8080')).toBe(true)
  })

  it('rejects public domains and public IPs over unencrypted plain http', () => {
    expect(isValidServerUrl('http://api.minefolio.com')).toBe(false)
    expect(isValidServerUrl('http://8.8.8.8:8080')).toBe(false)
  })

  it('allows public domains over https', () => {
    expect(isValidServerUrl('https://api.minefolio.com')).toBe(true)
    expect(isValidServerUrl('https://my-nas.example.org:8443')).toBe(true)
  })
})
