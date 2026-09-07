import { describe, it, expect, beforeEach, vi } from 'vitest'
import { encryptPassword, clearCryptoKeyCache, encryptText } from '@/utils/crypto'

describe('crypto RSA utility', () => {
  const mockJwk = {
    kty: 'RSA',
    n: 'u1lq',
    e: 'AQAB',
  }

  beforeEach(() => {
    clearCryptoKeyCache()
    vi.clearAllMocks()

    global.fetch = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        code: 0,
        data: {
          public_key: mockJwk,
        },
      }),
    }) as any

    vi.spyOn(crypto.subtle, 'importKey').mockResolvedValue({} as CryptoKey)
    vi.spyOn(crypto.subtle, 'encrypt').mockResolvedValue(new Uint8Array([1, 2, 3]).buffer)
  })

  it('encrypts password and returns base64url string', async () => {
    const res = await encryptPassword('myPassword123')
    expect(res).toBeTruthy()
    // Should not contain + / =
    expect(res).not.toMatch(/[+/=]/)
    expect(global.fetch).toHaveBeenCalledTimes(1)
  })

  it('caches the public key and avoids repeated fetch and importKey calls', async () => {
    await encryptPassword('password1')
    await encryptPassword('password2')
    await encryptPassword('password3')

    expect(global.fetch).toHaveBeenCalledTimes(1)
    expect(crypto.subtle.importKey).toHaveBeenCalledTimes(1)
  })

  it('handles concurrent encryption calls by sharing the same in-flight promise', async () => {
    const [res1, res2] = await Promise.all([
      encryptPassword('pwd1'),
      encryptPassword('pwd2'),
    ])

    expect(res1).toBeTruthy()
    expect(res2).toBeTruthy()
    expect(global.fetch).toHaveBeenCalledTimes(1)
    expect(crypto.subtle.importKey).toHaveBeenCalledTimes(1)
  })

  it('re-fetches key after clearCryptoKeyCache', async () => {
    await encryptPassword('first')
    expect(global.fetch).toHaveBeenCalledTimes(1)

    clearCryptoKeyCache()
    await encryptPassword('second')
    expect(global.fetch).toHaveBeenCalledTimes(2)
  })

  it('exports encryptText as an alias to encryptPassword', () => {
    expect(encryptText).toBe(encryptPassword)
  })
})
