import { defineStore } from 'pinia'
import { ref } from 'vue'
import { authApi } from '@/api/auth'

interface LoginResponse {
  token: string
  expires_in: number
}

interface User {
  id: number
  username: string
  created_at: string
}

export const useAuthStore = defineStore('auth', () => {
  const token = ref<string>(localStorage.getItem('token') || '')
  const user = ref<User | null>(null)

  async function login(username: string, password: string) {
    const res = await authApi.login(username, password)
    token.value = res.token
    localStorage.setItem('token', res.token)
    await fetchUser()
  }

  async function register(username: string, password: string) {
    const res = await authApi.register(username, password)
    token.value = res.token
    localStorage.setItem('token', res.token)
    await fetchUser()
  }

  async function fetchUser() {
    const res = await authApi.me()
    user.value = res
  }

  function logout() {
    token.value = ''
    user.value = null
    localStorage.removeItem('token')
  }

  return { token, user, login, register, logout, fetchUser }
})
