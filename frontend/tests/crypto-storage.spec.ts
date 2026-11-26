import { describe, it, expect, beforeEach, vi } from 'vitest'
import {
  encryptPayload,
  decryptPayload,
  CryptoStorageEncryptError,
  CryptoStorageDecryptError,
  resetInMemoryCryptoKey,
} from '@/utils/crypto-storage'

describe('crypto-storage', () => {
  beforeEach(() => {
    resetInMemoryCryptoKey()
  })

  it('encrypts Uint8Array with extractable:false CryptoKey and returns enc:v1 prefix without globalThis exposure', async () => {
    const data = new Uint8Array([1, 2, 3, 4, 5, 42, 99])
    const encrypted = await encryptPayload(data)
    expect(encrypted.startsWith('enc:v1:')).toBe(true)
    expect((globalThis as Record<string, unknown>).__minefolio_master_raw_key__).toBeUndefined()

    const decrypted = await decryptPayload(encrypted)
    expect(decrypted).toEqual(data)

    // Verify key persisted directly in IndexedDB is non-extractable
    const storedKey = await new Promise<CryptoKey | null>((resolve) => {
      const req = indexedDB.open('minefolio_keystore', 1)
      req.onsuccess = () => {
        const tx = req.result.transaction('keys', 'readonly')
        const getReq = tx.objectStore('keys').get('db_master_key_v1')
        getReq.onsuccess = () => resolve((getReq.result as CryptoKey) ?? null)
      }
    })
    expect(storedKey).not.toBeNull()
    expect(storedKey?.extractable).toBe(false)
  })

  it('throws CryptoStorageEncryptError instead of downgrading to b64: when WebCrypto is unavailable', async () => {
    const origCrypto = globalThis.crypto
    vi.stubGlobal('crypto', { ...origCrypto, subtle: undefined })
    try {
      await expect(encryptPayload(new Uint8Array([1, 2, 3]))).rejects.toBeInstanceOf(CryptoStorageEncryptError)
    } finally {
      vi.unstubAllGlobals()
    }
  })

  it('transparently decodes legacy b64: payload', async () => {
    const original = new Uint8Array([100, 101, 102])
    const b64 = 'b64:' + btoa(String.fromCharCode(...original))
    const decrypted = await decryptPayload(b64)
    expect(decrypted).toEqual(original)
  })

  it('transparently decodes legacy JSON array payload', async () => {
    const original = new Uint8Array([1, 2, 3])
    const jsonSaved = JSON.stringify(Array.from(original))
    const decrypted = await decryptPayload(jsonSaved)
    expect(decrypted).toEqual(original)
  })

  it('throws CryptoStorageDecryptError instead of falling back when enc:v1 payload is corrupted', async () => {
    await expect(decryptPayload('enc:v1:AAAA:invalidCiphertext')).rejects.toBeInstanceOf(CryptoStorageDecryptError)
    await expect(decryptPayload('enc:v1:broken')).rejects.toBeInstanceOf(CryptoStorageDecryptError)
  })

  it('closes IndexedDB connections after reading and saving the keystore key', async () => {
    const closeCalls: number[] = []
    const originalOpen = indexedDB.open.bind(indexedDB)
    vi.spyOn(indexedDB, 'open').mockImplementation((...args) => {
      const req = originalOpen(...(args as [string, number?]))
      const origSet = Object.getOwnPropertyDescriptor(req, 'onsuccess')
      let userHandler: ((ev: Event) => void) | null = null
      Object.defineProperty(req, 'onsuccess', {
        configurable: true,
        get: () => userHandler,
        set: (fn) => {
          userHandler = (ev: Event) => {
            const db = req.result as IDBDatabase & { close: () => void }
            const origClose = db.close.bind(db)
            db.close = () => {
              closeCalls.push(1)
              origClose()
            }
            fn(ev)
          }
          if (origSet?.set) origSet.set.call(req, userHandler)
        },
      })
      return req
    })

    try {
      const payload = new Uint8Array([9, 8, 7])
      const encrypted = await encryptPayload(payload)
      resetInMemoryCryptoKey()
      const decrypted = await decryptPayload(encrypted)
      expect(decrypted).toEqual(payload)
      expect(closeCalls.length).toBeGreaterThanOrEqual(2)
    } finally {
      vi.restoreAllMocks()
    }
  })
})
