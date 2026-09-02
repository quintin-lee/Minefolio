/**
 * @file 分类体系状态管理 Store
 * @description 支持多级分类树懒加载 (Lazy-loading)、按类型筛选计算属性、全树构建与缓存失效控制
 */

// frontend/src/stores/category.ts
import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { categoriesApi } from '@/api/categories'
import type { Category } from '@/types'

/**
 * 分类体系 Pinia Store
 */
export const useCategoryStore = defineStore('category', () => {
  /** 已加载的所有分类节点扁平数组 (顶级节点预加载，子节点按需加载) */
  const allNodes = ref<Category[]>([])
  /** 顶级根分类是否已加载完毕 */
  const loaded = ref(false)
  /** 是否正在请求加载分类数据 */
  const loading = ref(false)
  /** 记录已展开/已加载子节点的父分类 ID 集合 */
  const expanded = ref(new Set<number>())

  /**
   * 加载分类列表 (默认加载顶级分类)
   * @param type 按指定类型筛选 (如 'asset' | 'expense' | 'income' | 'transaction'，可选)
   * @param force 是否强制跳过缓存重新从服务端请求
   * @returns 分类列表
   */
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
    allNodes.value = [...allNodes.value, ...children]
    return children
  }

  /**
   * 将扁平分类数组组装构造成具有 children 属性的层级树形结构
   * @param nodes 扁平分类节点列表
   * @returns 根节点树形数组
   */
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
  }

  return {
    allNodes,
    loaded,
    loading,
    loadCategories,
    loadChildren,
    buildTree,
    invalidate,
    assetCategories,
    incomeCategories,
    expenseCategories,
    incomeExpenseCategories,
    transactionCategories,
  }
})

