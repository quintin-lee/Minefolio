/**
 * @file 系统视觉主题与暗黑模式状态管理 Store
 * @description 管理深色 (Dark)、浅色 (Light) 与跟随系统 (Auto) 主题切换及 DOM 类名属性同步
 */

import { defineStore } from 'pinia'
import { ref } from 'vue'

/**
 * 主题偏好模式类型
 * - 'dark': 强制深色赛博科技主题
 * - 'light': 浅色明亮主题
 * - 'auto': 跟随操作系统媒体查询 (prefers-color-scheme)
 */
export type ThemeMode = 'dark' | 'light' | 'auto'

/**
 * 主题设置 Pinia Store
 */
export const useThemeStore = defineStore('theme', () => {
  /** 从本地持久化读取的主题偏好 (默认 'dark') */
  const savedMode = (localStorage.getItem('minefolio_theme') as ThemeMode) || 'dark'
  /** 当前用户设定的主题模式 */
  const mode = ref<ThemeMode>(savedMode)
  /** 最终解析生效的实际主题 ('dark' | 'light') */
  const resolvedTheme = ref<'dark' | 'light'>('dark')

  /**
   * 将当前主题模式应用到 HTML 根元素 (设置 data-theme 属性与 class)
   */
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

  /**
   * 设定新的主题模式并立即应用
   * @param newMode 目标主题模式 ('dark' | 'light' | 'auto')
   */
  function setMode(newMode: ThemeMode) {
    mode.value = newMode
    applyTheme()
  }

  /**
   * 快速在深色与浅色主题之间来回切换
   */
  function toggle() {
    if (resolvedTheme.value === 'dark') {
      setMode('light')
    } else {
      setMode('dark')
    }
  }

  if (typeof window !== 'undefined') {
    // 监听系统深色模式变更 (当模式设为 auto 时响应)
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
      if (mode.value === 'auto') {
        applyTheme()
      }
    })
    // 首次加载初始化应用主题
    applyTheme()
  }

  return {
    mode,
    resolvedTheme,
    setMode,
    toggle,
    applyTheme,
  }
})

