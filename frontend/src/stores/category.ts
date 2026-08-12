// frontend/src/stores/category.ts
// Pinia store caching the category tree (spec §6.0)
import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { categoriesApi } from '@/api/categories'
import type { Category } from '@/types'

export const useCategoryStore = defineStore('category', () => {
  const tree = ref<Category[]>([])
  const loaded = ref(false)
  const loading = ref(false)

  async function loadCategories(type?: string, force = false) {
    if (loaded.value && !force && !type) return tree.value
    if (loading.value && !type) return tree.value
    loading.value = true
    try {
      const res = await categoriesApi.list(type ? { type } : undefined)
      if (!type) {
        tree.value = res
        loaded.value = true
      }
      return res
    } finally {
      loading.value = false
    }
  }

  const assetCategories = computed(() => tree.value.filter(c => c.type === 'asset' || (!c.type && c.asset_type)))
  const incomeCategories = computed(() => tree.value.filter(c => c.type === 'income'))
  const expenseCategories = computed(() => tree.value.filter(c => c.type === 'expense'))
  const incomeExpenseCategories = computed(() => tree.value.filter(c => c.type === 'income' || c.type === 'expense'))
  const transactionCategories = computed(() => tree.value.filter(c => c.type === 'transaction'))

  function invalidate() {
    loaded.value = false
  }

  return { tree, loaded, loading, loadCategories, invalidate, assetCategories, incomeCategories, expenseCategories, incomeExpenseCategories, transactionCategories }
})
