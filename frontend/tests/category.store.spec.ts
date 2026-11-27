import { describe, it, expect, beforeEach, vi } from 'vitest'
import { computed } from 'vue'
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
    localStorage.clear()
    setActivePinia(createPinia())
    vi.clearAllMocks()
  })

  it('loads categories and caches them', async () => {
    const mockCategories: Category[] = [
      { id: 1, name: '餐饮', type: 'expense', parent_id: null, sort_order: 0, created_at: '' },
      { id: 2, name: '工资', type: 'income', parent_id: null, sort_order: 0, created_at: '' },
      { id: 3, name: '银行账户', type: 'asset', asset_type: 'cash', parent_id: null, sort_order: 0, created_at: '' },
    ]
    ;(categoriesApi.list as any).mockResolvedValue({ list: mockCategories, total: 3, page: 1, page_size: 500 })

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

  it('does not append nested children to top-level allNodes in loadChildren', async () => {
    const nested: Category[] = [
      {
        id: 1,
        name: '餐饮美食',
        type: 'expense',
        parent_id: null,
        currency: 'CNY',
        sort_order: 1,
        children: [
          { id: 101, name: '咖啡奶茶', type: 'expense', parent_id: 1, currency: 'CNY', sort_order: 1 },
        ],
      },
    ]
    ;(categoriesApi.list as any).mockResolvedValue({ list: nested, total: 1, page: 1, page_size: 500 })

    const store = useCategoryStore()
    await store.loadCategories()

    const children = await store.loadChildren(1)
    expect(children).toHaveLength(1)
    expect(categoriesApi.children).not.toHaveBeenCalled()
    expect(store.allNodes).toHaveLength(1)
    expect(store.expenseCategories).toHaveLength(1)

    // Even when parent has empty children initially, fetched children attach under parent instead of top-level allNodes
    ;(categoriesApi.list as any).mockResolvedValue({
      list: [
        { id: 2, name: '交通出行', type: 'expense', parent_id: null, currency: 'CNY', sort_order: 2, children: [] },
      ],
      total: 1,
      page: 1,
      page_size: 500,
    })
    ;(categoriesApi.children as any).mockResolvedValue([
      { id: 201, name: '公共交通', type: 'expense', parent_id: 2, currency: 'CNY', sort_order: 1 },
    ])
    await store.loadCategories(undefined, true)
    const loadedChildren = await store.loadChildren(2)
    expect(loadedChildren).toHaveLength(1)
    expect(store.allNodes).toHaveLength(1)
    expect(store.allNodes[0].children).toHaveLength(1)
    expect(store.allNodes[0].children![0].id).toBe(201)
  })

  it('sanitizes mixed nested and stray flat duplicate nodes on loadCategories', async () => {
    const dirty: Category[] = [
      {
        id: 1,
        name: '餐饮美食',
        type: 'expense',
        parent_id: null,
        currency: 'CNY',
        sort_order: 1,
        children: [
          { id: 101, name: '咖啡奶茶', type: 'expense', parent_id: 1, currency: 'CNY', sort_order: 1 },
        ],
      },
      { id: 101, name: '咖啡奶茶', type: 'expense', parent_id: 1, currency: 'CNY', sort_order: 1 },
      { id: 1, name: '餐饮美食', type: 'expense', parent_id: null, currency: 'CNY', sort_order: 1 },
    ]
    ;(categoriesApi.list as any).mockResolvedValue({ list: dirty, total: 3, page: 1, page_size: 500 })

    const store = useCategoryStore()
    const res = await store.loadCategories()
    expect(res).toHaveLength(1)
    expect(res[0].id).toBe(1)
    expect(res[0].children).toHaveLength(1)
  })

  it('deduplicates duplicate root IDs in buildTree', () => {
    const store = useCategoryStore()
    const dupList: Category[] = [
      { id: 1, name: '餐饮', type: 'expense', parent_id: null, currency: 'CNY', sort_order: 0 },
      { id: 1, name: '餐饮', type: 'expense', parent_id: null, currency: 'CNY', sort_order: 0 },
    ]
    const tree = store.buildTree(dupList)
    expect(tree).toHaveLength(1)
  })

  it('triggers reactive computed updates when loadChildren attaches children under shallowRef', async () => {
    ;(categoriesApi.list as any).mockResolvedValue({
      list: [
        { id: 10, name: '交通出行', type: 'expense', parent_id: null, currency: 'CNY', sort_order: 1, children: [] },
      ],
      total: 1,
      page: 1,
      page_size: 500,
    })
    ;(categoriesApi.children as any).mockResolvedValue([
      { id: 101, name: '公共交通', type: 'expense', parent_id: 10, currency: 'CNY', sort_order: 1 },
      { id: 102, name: '打车网约车', type: 'expense', parent_id: 10, currency: 'CNY', sort_order: 2 },
    ])

    const store = useCategoryStore()
    await store.loadCategories()
    const childCount = computed(() => store.expenseCategories[0]?.children?.length ?? 0)
    expect(childCount.value).toBe(0)

    await store.loadChildren(10)
    expect(childCount.value).toBe(2)
  })

  it('preserves parent-child order without duplicates when pre-nested and flat items mix in buildTree', () => {
    const store = useCategoryStore()
    const mixed: Category[] = [
      {
        id: 1,
        name: '餐饮',
        type: 'expense',
        parent_id: null,
        sort_order: 1,
        children: [{ id: 11, name: '早餐', type: 'expense', parent_id: 1, sort_order: 1 }],
      },
      { id: 11, name: '早餐', type: 'expense', parent_id: 1, sort_order: 1 },
      { id: 12, name: '午餐', type: 'expense', parent_id: 1, sort_order: 2 },
      { id: 13, name: '晚餐', type: 'expense', parent_id: 1, sort_order: 3 },
    ]
    const tree = store.buildTree(mixed)
    expect(tree).toHaveLength(1)
    expect(tree[0].children?.map(c => c.name)).toEqual(['早餐', '午餐', '晚餐'])
  })
})
