/**
 * @file crypto-storage.ts
 * @description 基于 Web Crypto API (AES-256-GCM) 的离线 SQLite 数据库与敏感存储透明加解密模块
 */

const DB_KEYSTORE_NAME = 'minefolio_keystore'
const DB_STORE_NAME = 'keys'
const KEYSTORE_KEY_ID = 'db_master_key_v1'

declare global {
  // eslint-disable-next-line no-var
  var __minefolio_master_raw_key__: Uint8Array | undefined
}

let inMemoryKey: CryptoKey | null = null

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

/**
 * 访问 IndexedDB 或 globalThis 获取持久化主密钥
 */
async function getStoredRawKey(): Promise<Uint8Array | null> {
  if (typeof globalThis !== 'undefined' && globalThis.__minefolio_master_raw_key__) {
    return globalThis.__minefolio_master_raw_key__
  }
  if (typeof window === 'undefined' || !window.indexedDB) {
    return null
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
      req.onsuccess = () => {
        const db = req.result
        try {
          const tx = db.transaction(DB_STORE_NAME, 'readonly')
          const store = tx.objectStore(DB_STORE_NAME)
          const getReq = store.get(KEYSTORE_KEY_ID)
          getReq.onsuccess = () => {
            const res = getReq.result || null
            if (res && typeof globalThis !== 'undefined') {
              globalThis.__minefolio_master_raw_key__ = res
            }
            resolve(res)
          }
          getReq.onerror = () => resolve(null)
        } catch {
          resolve(null)
        }
      }
      req.onerror = () => resolve(null)
    } catch {
      resolve(null)
    }
  })
}

async function saveRawKey(rawKey: Uint8Array): Promise<void> {
  if (typeof globalThis !== 'undefined') {
    globalThis.__minefolio_master_raw_key__ = rawKey
  }
  if (typeof window === 'undefined' || !window.indexedDB) {
    return
  }
  return new Promise((resolve) => {
    try {
      const req = indexedDB.open(DB_KEYSTORE_NAME, 1)
      req.onsuccess = () => {
        const db = req.result
        try {
          const tx = db.transaction(DB_STORE_NAME, 'readwrite')
          const store = tx.objectStore(DB_STORE_NAME)
          store.put(rawKey, KEYSTORE_KEY_ID)
          tx.oncomplete = () => resolve()
          tx.onerror = () => resolve()
        } catch {
          resolve()
        }
      }
      req.onerror = () => resolve()
    } catch {
      resolve()
    }
  })
}

/**
 * 获取或生成 AES-256-GCM CryptoKey
 */
async function getCryptoKey(): Promise<CryptoKey | null> {
  if (inMemoryKey) return inMemoryKey
  if (typeof window === 'undefined' || !window.crypto || !window.crypto.subtle) {
    return null
  }

  try {
    const rawStored = await getStoredRawKey()
    if (rawStored && rawStored.length === 32) {
      inMemoryKey = await window.crypto.subtle.importKey(
        'raw',
        rawStored,
        { name: 'AES-GCM', length: 256 },
        true,
        ['encrypt', 'decrypt']
      )
      return inMemoryKey
    }

    // 生成新密钥并持久化
    const newKey = await window.crypto.subtle.generateKey(
      { name: 'AES-GCM', length: 256 },
      true,
      ['encrypt', 'decrypt']
    )
    const exportedRaw = new Uint8Array(await window.crypto.subtle.exportKey('raw', newKey))
    await saveRawKey(exportedRaw)
    inMemoryKey = newKey
    return inMemoryKey
  } catch (err) {
    console.warn('[crypto-storage] Web Crypto key derivation failed:', err)
    return null
  }
}

/**
 * 加密二进制载荷
 * @param data 待加密的 Uint8Array
 * @returns enc:v1:<iv_b64>:<ciphertext_b64> 或降级 b64:<data_b64>
 */
export async function encryptPayload(data: Uint8Array): Promise<string> {
  const key = await getCryptoKey()
  if (!key || typeof window === 'undefined' || !window.crypto || !window.crypto.subtle) {
    return 'b64:' + bytesToBase64(data)
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
    console.error('[crypto-storage] Encryption failed, falling back to plaintext base64:', err)
    return 'b64:' + bytesToBase64(data)
  }
}

/**
 * 解密本地持久化的载荷（透明兼容历史旧格式）
 * @param saved 本地存储的字符串
 * @returns 还原的原始 Uint8Array 二进制
 */
export async function decryptPayload(saved: string): Promise<Uint8Array> {
  if (!saved) {
    return new Uint8Array(0)
  }

  // 1. 加密格式：enc:v1:<iv_b64>:<ciphertext_b64>
  if (saved.startsWith('enc:v1:')) {
    const parts = saved.split(':')
    if (parts.length === 4 && parts[2] && parts[3]) {
      const ivB64 = parts[2]
      const cipherB64 = parts[3]
      const key = await getCryptoKey()
      if (key && typeof window !== 'undefined' && window.crypto && window.crypto.subtle) {
        try {
          const iv = base64ToBytes(ivB64)
          const cipherBytes = base64ToBytes(cipherB64)
          const decryptedBuffer = await window.crypto.subtle.decrypt(
            { name: 'AES-GCM', iv },
            key,
            cipherBytes
          )
          return new Uint8Array(decryptedBuffer)
        } catch (err) {
          console.error('[crypto-storage] Decryption failed:', err)
        }
      }
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
