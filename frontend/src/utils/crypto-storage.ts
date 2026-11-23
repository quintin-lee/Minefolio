/**
 * @file crypto-storage.ts
 * @description 基于 Web Crypto API (AES-256-GCM) 的离线 SQLite 数据库与敏感存储透明加解密模块
 *
 * 安全设计：
 * - 主密钥生成时强制 `extractable: false`，不可被 JS 脚本通过 exportKey 导出原始密钥字节。
 * - `CryptoKey` 对象直接持久化在 IndexedDB 结构化存储中，绝不在 globalThis 或 localStorage 暴露明文密钥。
 * - 拒绝加密失败后静默降级为 `b64:` 明文，统一抛出显式错误由调用方感知。
 */

const DB_KEYSTORE_NAME = 'minefolio_keystore'
const DB_STORE_NAME = 'keys'
const KEYSTORE_KEY_ID = 'db_master_key_v1'

let inMemoryKey: CryptoKey | null = null

export class CryptoStorageEncryptError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'CryptoStorageEncryptError'
  }
}

export class CryptoStorageDecryptError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'CryptoStorageDecryptError'
  }
}

export function resetInMemoryCryptoKey(): void {
  inMemoryKey = null
}

function base64ToBytes(b64: string): Uint8Array {
  const bin = atob(b64)
  const bytes = new Uint8Array(bin.length)
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i)
  return bytes
}

function bytesToBase64(bytes: Uint8Array): string {
  let bin = ''
  const len = bytes.byteLength
  for (let i = 0; i < len; i += 8192) {
    bin += String.fromCharCode(...bytes.subarray(i, Math.min(i + 8192, len)))
  }
  return btoa(bin)
}

function isCryptoKeyLike(val: unknown): val is CryptoKey {
  if (!val || typeof val !== 'object') return false
  if (typeof CryptoKey !== 'undefined' && val instanceof CryptoKey) return true
  return (
    !ArrayBuffer.isView(val) &&
    'type' in val &&
    'algorithm' in val &&
    'usages' in val
  )
}

function openKeystoreDb(): Promise<IDBDatabase | null> {
  if (typeof window === 'undefined' || !window.indexedDB) {
    return Promise.resolve(null)
  }
  return new Promise((resolve) => {
    try {
      const req = indexedDB.open(DB_KEYSTORE_NAME, 1)
      req.onupgradeneeded = () => {
        const db = req.result
        if (!db.objectStoreNames.contains(DB_STORE_NAME)) {
          db.createObjectStore(DB_STORE_NAME)
        }
      }
      req.onsuccess = () => resolve(req.result)
      req.onerror = () => resolve(null)
    } catch {
      resolve(null)
    }
  })
}

async function getStoredKeyEntry(): Promise<CryptoKey | Uint8Array | null> {
  const db = await openKeystoreDb()
  if (!db) return null
  return new Promise((resolve) => {
    try {
      const tx = db.transaction(DB_STORE_NAME, 'readonly')
      const store = tx.objectStore(DB_STORE_NAME)
      const getReq = store.get(KEYSTORE_KEY_ID)
      getReq.onsuccess = () => {
        const res = getReq.result
        if (isCryptoKeyLike(res)) {
          resolve(res)
          return
        }
        if (res instanceof Uint8Array) {
          resolve(res)
          return
        }
        if (ArrayBuffer.isView(res)) {
          resolve(new Uint8Array(res.buffer, res.byteOffset, res.byteLength))
          return
        }
        resolve(null)
      }
      getReq.onerror = () => resolve(null)
    } catch {
      resolve(null)
    }
  })
}

async function saveCryptoKey(key: CryptoKey): Promise<void> {
  const db = await openKeystoreDb()
  if (!db) return
  return new Promise((resolve) => {
    try {
      const tx = db.transaction(DB_STORE_NAME, 'readwrite')
      const store = tx.objectStore(DB_STORE_NAME)
      store.put(key, KEYSTORE_KEY_ID)
      tx.oncomplete = () => resolve()
      tx.onerror = () => resolve()
    } catch {
      resolve()
    }
  })
}

/**
 * 获取或生成不可导出 (extractable: false) 的 AES-256-GCM CryptoKey，并直接持久化至 IndexedDB
 */
