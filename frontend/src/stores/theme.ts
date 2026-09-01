import { defineStore } from 'pinia'
import { ref } from 'vue'

export type ThemeMode = 'dark' | 'light' | 'auto'

export const useThemeStore = defineStore('theme', () => {
  const savedMode = (localStorage.getItem('minefolio_theme') as ThemeMode) || 'dark'
  const mode = ref<ThemeMode>(savedMode)
  const resolvedTheme = ref<'dark' | 'light'>('dark')

  function applyTheme() {
    if (typeof window === 'undefined') return
    if (mode.value === 'auto') {
      const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches
      resolvedTheme.value = prefersDark ? 'dark' : 'light'
    } else {
      resolvedTheme.value = mode.value
    }

    document.documentElement.setAttribute('data-theme', resolvedTheme.value)
    if (resolvedTheme.value === 'dark') {
      document.documentElement.classList.add('dark')
      document.documentElement.classList.remove('light')
    } else {
      document.documentElement.classList.add('light')
      document.documentElement.classList.remove('dark')
    }
    localStorage.setItem('minefolio_theme', mode.value)
  }

  function setMode(newMode: ThemeMode) {
    mode.value = newMode
    applyTheme()
  }

  function toggle() {
    if (resolvedTheme.value === 'dark') {
      setMode('light')
    } else {
      setMode('dark')
    }
  }

  if (typeof window !== 'undefined') {
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
      if (mode.value === 'auto') {
        applyTheme()
      }
    })
    // 首次初始化
    applyTheme()
  }

  return {
    mode,
    resolvedTheme,
    setMode,
    toggle,
    applyTheme
  }
})
