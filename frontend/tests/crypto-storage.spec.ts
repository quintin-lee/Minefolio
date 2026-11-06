import { describe, it, expect } from 'vitest'
import { encryptPayload, decryptPayload } from '@/utils/crypto-storage'

describe('crypto-storage', () => {
  it('encrypts Uint8Array and returns enc:v1 prefix', async () => {
    const data = new Uint8Array([1, 2, 3, 4, 5, 42, 99])
    const encrypted = await encryptPayload(data)
    expect(encrypted.startsWith('enc:v1:')).toBe(true)
  })

  it('decrypts encrypted payload back to original bytes', async () => {
    const data = new Uint8Array([10, 20, 30, 40, 50, 60, 70, 80])
    const encrypted = await encryptPayload(data)
    const decrypted = await decryptPayload(encrypted)
    expect(decrypted).toEqual(data)
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
})
