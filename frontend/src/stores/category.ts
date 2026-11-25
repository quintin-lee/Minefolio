/**
 * @file 分类体系状态管理 Store
 * @description 支持多级分类树懒加载 (Lazy-loading)、按类型筛选计算属性、全树构建与缓存失效控制
 */

// frontend/src/stores/category.ts
import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { categoriesApi } from '@/api/categories'
import type { Category } from '@/types'
import { getSwrCache, setSwrCache, clearSwrCache } from '@/utils/swr-cache'

const CATEGORY_CACHE_KEY = 'minefolio_cache_categories'

/**
 * 分类体系 Pinia Store
 */
export const useCategoryStore = defineStore('category', () => {
  const initialNodes = getSwrCache<Category[]>(CATEGORY_CACHE_KEY, [])
  /** 已加载的所有分类节点扁平数组 (顶级节点预加载，子节点按需加载) */
  const allNodes = ref<Category[]>(initialNodes)
  /** 顶级根分类是否已加载完毕 */
  const loaded = ref(initialNodes.length > 0)
  /** 是否正在请求加载分类数据 */
  const loading = ref(false)
  /** 记录已展开/已加载子节点的父分类 ID 集合 */
  const expanded = ref(new Set<number>())
  /** 本会话是否已完成一次后台保鲜验证 */
  let hasRevalidated = false
  /** 并发去重：在途的全量加载 Promise */
  let inflightLoad: Promise<Category[]> | null = null

  async function revalidateCategories() {
    try {
      const res = await categoriesApi.list()
      if (Array.isArray(res)) {
        allNodes.value = res
        loaded.value = true
        hasRevalidated = true
        setSwrCache(CATEGORY_CACHE_KEY, res)
      }
    } catch (err) {
      console.warn('[category store] SWR background revalidation failed:', err)
    }
  }

  /**
   * 加载分类列表 (默认加载顶级分类)
   * @param type 按指定类型筛选 (如 'asset' | 'expense' | 'income' | 'transaction'，可选)
   * @param force 是否强制跳过缓存重新从服务端请求
   * @returns 分类列表
   */
  async function loadCategories(type?: string, force = false) {
    if (loaded.value && !force && !type) {
      if (!hasRevalidated && !loading.value) {
        revalidateCategories()
      }
      return allNodes.value
    }
    if (!type && inflightLoad && !force) {
      return inflightLoad
    }
    if (type) {
      loading.value = true
      try {
        return await categoriesApi.list({ type })
      } finally {
        loading.value = false
      }
    }
    loading.value = true
    let req: Promise<Category[]> | null = null
    req = (async () => {
      try {
        const res = await categoriesApi.list()
        allNodes.value = res
        loaded.value = true
        hasRevalidated = true
        setSwrCache(CATEGORY_CACHE_KEY, res)
        return res
      } finally {
        loading.value = false
        if (inflightLoad === req) inflightLoad = null
      }
    })()
    inflightLoad = req
    return req
  }

  /**
   * 懒加载指定父分类节点下的直接子分类
   * @param parentId 父分类 ID
   * @returns 子分类节点数组
   */
  async function loadChildren(parentId: number): Promise<Category[]> {
    if (expanded.value.has(parentId)) {
      return allNodes.value.filter(c => c.parent_id === parentId)
    }
    const children = await categoriesApi.children(parentId)
    expanded.value.add(parentId)
    const seen = new Set(allNodes.value.map(c => Number(c.id)))
    const fresh = (Array.isArray(children) ? children : []).filter(c => !seen.has(Number(c.id)))
    if (fresh.length > 0) {
      allNodes.value = [...allNodes.value, ...fresh]
    }
    return children
  }

  /**
   * 将扁平或已预嵌套的分类数组组装构造成具有 children 属性的层级树形结构
   * @param nodes 分类节点列表
   * @returns 根节点树形数组
   */
  function buildTree(nodes: Category[]): Category[] {
    const map = new Map<number, Category>()
    const roots: Category[] = []

    const register = (node: Category): Category => {
      const existing = map.get(node.id)
      if (existing) return existing
      const cloned: Category = {
        ...node,
        children: Array.isArray(node.children) ? node.children.map(register) : [],
      }
      map.set(node.id, cloned)
      return cloned
    }

    for (const node of nodes) {
      register(node)
    }

    const childIdSet = new Set<number>()
    for (const cloned of map.values()) {
      for (const ch of cloned.children || []) {
        childIdSet.add(ch.id)
      }
    }

    for (const node of nodes) {
      const target = map.get(node.id)!
      if (childIdSet.has(node.id)) continue
      const parent = node.parent_id != null ? map.get(node.parent_id) : undefined
      if (parent && parent.id !== target.id) {
        if (!parent.children!.some(c => c.id === target.id)) {
          parent.children!.push(target)
        }
      } else {
        roots.push(target)
      }
    }
    return roots
  }

  /** 资产类分类列表计算属性 */
  const assetCategories = computed(() => allNodes.value.filter(c => c.type === 'asset' || (!c.type && c.asset_type)))
  /** 收入类分类列表计算属性 */
  const incomeCategories = computed(() => allNodes.value.filter(c => c.type === 'income'))
  /** 支出类分类列表计算属性 */
  const expenseCategories = computed(() => allNodes.value.filter(c => c.type === 'expense'))
  /** 包含收入与支出的分类列表计算属性 */
  const incomeExpenseCategories = computed(() => allNodes.value.filter(c => c.type === 'income' || c.type === 'expense'))
  /** 交易流水类分类列表计算属性 */
  const transactionCategories = computed(() => allNodes.value.filter(c => c.type === 'transaction'))

  /**
   * 清除本地分类缓存标记 (在分类新增、修改或删除后调用)
   */
  function invalidate() {
    loaded.value = false
    expanded.value.clear()
    hasRevalidated = false
    clearSwrCache(CATEGORY_CACHE_KEY)
  }

  /**
   * 清空全部分类缓存 (退出登录时调用，连同已加载节点一并清空，
   * 防止上一账号的分类树泄露给同一设备上的下一位登录用户)
   */
  function reset() {
    allNodes.value = []
    loaded.value = false
    loading.value = false
    expanded.value.clear()
    hasRevalidated = false
    clearSwrCache(CATEGORY_CACHE_KEY)
  }

  return {
    allNodes,
    loaded,
    loading,
    loadCategories,
    loadChildren,
    buildTree,
    invalidate,
    reset,
    assetCategories,
    incomeCategories,
    expenseCategories,
    incomeExpenseCategories,
    transactionCategories,
  }
})

