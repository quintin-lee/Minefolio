/**
 * @file 分类体系状态管理 Store
 * @description 支持多级分类树懒加载 (Lazy-loading)、按类型筛选计算属性、全树构建与缓存失效控制
 */

// frontend/src/stores/category.ts
import { defineStore } from 'pinia'
import { ref, shallowRef, computed } from 'vue'
import { categoriesApi } from '@/api/categories'
import type { Category } from '@/types'
import { getSwrCache, setSwrCache, clearSwrCache } from '@/utils/swr-cache'
import { fetchAllPages } from '@/utils/pagination'

const CATEGORY_CACHE_KEY = 'minefolio_cache_categories'

/**
 * 分类体系 Pinia Store
 */
export const useCategoryStore = defineStore('category', () => {
  function findNodeById(nodes: Category[], id: number): Category | null {
    const targetId = Number(id)
    for (const node of nodes) {
      if (Number(node.id) === targetId) return node
      if (Array.isArray(node.children) && node.children.length > 0) {
        const found = findNodeById(node.children, targetId)
        if (found) return found
      }
    }
    return null
  }

  function collectIds(nodes: Category[], out = new Set<number>()): Set<number> {
    for (const node of nodes) {
      out.add(Number(node.id))
      if (Array.isArray(node.children) && node.children.length > 0) {
        collectIds(node.children, out)
      }
    }
    return out
  }

  function sanitizeNodes(nodes: Category[]): Category[] {
    if (!Array.isArray(nodes) || nodes.length === 0) return []
    const nestedChildIds = new Set<number>()
    const markChildren = (children?: Category[]) => {
      if (!Array.isArray(children)) return
      for (const ch of children) {
        nestedChildIds.add(Number(ch.id))
        markChildren(ch.children)
      }
    }
    for (const n of nodes) {
      markChildren(n.children)
    }

    const dedupeLevel = (list: Category[], skipNestedTop: boolean): Category[] => {
      const seen = new Set<number>()
      const out: Category[] = []
      for (const item of list) {
        const id = Number(item.id)
        if (seen.has(id)) continue
        if (skipNestedTop && nestedChildIds.has(id)) continue
        seen.add(id)
        const copy: Category = { ...item, id }
        if (Array.isArray(item.children)) {
          copy.children = dedupeLevel(item.children, false)
        }
        out.push(copy)
      }
      return out
    }

    return dedupeLevel(nodes, nestedChildIds.size > 0)
  }

  const initialNodes = sanitizeNodes(getSwrCache<Category[]>(CATEGORY_CACHE_KEY, []))
  /** 已加载的所有分类节点扁平数组 (顶级节点预加载，子节点按需加载) */
  const allNodes = shallowRef<Category[]>(initialNodes)
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
  /** 异步代际计数：防止 reset()/invalidate() 后在途请求回写旧账号或过期分类树 */
  let loadEpoch = 0

  async function revalidateCategories() {
    const epoch = loadEpoch
    try {
      const res = await fetchAllPages((page, page_size) => categoriesApi.list({ page, page_size }))
      if (epoch !== loadEpoch) return
      const cleaned = sanitizeNodes(res)
      allNodes.value = cleaned
      loaded.value = true
      hasRevalidated = true
      setSwrCache(CATEGORY_CACHE_KEY, cleaned)
    } catch (err) {
      if (epoch === loadEpoch) {
        console.warn('[category store] SWR background revalidation failed:', err)
      }
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
      const epoch = loadEpoch
      loading.value = true
      try {
        const raw = await fetchAllPages((page, page_size) =>
          categoriesApi.list({ type, page, page_size }),
        )
        const res = sanitizeNodes(raw)
        if (epoch !== loadEpoch) return []
        return res
      } finally {
        if (epoch === loadEpoch) loading.value = false
      }
    }
    const epoch = loadEpoch
    loading.value = true
    let req: Promise<Category[]> | null = null
    req = (async () => {
      try {
        const raw = await fetchAllPages((page, page_size) =>
          categoriesApi.list({ page, page_size }),
        )
        const res = sanitizeNodes(raw)
        if (epoch !== loadEpoch) return []
        allNodes.value = res
        loaded.value = true
        hasRevalidated = true
        setSwrCache(CATEGORY_CACHE_KEY, res)
        return res
      } finally {
        if (epoch === loadEpoch) loading.value = false
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
    const pid = Number(parentId)
    const parent = findNodeById(allNodes.value, pid)
    if (parent && Array.isArray(parent.children) && parent.children.length > 0) {
      expanded.value.add(pid)
      return parent.children
    }
    if (expanded.value.has(pid)) {
      return parent?.children?.length
        ? parent.children
        : allNodes.value.filter(c => Number(c.parent_id) === pid)
    }
    const epoch = loadEpoch
    const rawChildren = await categoriesApi.children(pid)
    if (epoch !== loadEpoch) return []
    const children = Array.isArray(rawChildren) ? rawChildren : []
    expanded.value.add(pid)
    if (parent) {
      const seen = new Set((parent.children || []).map(c => Number(c.id)))
      const fresh = children.filter(c => !seen.has(Number(c.id)))
      parent.children = [...(parent.children || []), ...fresh]
      allNodes.value = [...allNodes.value]
    } else if (!allNodes.value.some(n => Array.isArray(n.children) && n.children.length > 0)) {
      const seen = collectIds(allNodes.value)
      const fresh = children.filter(c => !seen.has(Number(c.id)))
      if (fresh.length > 0) {
        allNodes.value = [...allNodes.value, ...fresh]
      }
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
      const id = Number(node.id)
      const existing = map.get(id)
      if (existing) return existing
      const cloned: Category = {
        ...node,
        id,
        children: Array.isArray(node.children) ? node.children.map(register) : [],
      }
      map.set(id, cloned)
      return cloned
    }

    for (const node of nodes) {
      register(node)
    }

    const childIdSet = new Set<number>()
    const childIdsByParent = new Map<number, Set<number>>()
    for (const cloned of map.values()) {
      const cset = new Set<number>()
      for (const ch of cloned.children || []) {
        const chId = Number(ch.id)
        childIdSet.add(chId)
        cset.add(chId)
      }
      childIdsByParent.set(cloned.id, cset)
    }

    const addedRootIds = new Set<number>()
    for (const target of map.values()) {
      if (childIdSet.has(target.id)) continue
      const parent = target.parent_id != null ? map.get(Number(target.parent_id)) : undefined
      if (parent && parent.id !== target.id) {
        let cset = childIdsByParent.get(parent.id)
        if (!cset) {
          cset = new Set<number>()
          childIdsByParent.set(parent.id, cset)
        }
        if (!cset.has(target.id)) {
          cset.add(target.id)
          parent.children!.push(target)
        }
      } else if (!addedRootIds.has(target.id)) {
        addedRootIds.add(target.id)
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
    loadEpoch++
    inflightLoad = null
    loaded.value = false
    loading.value = false
    expanded.value.clear()
    hasRevalidated = false
    clearSwrCache(CATEGORY_CACHE_KEY)
  }

  /**
   * 清空全部分类缓存 (退出登录时调用，连同已加载节点一并清空，
   * 防止上一账号的分类树泄露给同一设备上的下一位登录用户)
   */
  function reset() {
    loadEpoch++
    inflightLoad = null
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

