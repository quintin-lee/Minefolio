/**
 * RSA-OAEP (SHA-256) password encryption for transit security.
 *
 * Flow:
 *   1. GET /api/auth/public-key → JWK { kty:"RSA", n, e }
 *   2. importKey → CryptoKey (public, encrypt)
 *   3. encrypt(password_bytes) → ArrayBuffer
 *   4. Base64url(ArrayBuffer) → string sent as password_enc
 */

interface RsaJwk {
  kty: 'RSA'
  n: string
  e: string
}

async function fetchPublicKey(): Promise<RsaJwk> {
  // 公钥接口无需鉴权，直接 fetch 避免循环依赖 http.ts
  const res = await fetch('/api/auth/public-key')
  if (!res.ok) throw new Error('Failed to fetch public key')
  const body = await res.json()
  const pk = body.data.public_key
  return typeof pk === 'string' ? JSON.parse(pk) : pk
}

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
