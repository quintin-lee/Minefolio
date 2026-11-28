/**
 * @file 全局显示设置 Pinia Store
 * @description 管理金额显示/隐藏等全局 UI 偏好，状态持久化到 localStorage
 */

import { defineStore, getActivePinia } from 'pinia'
import { ref } from 'vue'

const STORAGE_KEY = 'minefolio_display_settings'

interface DisplaySettings {
  showAmount: boolean
}

function loadFromStorage(): DisplaySettings {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    if (raw) {
      const parsed = JSON.parse(raw) as Partial<DisplaySettings>
      return {
        showAmount: parsed.showAmount !== false, // default: true
      }
    }
  } catch {
    // ignore
  }
  return { showAmount: true }
}

function persist(settings: DisplaySettings) {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(settings))
  } catch {
    // ignore
  }
}

/** 同步 <html> data-amount-masked 属性（CSS 选择器依赖此属性） */
function syncDomAttribute(showAmount: boolean) {
  if (typeof document === 'undefined') return
  if (showAmount) {
    document.documentElement.removeAttribute('data-amount-masked')
  } else {
    document.documentElement.setAttribute('data-amount-masked', 'true')
  }
}

export const useDisplaySettingsStore = defineStore('display-settings', () => {
  const initial = loadFromStorage()

  /** 是否显示金额（false 时全局隐藏/打码金额） */
  const showAmount = ref(initial.showAmount)

  function toggle() {
    showAmount.value = !showAmount.value
    persist({ showAmount: showAmount.value })
    syncDomAttribute(showAmount.value)
  }

  function set(val: boolean) {
    showAmount.value = val
    persist({ showAmount: val })
    syncDomAttribute(val)
  }

  function maskText(text: string, placeholder = '****'): string {
    return showAmount.value ? text : placeholder
  }

  // 初始化时同步 DOM 属性（处理 SSR / 首次 mount 场景）
  if (typeof document !== 'undefined') {
    syncDomAttribute(showAmount.value)
  }

  return {
    showAmount,
    toggle,
    set,
    maskText,
  }
})

/**
 * Pinia-safe reactive helper for masking formatted amount text in templates/computeds.
 * Falls back to unmasked text when rendered in isolated unit tests without an active Pinia instance.
 */
export function maskAmountText(text: string, placeholder = '****'): string {
  if (!getActivePinia()) return text
  return useDisplaySettingsStore().showAmount ? text : placeholder
}
