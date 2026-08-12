import { defineStore } from 'pinia'
import { ref } from 'vue'
import { authApi } from '@/api/auth'
import { systemApi } from '@/api/system'

interface User {
  id: number
  username: string
  created_at: string
}

interface RsaJwk {
  kty: string
  n: string
  e: string
}

async function fetchRsaJwk(): Promise<RsaJwk> {
  const r = await fetch(`${import.meta.env.VITE_API_URL}/api/auth/public-key`)
  if (!r.ok) throw new Error('Failed to fetch public key')
  const body = await r.json()
  return body.data.public_key
}

function jwkB64urlToUint8Array(b64: string): Uint8Array {
  const padded = b64.replace(/-/g, '+').replace(/_/g, '/')
  return new Uint8Array(
    atob(padded).split('').map((c) => c.charCodeAt(0))
  )
}

function uint8ArrayToB64url(buf: Uint8Array): string {
  return btoa(String.fromCharCode(...buf))
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/, '')
}

async function encryptPassword(password: string): Promise<string> {
  const jwk = await fetchRsaJwk()
  const modulus = jwkB64urlToUint8Array(jwk.n)
  const exponent = jwkB64urlToUint8Array(jwk.e)
  const key = await crypto.subtle.importKey(
    'jwk',
    {
      kty: jwk.kty,
      n: uint8ArrayToB64url(modulus),
      e: uint8ArrayToB64url(exponent),
    },
    { name: 'RSA-OAEP', hash: 'SHA-256' },
    false,
    ['encrypt']
  )
  const encBuf = await crypto.subtle.encrypt(
    { name: 'RSA-OAEP' },
    key,
    new TextEncoder().encode(password)
  )
  return uint8ArrayToB64url(new Uint8Array(encBuf))
}

export const useAuthStore = defineStore('auth', () => {
  const token = ref<string>(localStorage.getItem('token') || '')
  const user = ref<User | null>(null)
  const isInitialized = ref<boolean | null>(null)

  async function checkSystemStatus() {
    try {
      const res = await systemApi.status()
      isInitialized.value = res.initialized
      return res.initialized
    } catch {
      isInitialized.value = true
      return true
    }
  }

  async function setup(username: string, password: string) {
    const password_enc = await encryptPassword(password)
    const res = await systemApi.setup({ username, password_enc }) as { token: string; expires_in: number }
    token.value = res.token
    localStorage.setItem('token', res.token)
    isInitialized.value = true
    await fetchUser()
  }

  async function login(username: string, password: string) {
    const password_enc = await encryptPassword(password)
    const res = await authApi.login(username, password_enc)
    token.value = res.token
    localStorage.setItem('token', res.token)
    await fetchUser()
  }

  async function fetchUser() {
    const res = await authApi.me()
    user.value = res
  }

  async function changePassword(oldPassword: string, newPassword: string) {
    const old_enc = await encryptPassword(oldPassword)
    const new_enc = await encryptPassword(newPassword)
    await authApi.changePassword(old_enc, new_enc)
  }

  function logout() {
    token.value = ''
    user.value = null
    localStorage.removeItem('token')
  }

  return { token, user, isInitialized, checkSystemStatus, setup, login, logout, fetchUser, changePassword }
})

