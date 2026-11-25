/**
 * @file 纯 TypeScript RSA-OAEP (SHA-256 / MGF1-SHA256) 降级加密实现
 * @description 当页面运行在非安全上下文（如 http:// 域名或内网 IP）导致浏览器禁用
 *              crypto.subtle 时，使用 ES2020 BigInt + crypto.getRandomValues 完成与
 *              WebCrypto RSA-OAEP-256 及 OpenSSL 后端完全等价的 RFC 8017 加密。
 */

const SHA256_LEN = 32

const SHA256_K = new Uint32Array([
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
])

const SHA256_INIT = new Uint32Array([
  0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
])

function rotr(x: number, n: number): number {
  return (x >>> n) | (x << (32 - n))
}

/**
 * FIPS 180-4 SHA-256 摘要
 */
export function sha256(data: Uint8Array): Uint8Array {
  const paddedLen = Math.ceil((data.length + 9) / 64) * 64
  const padded = new Uint8Array(paddedLen)
  padded.set(data)
  padded[data.length] = 0x80

  const view = new DataView(padded.buffer)
  const bitLenHi = Math.floor(data.length / 0x20000000)
  const bitLenLo = (data.length << 3) >>> 0
  view.setUint32(paddedLen - 8, bitLenHi, false)
  view.setUint32(paddedLen - 4, bitLenLo, false)

  const h = new Uint32Array(SHA256_INIT)
  const w = new Uint32Array(64)

  for (let offset = 0; offset < paddedLen; offset += 64) {
    for (let i = 0; i < 16; i++) {
      w[i] = view.getUint32(offset + i * 4, false)
    }
    for (let i = 16; i < 64; i++) {
      const w15 = w[i - 15]!
      const w2 = w[i - 2]!
      const s0 = rotr(w15, 7) ^ rotr(w15, 18) ^ (w15 >>> 3)
      const s1 = rotr(w2, 17) ^ rotr(w2, 19) ^ (w2 >>> 10)
      w[i] = (w[i - 16]! + s0 + w[i - 7]! + s1) >>> 0
    }

    let a = h[0]!
    let b = h[1]!
    let c = h[2]!
    let d = h[3]!
    let e = h[4]!
    let f = h[5]!
    let g = h[6]!
    let hh = h[7]!

    for (let i = 0; i < 64; i++) {
      const s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)
      const ch = (e & f) ^ (~e & g)
      const temp1 = (hh + s1 + ch + SHA256_K[i]! + w[i]!) >>> 0
      const s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)
      const maj = (a & b) ^ (a & c) ^ (b & c)
      const temp2 = (s0 + maj) >>> 0

      hh = g
      g = f
      f = e
      e = (d + temp1) >>> 0
      d = c
      c = b
      b = a
      a = (temp1 + temp2) >>> 0
    }

    h[0] = (h[0]! + a) >>> 0
    h[1] = (h[1]! + b) >>> 0
    h[2] = (h[2]! + c) >>> 0
    h[3] = (h[3]! + d) >>> 0
    h[4] = (h[4]! + e) >>> 0
    h[5] = (h[5]! + f) >>> 0
    h[6] = (h[6]! + g) >>> 0
    h[7] = (h[7]! + hh) >>> 0
  }

  const out = new Uint8Array(SHA256_LEN)
  const outView = new DataView(out.buffer)
  for (let i = 0; i < 8; i++) {
    outView.setUint32(i * 4, h[i]!, false)
  }
  return out
}

const EMPTY_LABEL_HASH = sha256(new Uint8Array(0))

function mgf1Sha256(seed: Uint8Array, maskLen: number): Uint8Array {
  const mask = new Uint8Array(maskLen)
  const input = new Uint8Array(seed.length + 4)
  input.set(seed)

  let offset = 0
  for (let counter = 0; offset < maskLen; counter++) {
    input[seed.length] = (counter >>> 24) & 0xff
    input[seed.length + 1] = (counter >>> 16) & 0xff
    input[seed.length + 2] = (counter >>> 8) & 0xff
    input[seed.length + 3] = counter & 0xff
    const digest = sha256(input)
    const take = Math.min(SHA256_LEN, maskLen - offset)
    mask.set(digest.subarray(0, take), offset)
    offset += take
  }
  return mask
}

