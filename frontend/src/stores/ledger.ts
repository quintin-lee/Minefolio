/**
 * @file 账本空间与多用户协作状态管理 Store
 * @description 管理多账本切换、当前激活账本上下文 (注入 X-Ledger-Id 请求头)、用户角色权限判断 (Owner/Editor/Viewer) 等
 */

import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import type { Ledger } from '@/types'
import { ledgerApi } from '@/api/ledgers'

/** LocalStorage 存储当前选中账本 ID 的键名 */
const STORAGE_KEY = 'minefolio_active_ledger_id'

/**
 * 账本协作与空间状态 Pinia Store
 */
export const useLedgerStore = defineStore('ledger', () => {
  /** 当前用户可访问的所有账本列表 */
  const ledgers = ref<Ledger[]>([])
  /** 从本地持久化存储中读取上次激活的账本 ID */
  const saved = localStorage.getItem(STORAGE_KEY)
  /** 当前选中的激活账本 ID */
  const currentLedgerId = ref<number | null>(saved ? Number(saved) : null)
  /** 账本列表加载中状态 */
  const loading = ref(false)

  /**
   * 当前激活的账本对象实体计算属性 (若未匹配则回退到默认账本或首个账本)
   */
  const currentLedger = computed(() => {
    if (!ledgers.value.length) return null
    return (
      ledgers.value.find((l) => l.id === currentLedgerId.value) ||
      ledgers.value.find((l) => Boolean(l.is_default)) ||
      ledgers.value[0]
    )
  })

  /** 当前用户在当前激活账本中是否仅为访客/只读权限 (Viewer) */
  const isViewer = computed(() => currentLedger.value?.my_role === 'viewer')
  /** 当前用户在当前激活账本中是否为所有者 (Owner) */
  const isOwner = computed(() => currentLedger.value?.my_role === 'owner')
  /** 当前用户在当前激活账本中是否具备编辑与记账权限 (Owner 或 Editor) */
  const isEditor = computed(() => {
    const r = currentLedger.value?.my_role
    return r === 'owner' || r === 'editor'
  })

  /**
   * 从服务端拉取当前用户参与的所有账本列表，并初始化选中当前激活账本
   */
  async function fetchLedgers() {
    loading.value = true
    try {
      const data = await ledgerApi.list()
      ledgers.value = Array.isArray(data) ? data : []
      if (ledgers.value.length > 0) {
        const found = ledgers.value.find((l) => l.id === currentLedgerId.value)
        if (!found) {
          const def = ledgers.value.find((l) => Boolean(l.is_default)) || ledgers.value[0]
          if (def) {
            currentLedgerId.value = def.id
            localStorage.setItem(STORAGE_KEY, String(def.id))
          }
        }
      }
    } finally {
      loading.value = false
    }
  }

  /**
   * 切换当前激活的账本空间
   * @param id 目标账本 ID
   */
  function setCurrentLedger(id: number) {
    currentLedgerId.value = id
    localStorage.setItem(STORAGE_KEY, String(id))
    window.dispatchEvent(new CustomEvent('minefolio:ledger-changed', { detail: { id } }))
  }

  return {
    ledgers,
    currentLedgerId,
    currentLedger,
    loading,
    isViewer,
    isOwner,
    isEditor,
    fetchLedgers,
    setCurrentLedger,
  }
})

