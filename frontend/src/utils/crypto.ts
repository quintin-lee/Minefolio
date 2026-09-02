/**
 * @file RSA-OAEP (SHA-256) 密码传输加密工具模块
 * @description 采用 Web Cryptography API，在前端使用服务端公开的 RSA-OAEP-256 公钥对密码进行非对称加密，
 * 防止传输途中明文泄露，满足端到端传输安全。
 *
 * 加密流程：
 * 1. GET /api/auth/public-key → 获取服务端 RSA 公钥 JWK { kty: "RSA", n, e }
 * 2. crypto.subtle.importKey → 导入生成 CryptoKey 公钥对象
 * 3. crypto.subtle.encrypt(RSA-OAEP, key, UTF8(password)) → ArrayBuffer 密文字节流
 * 4. arrayBufferToBase64url(ArrayBuffer) → 生成 Base64url 格式的 password_enc 字符串
 */

import { buildApiUrl } from '@/utils/http'

/**
 * RSA 公钥 JSON Web Key (JWK) 格式接口
 */
interface RsaJwk {
  /** 算法族 (固定 'RSA') */
  kty: 'RSA'
  /** 模数 Modulus (Base64url 编码) */
  n: string
  /** 公开指数 Exponent (Base64url 编码) */
  e: string
}

/**
 * 从服务端无鉴权获取当前激活的 RSA 公钥 JWK
 * @returns 服务端返回的 RSA 公钥 JWK 对象
 */
async function fetchPublicKey(): Promise<RsaJwk> {
  // 公钥接口无需鉴权，使用 buildApiUrl 统一各平台路径
  const res = await fetch(buildApiUrl('/auth/public-key'))
  if (!res.ok) throw new Error('Failed to fetch public key')
  const body = await res.json()
  const pk = body.data.public_key
  return typeof pk === 'string' ? JSON.parse(pk) : pk
}

/**
 * 将加密生成的 ArrayBuffer 二进制流转换为符合 RFC 7515 标准的 Base64url 字符串
 * @param buf 二进制缓冲区
 * @returns 经过 url-safe 替换 (+ -> -, / -> _, 去除 =) 后的字符串
 */
function arrayBufferToBase64url(buf: ArrayBuffer): string {
  const bytes = new Uint8Array(buf)
  let binary = ''
  for (let i = 0; i < bytes.length; i++) {
    binary += String.fromCharCode(bytes[i]!)
  }
  return btoa(binary)
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/, '')
}

/**
 * 使用服务端 RSA-OAEP-256 公钥加密用户密码或敏感文本
 * @param password 明文字符串 (如登录/注册密码)
 * @returns 加密后的 Base64url 密文字符串 (作为 password_enc 字段提交给服务端)
 */
export async function encryptPassword(password: string): Promise<string> {
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

  const cryptoKey = await crypto.subtle.importKey(
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

  const encrypted = await crypto.subtle.encrypt(
    { name: 'RSA-OAEP' },
    cryptoKey,
    new TextEncoder().encode(password)
  )

  return arrayBufferToBase64url(encrypted)
}

/** 别名导出：加密普通敏感文本 */
export const encryptText = encryptPassword

