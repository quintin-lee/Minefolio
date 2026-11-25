import { describe, it, expect } from 'vitest'
import {
  prepareCategoryTree,
  isIdInTree,
  findCategoryByName,
} from '@/utils/category-tree'
import type { Category } from '@/types'

describe('category-tree utility', () => {
  describe('prepareCategoryTree', () => {
    it('returns empty array when input is null, undefined, or empty', () => {
      expect(prepareCategoryTree([])).toEqual([])
      expect(prepareCategoryTree(null as any)).toEqual([])
      expect(prepareCategoryTree(undefined as any)).toEqual([])
    })

    it('processes nested online category tree and removes empty children', () => {
      const nestedData: any[] = [
        {
          id: 1,
          name: '餐饮美食',
          type: 'expense',
          parent_id: null,
          children: [
            { id: 101, name: '早餐', type: 'expense', parent_id: 1, children: [] },
            { id: 102, name: '午餐', type: 'expense', parent_id: 1 },
          ],
        },
        {
          id: 2,
          name: '交通出行',
          type: 'expense',
          parent_id: null,
          children: [],
        },
        {
          id: 3,
          name: '职业收入',
          type: 'income',
          parent_id: null,
          children: [
            { id: 301, name: '工资薪酬', type: 'income', parent_id: 3 },
          ],
        },
      ]

      // Filter by expense
      const expenseTree = prepareCategoryTree(nestedData as Category[], 'expense')
      expect(expenseTree).toHaveLength(2)
      expect(expenseTree[0].name).toBe('餐饮美食')
      expect(expenseTree[0].children).toHaveLength(2)
      // Empty children array was cleaned/removed
      expect(expenseTree[0].children![0].children).toBeUndefined()
      expect(expenseTree[1].name).toBe('交通出行')
      expect(expenseTree[1].children).toBeUndefined()

      // Filter by income
      const incomeTree = prepareCategoryTree(nestedData as Category[], 'income')
      expect(incomeTree).toHaveLength(1)
      expect(incomeTree[0].name).toBe('职业收入')
      expect(incomeTree[0].children).toHaveLength(1)
      expect(incomeTree[0].children![0].name).toBe('工资薪酬')
      expect(incomeTree[0].children![0].children).toBeUndefined()

      // All types
      const allTree = prepareCategoryTree(nestedData as Category[], 'all')
      expect(allTree).toHaveLength(3)
    })

    it('assembles flat offline SQLite rows into a hierarchical cascader tree', () => {
      const flatRows: Category[] = [
        { id: 10, name: '居家生活', type: 'expense', parent_id: null, sort_order: 1, created_at: '' },
        { id: 11, name: '水电燃气', type: 'expense', parent_id: 10, sort_order: 1, created_at: '' },
        { id: 12, name: '物业维修', type: 'expense', parent_id: 10, sort_order: 2, created_at: '' },
        { id: 20, name: '经营收入', type: 'income', parent_id: null, sort_order: 1, created_at: '' },
        { id: 21, name: '副业收益', type: 'income', parent_id: 20, sort_order: 1, created_at: '' },
      ]

      const expenseTree = prepareCategoryTree(flatRows, 'expense')
      expect(expenseTree).toHaveLength(1)
      expect(expenseTree[0].id).toBe(10)
      expect(expenseTree[0].name).toBe('居家生活')
      expect(expenseTree[0].children).toHaveLength(2)
      expect(expenseTree[0].children![0].name).toBe('水电燃气')
      expect(expenseTree[0].children![1].name).toBe('物业维修')
      expect(expenseTree[0].children![0].children).toBeUndefined()

      const incomeTree = prepareCategoryTree(flatRows, 'income')
      expect(incomeTree).toHaveLength(1)
      expect(incomeTree[0].id).toBe(20)
      expect(incomeTree[0].name).toBe('经营收入')
      expect(incomeTree[0].children).toHaveLength(1)
      expect(incomeTree[0].children![0].name).toBe('副业收益')
    })

    it('handles orphaned flat rows safely without crashing', () => {
      const flatRows: Category[] = [
        { id: 99, name: '子分类无父级', type: 'expense', parent_id: 9999, sort_order: 1, created_at: '' },
      ]
      const tree = prepareCategoryTree(flatRows, 'expense')
      expect(tree).toHaveLength(1)
      expect(tree[0].id).toBe(99)
      expect(tree[0].children).toBeUndefined()
    })

    it('strips stray flat child nodes when mixed with nested tree roots', () => {
      const mixed: any[] = [
        {
          id: 1,
          name: '餐饮美食',
          type: 'expense',
          parent_id: null,
          children: [
            { id: 101, name: '咖啡奶茶', type: 'expense', parent_id: 1 },
          ],
        },
        { id: 101, name: '咖啡奶茶', type: 'expense', parent_id: 1 },
        { id: 1, name: '餐饮美食', type: 'expense', parent_id: null },
      ]
      const tree = prepareCategoryTree(mixed as Category[], 'expense')
      expect(tree).toHaveLength(1)
      expect(tree[0].id).toBe(1)
      expect(tree[0].children).toHaveLength(1)
      expect(tree[0].children![0].id).toBe(101)
    })

    it('deduplicates duplicate flat rows by id', () => {
      const flatDup: Category[] = [
        { id: 10, name: '餐饮', type: 'expense', parent_id: null, currency: 'CNY', sort_order: 1 },
        { id: 10, name: '餐饮', type: 'expense', parent_id: null, currency: 'CNY', sort_order: 1 },
        { id: 11, name: '早餐', type: 'expense', parent_id: 10, currency: 'CNY', sort_order: 1 },
        { id: 11, name: '早餐', type: 'expense', parent_id: 10, currency: 'CNY', sort_order: 1 },
      ]
      const tree = prepareCategoryTree(flatDup, 'expense')
      expect(tree).toHaveLength(1)
      expect(tree[0].children).toHaveLength(1)
    })
  })

  describe('isIdInTree', () => {
    const tree = [
      {
        id: 1,
        name: '餐饮',
        type: 'expense',
        parent_id: null,
        children: [
          { id: 11, name: '早餐', type: 'expense', parent_id: 1 },
          {
            id: 12,
            name: '正餐',
            type: 'expense',
            parent_id: 1,
            children: [{ id: 121, name: '火锅', type: 'expense', parent_id: 12 }],
          },
        ],
      },
    ]

    it('returns true for root node id', () => {
      expect(isIdInTree(tree, 1)).toBe(true)
    })

    it('returns true for level-2 child node id', () => {
      expect(isIdInTree(tree, 11)).toBe(true)
    })

    it('returns true for level-3 child node id', () => {
      expect(isIdInTree(tree, 121)).toBe(true)
    })

    it('returns false for non-existent id', () => {
      expect(isIdInTree(tree, 999)).toBe(false)
      expect(isIdInTree([], 1)).toBe(false)
    })
  })

  describe('findCategoryByName', () => {
    const tree = [
      {
        id: 1,
        name: '餐饮美食',
        type: 'expense',
        parent_id: null,
        children: [
          { id: 11, name: '咖啡奶茶', type: 'expense', parent_id: 1 },
          { id: 12, name: '快餐便当', type: 'expense', parent_id: 1 },
        ],
      },
      {
        id: 2,
        name: '交通',
        type: 'expense',
        parent_id: null,
      },
    ]

    it('matches exact name on root node', () => {
      const found = findCategoryByName(tree, '交通')
      expect(found).not.toBeNull()
      expect(found!.id).toBe(2)
    })

    it('matches exact name on child node', () => {
      const found = findCategoryByName(tree, '咖啡奶茶')
      expect(found).not.toBeNull()
      expect(found!.id).toBe(11)
    })

    it('matches fuzzy substring on child node', () => {
      const found = findCategoryByName(tree, '便当')
      expect(found).not.toBeNull()
      expect(found!.id).toBe(12)
    })

    it('matches fuzzy substring where target is longer than category name', () => {
      const found = findCategoryByName(tree, '今日交通支出')
      expect(found).not.toBeNull()
      expect(found!.id).toBe(2)
    })

    it('returns null for empty or non-matching string', () => {
      expect(findCategoryByName(tree, '')).toBeNull()
      expect(findCategoryByName(tree, '   ')).toBeNull()
      expect(findCategoryByName(tree, '股票投资')).toBeNull()
    })
  })
})
