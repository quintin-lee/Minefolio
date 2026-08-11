// frontend/src/stores/category.ts
// Pinia store caching the category tree (spec §6.0)
import { defineStore } from 'pinia'
import { ref } from 'vue'
import { categoriesApi } from '@/api/categories'
import type { Category } from '@/types'

export const useCategoryStore = defineStore('category', () => {
  const tree = ref<Category[]>([])
  const loaded = ref(false)
  const loading = ref(false)

  async function loadCategories(force = false) {
    if (loaded.value && !force) return tree.value
    if (loading.value) return tree.value
    loading.value = true
    try {
      const res = await categoriesApi.list()
      tree.value = res.data
      loaded.value = true
      return tree.value
    } finally {
      loading.value = false
    }
  }

  function invalidate() {
    loaded.value = false
  }

  return { tree, loaded, loading, loadCategories, invalidate }
})
