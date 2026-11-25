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

  it('falls back to pure-JS RSA-OAEP-SHA256 when crypto.subtle is unavailable (HTTP domain)', async () => {
    const nodeCrypto = await import('crypto')
    const { publicKey, privateKey } = nodeCrypto.generateKeyPairSync('rsa', {
      modulusLength: 2048,
    })
    const exportedJwk = publicKey.export({ format: 'jwk' }) as { kty: string; n: string; e: string }

    global.fetch = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        code: 0,
        data: { public_key: exportedJwk },
      }),
    }) as any

    const origCrypto = globalThis.crypto
    vi.stubGlobal('crypto', {
      getRandomValues: origCrypto.getRandomValues.bind(origCrypto),
      subtle: undefined,
    })
    try {
      const secret = 'HttpDomainPass123!中文'
      const cipherB64Url = await encryptPassword(secret)
      expect(cipherB64Url).toBeTruthy()
      expect(cipherB64Url).not.toMatch(/[+/=]/)

      const b64 = cipherB64Url.replace(/-/g, '+').replace(/_/g, '/')
      const padded = b64 + '='.repeat((4 - (b64.length % 4)) % 4)
      const cipherBuf = Buffer.from(padded, 'base64')
      const decrypted = nodeCrypto.privateDecrypt(
        {
          key: privateKey,
          padding: nodeCrypto.constants.RSA_PKCS1_OAEP_PADDING,
          oaepHash: 'sha256',
        },
        cipherBuf
      )
      expect(decrypted.toString('utf8')).toBe(secret)
      expect(global.fetch).toHaveBeenCalledTimes(1)

      // Cached fallback key avoids re-fetching on second encryption
      await encryptPassword('secondHttpPass')
      expect(global.fetch).toHaveBeenCalledTimes(1)
    } finally {
      vi.unstubAllGlobals()
    }
  })

  it('throws clear WebCrypto error when both crypto.subtle and getRandomValues are unavailable', async () => {
    const origCrypto = globalThis.crypto
    vi.stubGlobal('crypto', { ...origCrypto, subtle: undefined, getRandomValues: undefined })
    try {
      await expect(encryptPassword('myPassword123')).rejects.toThrow(/WebCrypto/)
      expect(global.fetch).not.toHaveBeenCalled()
    } finally {
      vi.unstubAllGlobals()
    }
  })
})