async function getCryptoKey(): Promise<CryptoKey | null> {
  if (inMemoryKey) return inMemoryKey
  if (typeof window === 'undefined' || !window.crypto || !window.crypto.subtle) {
    return null
  }

  try {
    const stored = await getStoredKeyEntry()
    if (isCryptoKeyLike(stored)) {
      inMemoryKey = stored
      return inMemoryKey
    }

    // 兼容历史旧版 Uint8Array 原始密钥：一次性导入为不可导出 CryptoKey 并回写 IndexedDB，同时擦除原始字节
    if (stored instanceof Uint8Array && stored.length === 32) {
      const migrated = await window.crypto.subtle.importKey(
        'raw',
        stored,
        { name: 'AES-GCM', length: 256 },
        false,
        ['encrypt', 'decrypt']
      )
      stored.fill(0)
      await saveCryptoKey(migrated)
      inMemoryKey = migrated
      return inMemoryKey
    }

    const newKey = await window.crypto.subtle.generateKey(
      { name: 'AES-GCM', length: 256 },
      false,
      ['encrypt', 'decrypt']
    )
    await saveCryptoKey(newKey)
    inMemoryKey = newKey
    return inMemoryKey
  } catch (err) {
    console.warn('[crypto-storage] Web Crypto key derivation failed:', err)
    return null
  }
}

/**
 * 加密二进制载荷（严格要求 AES-256-GCM，禁止静默降级为 b64: 明文）
 * @param data 待加密的 Uint8Array
 * @returns enc:v1:<iv_b64>:<ciphertext_b64>
 */
export async function encryptPayload(data: Uint8Array): Promise<string> {
  if (typeof window === 'undefined' || !window.crypto || !window.crypto.subtle) {
    throw new CryptoStorageEncryptError('WebCrypto API unavailable for local storage encryption')
  }
  const key = await getCryptoKey()
  if (!key) {
    throw new CryptoStorageEncryptError('AES-GCM CryptoKey unavailable for local storage encryption')
  }

  try {
    const iv = window.crypto.getRandomValues(new Uint8Array(12))
    const cipherBuffer = await window.crypto.subtle.encrypt(
      { name: 'AES-GCM', iv },
      key,
      data
    )
    const ivB64 = bytesToBase64(iv)
    const cipherB64 = bytesToBase64(new Uint8Array(cipherBuffer))
    return `enc:v1:${ivB64}:${cipherB64}`
  } catch (err) {
    console.error('[crypto-storage] Encryption failed:', err)
    throw new CryptoStorageEncryptError(
      err instanceof Error ? `Failed to encrypt local storage payload: ${err.message}` : 'Failed to encrypt local storage payload'
    )
  }
}

/**
 * 解密本地持久化的载荷（透明兼容历史旧格式；密文解密失败绝不降级回落）
 * @param saved 本地存储的字符串
 * @returns 还原的原始 Uint8Array 二进制
 */
export async function decryptPayload(saved: string): Promise<Uint8Array> {
  if (!saved) {
    return new Uint8Array(0)
  }

  // 1. 加密格式：enc:v1:<iv_b64>:<ciphertext_b64> — 绝不降级到明文解析，防止密文被静默当成损坏库覆盖
  if (saved.startsWith('enc:v1:')) {
    const parts = saved.split(':')
    if (parts.length !== 4 || !parts[2] || !parts[3]) {
      throw new CryptoStorageDecryptError('Invalid encrypted local storage payload format')
    }
    if (typeof window === 'undefined' || !window.crypto || !window.crypto.subtle) {
      throw new CryptoStorageDecryptError('WebCrypto API unavailable to decrypt local storage payload')
    }
    const key = await getCryptoKey()
    if (!key) {
      throw new CryptoStorageDecryptError('Missing AES-GCM decryption key for local storage payload')
    }
    try {
      const iv = base64ToBytes(parts[2])
      const cipherBytes = base64ToBytes(parts[3])
      const decryptedBuffer = await window.crypto.subtle.decrypt(
        { name: 'AES-GCM', iv },
        key,
        cipherBytes
      )
      return new Uint8Array(decryptedBuffer)
    } catch (err) {
      console.error('[crypto-storage] Decryption failed:', err)
      throw new CryptoStorageDecryptError(
        err instanceof Error ? `Failed to decrypt local storage payload: ${err.message}` : 'Failed to decrypt local storage payload'
      )
    }
  }

  // 2. 兼容历史明文 base64 格式
  if (saved.startsWith('b64:')) {
    return base64ToBytes(saved.slice(4))
  }

  // 3. 兼容历史旧 JSON 数组格式
  if (saved.startsWith('[')) {
    return new Uint8Array(JSON.parse(saved))
  }

  // 4. 回退自适应
  try {
    return base64ToBytes(saved)
  } catch {
    return new Uint8Array(JSON.parse(saved))
  }
}
