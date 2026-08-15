// frontend/src/stores/category.ts
// Pinia store with lazy-loading category tree (spec §6.0)
import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { categoriesApi } from '@/api/categories'
import type { Category } from '@/types'

export const useCategoryStore = defineStore('category', () => {
  // Flat list of all loaded nodes (top-level loaded eagerly, children loaded on demand)
  const allNodes = ref<Category[]>([])
  const loaded = ref(false)
  const loading = ref(false)
  // Track which parent IDs have been expanded
  const expanded = ref(new Set<number>())

  // Load top-level categories (only parent_id IS NULL)
  async function loadCategories(type?: string, force = false) {
    if (loaded.value && !force && !type) return allNodes.value
    if (loading.value && !type) return allNodes.value
    loading.value = true
    try {
      const res = await categoriesApi.list(type ? { type } : undefined)
      if (!type) {
        allNodes.value = res
        loaded.value = true
      }
      return res
    } finally {
      loading.value = false
    }
  }

  // Load children of a parent node (lazy)
  async function loadChildren(parentId: number): Promise<Category[]> {
    if (expanded.value.has(parentId)) {
      return allNodes.value.filter(c => c.parent_id === parentId)
    }
    const children = await categoriesApi.children(parentId)
    expanded.value.add(parentId)
    allNodes.value = [...allNodes.value, ...children]
    return children
  }

  // Build tree from flat list (for views that need the full tree)
  function buildTree(nodes: Category[]): Category[] {
    const map = new Map<number, Category>()
    const roots: Category[] = []
    for (const node of nodes) {
      map.set(node.id, { ...node, children: [] })
    }
    for (const node of nodes) {
      const target = map.get(node.id)!
      const parent = map.get(node.parent_id ?? -1)
      if (parent) {
        parent.children!.push(target)
      } else {
        roots.push(target)
      }
    }
    return roots
  }

  const assetCategories = computed(() => allNodes.value.filter(c => c.type === 'asset' || (!c.type && c.asset_type)))
  const incomeCategories = computed(() => allNodes.value.filter(c => c.type === 'income'))
  const expenseCategories = computed(() => allNodes.value.filter(c => c.type === 'expense'))
  const incomeExpenseCategories = computed(() => allNodes.value.filter(c => c.type === 'income' || c.type === 'expense'))
  const transactionCategories = computed(() => allNodes.value.filter(c => c.type === 'transaction'))

  function invalidate() {
    loaded.value = false
    expanded.value.clear()
  }

  return {
    allNodes, loaded, loading,
    loadCategories, loadChildren, buildTree,
    invalidate,
    assetCategories, incomeCategories, expenseCategories,
    incomeExpenseCategories, transactionCategories,
  }
})
