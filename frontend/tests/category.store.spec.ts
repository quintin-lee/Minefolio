import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { useCategoryStore } from '@/stores/category'

vi.mock('@/api/categories', () => ({
  categoriesApi: {
    list: vi.fn(),
    children: vi.fn(),
  },
}))

import { categoriesApi } from '@/api/categories'
import type { Category } from '@/types'

describe('category store', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
  })

  it('loads categories and caches them', async () => {
    const mockCategories: Category[] = [
      { id: 1, name: '餐饮', type: 'expense', parent_id: null, sort_order: 0, created_at: '' },
      { id: 2, name: '工资', type: 'income', parent_id: null, sort_order: 0, created_at: '' },
      { id: 3, name: '银行账户', type: 'asset', asset_type: 'cash', parent_id: null, sort_order: 0, created_at: '' },
    ]
    ;(categoriesApi.list as any).mockResolvedValue(mockCategories)

    const store = useCategoryStore()
    expect(store.loaded).toBe(false)

    const res = await store.loadCategories()
    expect(res).toHaveLength(3)
    expect(store.loaded).toBe(true)
    expect(store.allNodes).toHaveLength(3)

    // Filter computeds
    expect(store.expenseCategories).toHaveLength(1)
    expect(store.incomeCategories).toHaveLength(1)
    expect(store.assetCategories).toHaveLength(1)
    expect(store.incomeExpenseCategories).toHaveLength(2)

    // Call again should use cache
    await store.loadCategories()
    expect(categoriesApi.list).toHaveBeenCalledTimes(1)
  })

  it('invalidates cache correctly', async () => {
    const store = useCategoryStore()
    store.loaded = true
    store.invalidate()
    expect(store.loaded).toBe(false)
  })

  it('builds category tree with parent-child relationships', () => {
    const store = useCategoryStore()
    const flatList: Category[] = [
      { id: 1, name: '餐饮', type: 'expense', parent_id: null, sort_order: 0, created_at: '' },
      { id: 2, name: '午餐', type: 'expense', parent_id: 1, sort_order: 0, created_at: '' },
      { id: 3, name: '晚餐', type: 'expense', parent_id: 1, sort_order: 1, created_at: '' },
      { id: 4, name: '交通', type: 'expense', parent_id: null, sort_order: 1, created_at: '' },
    ]

    const tree = store.buildTree(flatList)
    expect(tree).toHaveLength(2) // 餐饮, 交通
    expect(tree[0].name).toBe('餐饮')
    expect(tree[0].children).toHaveLength(2)
    expect(tree[0].children![0].name).toBe('午餐')
    expect(tree[0].children![1].name).toBe('晚餐')
    expect(tree[1].name).toBe('交通')
    expect(tree[1].children).toHaveLength(0)
  })
})
