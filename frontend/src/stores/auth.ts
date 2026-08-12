import { defineStore } from 'pinia'
import { ref } from 'vue'
import { authApi } from '@/api/auth'
import { systemApi } from '@/api/system'

interface User {
  id: number
  username: string
  created_at: string
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
    const res = await systemApi.setup({ username, password })
    token.value = res.token
    localStorage.setItem('token', res.token)
    isInitialized.value = true
    await fetchUser()
  }

  async function login(username: string, password: string) {
    const res = await authApi.login(username, password)
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

  return { token, user, isInitialized, checkSystemStatus, setup, login, logout, fetchUser }
})
