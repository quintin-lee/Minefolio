import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { findCategoryPath } from '@/utils/category-tree'
import { useCategoryStore } from '@/stores/category'
import { useMarketStore } from '@/stores/market'

vi.mock('@/api/categories', () => ({
  categoriesApi: {
    list: vi.fn(),
    children: vi.fn(),
  },
}))

vi.mock('@/api/market', () => ({
  marketApi: {
    getExchangeRates: vi.fn(),
    getFxHistory: vi.fn(),
    getMultiCurrencySummary: vi.fn(),
    getFxDirections: vi.fn(),
    syncAll: vi.fn(),
    syncExchangeRates: vi.fn(),
    updateExchangeRate: vi.fn(),
    getIndices: vi.fn(),
  },
}))

import { categoriesApi } from '@/api/categories'
import { marketApi } from '@/api/market'
import type { Category } from '@/types'

describe('frontend audit phase 3 fixes', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    localStorage.clear()
    vi.clearAllMocks()
  })

  it('findCategoryPath resolves nested paths across tree and flat shapes', () => {
    const nested: Category[] = [
      {
        id: 1,
        name: '饮食',
        type: 'expense',
        parent_id: null,
        sort_order: 0,
        created_at: '',
        children: [
          {
            id: 2,
            name: '咖啡茶饮',
            type: 'expense',
            parent_id: 1,
            sort_order: 0,
            created_at: '',
          },
        ],
      },
    ]
    expect(findCategoryPath(nested, 2)).toEqual([1, 2])
    expect(findCategoryPath(nested, 999)).toEqual([999])
    expect(findCategoryPath([], 999)).toEqual([])
  })

  it('categoryStore deduplicates concurrent loadCategories and preserves pre-nested children', async () => {
    let resolveList!: (val: any) => void
    const deferred = new Promise<any>((r) => {
      resolveList = r
    })
    vi.mocked(categoriesApi.list).mockReturnValueOnce(deferred)

    const store = useCategoryStore()
    const p1 = store.loadCategories()
    const p2 = store.loadCategories()
    expect(categoriesApi.list).toHaveBeenCalledTimes(1)

    const nestedInput: Category[] = [
      {
        id: 10,
        name: '股票',
        type: 'asset',
        parent_id: null,
        sort_order: 0,
        created_at: '',
        children: [
          {
            id: 11,
            name: '港股',
            type: 'asset',
            parent_id: 10,
            sort_order: 0,
            created_at: '',
          },
        ],
      },
    ]
    resolveList({ list: nestedInput, total: 1, page: 1, page_size: 500 })
    await Promise.all([p1, p2])

    const tree = store.buildTree(nestedInput)
    expect(tree).toHaveLength(1)
    expect(tree[0].children).toHaveLength(1)
    expect(tree[0].children![0].id).toBe(11)
  })

  it('marketStore guards stale fetchHoldingsSummary responses and resets cleanly', async () => {
    let resolveSlow!: (val: any) => void
    const slow = new Promise((r) => {
      resolveSlow = r
    })
    vi.mocked(marketApi.getMultiCurrencySummary)
      .mockReturnValueOnce(slow as any)
      .mockResolvedValueOnce({
        base_currency: 'USD',
        total_in_base: 500,
        currencies: [{ currency: 'USD', original_total: 500, fx_rate: 1, converted_total: 500, asset_count: 1, weight_pct: 100 }],
      })

    const store = useMarketStore()
    const pSlow = store.fetchHoldingsSummary('CNY')
    const pFast = store.fetchHoldingsSummary('USD')
    await pFast
    expect(store.holdingsSummary?.base_currency).toBe('USD')

    resolveSlow({
      base_currency: 'CNY',
      total_in_base: 3600,
      currencies: [],
    })
    await pSlow
    expect(store.holdingsSummary?.base_currency).toBe('USD')

    store.reset()
    expect(store.holdingsSummary).toBeNull()
    expect(store.historyPoints).toEqual([])
  })
})