export function base64urlToBytes(b64url: string): Uint8Array {
  const base64 = b64url.replace(/-/g, '+').replace(/_/g, '/')
  const padded = base64 + '='.repeat((4 - (base64.length % 4)) % 4)
  const bin = atob(padded)
  const out = new Uint8Array(bin.length)
  for (let i = 0; i < bin.length; i++) {
    out[i] = bin.charCodeAt(i)
  }
  return out
}

export function bytesToBase64url(bytes: Uint8Array): string {
  let binary = ''
  for (let i = 0; i < bytes.length; i++) {
    binary += String.fromCharCode(bytes[i]!)
  }
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '')
}

function stripLeadingZeros(bytes: Uint8Array): Uint8Array {
  let start = 0
  while (start < bytes.length && bytes[start] === 0) {
    start++
  }
  return bytes.subarray(start)
}

function bytesToBigInt(bytes: Uint8Array): bigint {
  let val = 0n
  for (let i = 0; i < bytes.length; i++) {
    val = (val << 8n) | BigInt(bytes[i]!)
  }
  return val
}

function bigIntToFixedBytes(val: bigint, len: number): Uint8Array {
  const out = new Uint8Array(len)
  let cur = val
  for (let i = len - 1; i >= 0; i--) {
    out[i] = Number(cur & 0xffn)
    cur >>= 8n
  }
  return out
}

function modPow(base: bigint, exp: bigint, mod: bigint): bigint {
  let result = 1n
  let b = base % mod
  let e = exp
  while (e > 0n) {
    if ((e & 1n) === 1n) {
      result = (result * b) % mod
    }
    e >>= 1n
    if (e > 0n) {
      b = (b * b) % mod
    }
  }
  return result
}

export interface RsaPublicParams {
  n: bigint
  e: bigint
  modulusByteLen: number
}

export function parseRsaJwk(jwk: { kty: string; n: string; e: string }): RsaPublicParams {
  if (jwk.kty !== 'RSA' || !jwk.n || !jwk.e) {
    throw new Error('服务端返回的 RSA 公钥格式无效')
  }
  const nBytes = stripLeadingZeros(base64urlToBytes(jwk.n))
  const eBytes = stripLeadingZeros(base64urlToBytes(jwk.e))
  if (nBytes.length < 2 * SHA256_LEN + 2 || eBytes.length === 0) {
    throw new Error('RSA 公钥长度不足以执行 OAEP-SHA256 加密')
  }
  const n = bytesToBigInt(nBytes)
  const e = bytesToBigInt(eBytes)
  if (n <= 0n || e < 3n || (e & 1n) === 0n) {
    throw new Error('RSA 公钥参数非法')
  }
  return { n, e, modulusByteLen: nBytes.length }
}

/**
 * 使用 RFC 8017 RSAES-OAEP (SHA-256 / MGF1-SHA256 / 空标签) 对明文字节加密
 */
export function encryptRsaOaepSha256(
  params: RsaPublicParams,
  message: Uint8Array,
  getRandomValues: <T extends ArrayBufferView>(array: T) => T
): Uint8Array {
  const k = params.modulusByteLen
  const maxMsgLen = k - 2 * SHA256_LEN - 2
  if (message.length > maxMsgLen) {
    throw new Error('待加密内容超出 RSA-OAEP 允许的最大长度')
  }

  const dbLen = k - SHA256_LEN - 1
  const db = new Uint8Array(dbLen)
  db.set(EMPTY_LABEL_HASH, 0)
  const psEnd = dbLen - message.length - 1
  db[psEnd] = 0x01
  db.set(message, psEnd + 1)

  const seed = new Uint8Array(SHA256_LEN)
  getRandomValues(seed)

  const dbMask = mgf1Sha256(seed, dbLen)
  const maskedDB = new Uint8Array(dbLen)
  for (let i = 0; i < dbLen; i++) {
    maskedDB[i] = db[i]! ^ dbMask[i]!
  }

  const seedMask = mgf1Sha256(maskedDB, SHA256_LEN)
  const maskedSeed = new Uint8Array(SHA256_LEN)
  for (let i = 0; i < SHA256_LEN; i++) {
    maskedSeed[i] = seed[i]! ^ seedMask[i]!
  }

  const em = new Uint8Array(k)
  em[0] = 0x00
  em.set(maskedSeed, 1)
  em.set(maskedDB, 1 + SHA256_LEN)

  const m = bytesToBigInt(em)
  const c = modPow(m, params.e, params.n)
  return bigIntToFixedBytes(c, k)
}
