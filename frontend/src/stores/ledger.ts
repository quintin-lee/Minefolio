import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import type { Ledger } from '@/types'
import { ledgerApi } from '@/api/ledgers'

const STORAGE_KEY = 'minefolio_active_ledger_id'

export const useLedgerStore = defineStore('ledger', () => {
  const ledgers = ref<Ledger[]>([])
  const saved = localStorage.getItem(STORAGE_KEY)
  const currentLedgerId = ref<number | null>(saved ? Number(saved) : null)
  const loading = ref(false)

  const currentLedger = computed(() => {
    if (!ledgers.value.length) return null
    return (
      ledgers.value.find((l) => l.id === currentLedgerId.value) ||
      ledgers.value.find((l) => Boolean(l.is_default)) ||
      ledgers.value[0]
    )
  })

  const isViewer = computed(() => currentLedger.value?.my_role === 'viewer')
  const isOwner = computed(() => currentLedger.value?.my_role === 'owner')
  const isEditor = computed(() => {
    const r = currentLedger.value?.my_role
    return r === 'owner' || r === 'editor'
  })

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
    setCurrentLedger
  }
})
