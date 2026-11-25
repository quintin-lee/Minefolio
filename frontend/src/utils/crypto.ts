/**
 * @file RSA-OAEP (SHA-256) 密码传输加密工具模块
 * @description 优先使用 Web Cryptography API (crypto.subtle) 对密码等敏感字段进行
 *              RSA-OAEP-256 非对称加密；在非安全上下文（如 http:// 域名、内网 IP）
 *              浏览器禁用 crypto.subtle 时，自动降级到纯 TypeScript + crypto.getRandomValues
 *              的 RFC 8017 RSA-OAEP-SHA256 实现，保证 HTTP/HTTPS 均可正常登录注册。
 */

import { buildApiUrl, onServerUrlChange } from '@/utils/server-url'
import {
  bytesToBase64url,
  encryptRsaOaepSha256,
  parseRsaJwk,
  type RsaPublicParams,
} from '@/utils/rsa-oaep'

// 当服务端地址发生变更时自动重置 RSA 公钥缓存
onServerUrlChange(() => {
  clearCryptoKeyCache()
})

interface RsaJwk {
  kty: 'RSA'
  n: string
  e: string
}

async function fetchPublicKey(): Promise<RsaJwk> {
  const res = await fetch(buildApiUrl('/auth/public-key'))
  if (!res.ok) throw new Error('Failed to fetch public key')
  const body = await res.json()
  const pk = body.data.public_key
  return typeof pk === 'string' ? JSON.parse(pk) : pk
}

let cachedCryptoKey: CryptoKey | null = null
let keyPromise: Promise<CryptoKey> | null = null
let cachedFallbackKey: RsaPublicParams | null = null
let fallbackKeyPromise: Promise<RsaPublicParams> | null = null

/**
 * 清除 RSA 公钥缓存（在密钥轮换或重新初始化时调用）
 */
export function clearCryptoKeyCache(): void {
  cachedCryptoKey = null
  keyPromise = null
  cachedFallbackKey = null
  fallbackKeyPromise = null
}

function getWebCrypto(): Crypto {
  if (typeof globalThis.crypto === 'undefined') {
    throw new Error(
      '当前浏览环境不支持 WebCrypto（缺少加密 API，请切换 HTTPS 或使用现代浏览器访问）'
    )
  }
  return globalThis.crypto
}

async function getCryptoKey(subtle: SubtleCrypto): Promise<CryptoKey> {
  if (cachedCryptoKey) {
    return cachedCryptoKey
  }
  if (keyPromise) {
    return keyPromise
  }

  keyPromise = (async () => {
    try {
      const jwk = await fetchPublicKey()

      const modulus = new Uint8Array(
        atob(jwk.n.replace(/-/g, '+').replace(/_/g, '/'))
          .split('')
          .map((c) => c.charCodeAt(0))
      )
      const exponent = new Uint8Array(
        atob(jwk.e.replace(/-/g, '+').replace(/_/g, '/'))
          .split('')
          .map((c) => c.charCodeAt(0))
      )

      const key = await subtle.importKey(
        'jwk',
        {
          kty: jwk.kty,
          n: btoa(String.fromCharCode(...modulus)).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, ''),
          e: btoa(String.fromCharCode(...exponent)).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, ''),
        },
        { name: 'RSA-OAEP', hash: 'SHA-256' },
        false,
        ['encrypt']
      )

      cachedCryptoKey = key
      return key
    } finally {
      keyPromise = null
    }
  })()

  return keyPromise
}

async function getFallbackKey(): Promise<RsaPublicParams> {
  if (cachedFallbackKey) {
    return cachedFallbackKey
  }
  if (fallbackKeyPromise) {
    return fallbackKeyPromise
  }

  fallbackKeyPromise = (async () => {
    try {
      const jwk = await fetchPublicKey()
      const params = parseRsaJwk(jwk)
      cachedFallbackKey = params
      return params
    } finally {
      fallbackKeyPromise = null
    }
  })()

  return fallbackKeyPromise
}

/**
 * 使用服务端 RSA-OAEP-256 公钥加密用户密码或敏感文本
 * @param password 明文字符串 (如登录/注册密码)
 * @returns 加密后的 Base64url 密文字符串 (作为 password_enc 字段提交给服务端)
 */
export async function encryptPassword(password: string): Promise<string> {
  const cryptoObj = getWebCrypto()
  const encoded = new TextEncoder().encode(password)

  if (cryptoObj.subtle) {
    const cryptoKey = await getCryptoKey(cryptoObj.subtle)
    const encrypted = await cryptoObj.subtle.encrypt(
      { name: 'RSA-OAEP' },
      cryptoKey,
      encoded
    )
    return bytesToBase64url(new Uint8Array(encrypted))
  }

  if (typeof cryptoObj.getRandomValues !== 'function') {
    throw new Error(
      '当前浏览环境不支持 WebCrypto（RSA 加密仅在 HTTPS、localhost 或支持安全随机数的环境下可用）'
    )
  }

  const params = await getFallbackKey()
  const encryptedBytes = encryptRsaOaepSha256(
    params,
    encoded,
    cryptoObj.getRandomValues.bind(cryptoObj)
  )
  return bytesToBase64url(encryptedBytes)
}

/** 别名导出：加密普通敏感文本 */
export const encryptText = encryptPassword
