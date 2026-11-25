import { describe, it, expect, vi, beforeEach } from 'vitest'
import { mount, flushPromises } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import Transactions from '@/views/Transactions.vue'

vi.mock('@/api/transactions', () => ({
  transactionsApi: {
    list: vi.fn().mockResolvedValue({ list: [], total: 0 }),
    monthly: vi.fn().mockResolvedValue({ total_volume: 0, inflows: 0, outflows: 0, count: 0 }),
    create: vi.fn().mockResolvedValue({ id: 1 }),
    update: vi.fn().mockResolvedValue(undefined),
    delete: vi.fn().mockResolvedValue(undefined),
  },
}))

vi.mock('@/api/assets', () => ({
  assetsApi: {
    list: vi.fn().mockResolvedValue({
      list: [
        { id: 29, name: '工资卡', asset_type: 'cash', current_value: 50000, currency: 'CNY', quantity: 0 },
        { id: 32, name: '测试A股', asset_type: 'stock', current_value: 10000, currency: 'CNY', quantity: 1000 },
      ],
      total: 2,
    }),
  },
}))

vi.mock('@/stores/category', () => ({
  useCategoryStore: () => ({
    transactionCategories: [],
    loadCategories: vi.fn().mockResolvedValue(undefined),
    loadChildren: vi.fn().mockResolvedValue([]),
  }),
}))

describe('Transactions.vue asset filtering by transaction type', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
  })

  it('constrains sell/buy target assets to investment assets and linked accounts to funding accounts', async () => {
    const wrapper = mount(Transactions, {
      global: {
        stubs: {
          SummaryCard: { template: '<div />' },
          'el-card': { template: '<div><slot /></div>' },
          'el-row': { template: '<div><slot /></div>' },
          'el-col': { template: '<div><slot /></div>' },
          'el-form': { template: '<form><slot /></form>' },
          'el-form-item': { template: '<div><slot /></div>' },
          'el-select': {
            props: ['modelValue'],
            emits: ['update:modelValue', 'change'],
            template: '<select><slot /></select>',
          },
          'el-option': { props: ['label', 'value', 'disabled'], template: '<option />' },
          'el-input': { template: '<input />' },
          'el-input-number': { template: '<input />' },
          'el-date-picker': { template: '<input />' },
          'el-cascader': { template: '<input />' },
          'el-button': { template: '<button><slot /></button>' },
          'el-table': { template: '<div><slot /></div>' },
          'el-table-column': { template: '<div />' },
          'el-pagination': { template: '<div />' },
          'el-dialog': { template: '<div><slot /><slot name="footer" /></div>' },
          'el-steps': { template: '<div />' },
          'el-step': { template: '<div />' },
          'el-divider': { template: '<hr />' },
          'el-alert': { template: '<div />' },
          'el-upload': { template: '<div />' },
          'el-progress': { template: '<div />' },
          'el-icon': { template: '<i />' },
          'el-tag': { template: '<span />' },
          'el-tooltip': { template: '<div><slot /></div>' },
          'el-empty': { template: '<div />' },
        },
      },
    })
    await flushPromises()

    const vm = wrapper.vm as any
    vm.openDialog()
    vm.form.transaction_type = 'sell'
    await flushPromises()

    expect(vm.selectableTargetAssets.map((a: any) => a.id)).toEqual([32])
    expect(vm.selectableLinkedAssets.map((a: any) => a.id)).toEqual([29])

    // Switching from deposit (工资卡 id=29) to sell clears incompatible target asset
    vm.form.transaction_type = 'deposit'
    vm.onTransactionTypeChange('deposit')
    vm.form.asset_id = 29
    vm.form.transaction_type = 'sell'
    vm.onTransactionTypeChange('sell')
    expect(vm.form.asset_id).toBeNull()

    // Editing preserves numeric asset_id and clears corrupted non-investment asset on sell
    vm.openDialog({
      id: 99,
      asset_id: 32,
      linked_asset_id: 29,
      transaction_type: 'sell',
      amount: 500,
      quantity: 50,
      price_per_unit: 10,
      transaction_date: '2026-11-25',
    })
    expect(vm.form.asset_id).toBe(32)
    expect(vm.form.linked_asset_id).toBe(29)

    vm.openDialog({
      id: 100,
      asset_id: 29,
      linked_asset_id: 0,
      transaction_type: 'sell',
      amount: 500,
      quantity: 50,
      price_per_unit: 10,
      transaction_date: '2026-11-25',
    })
    expect(vm.form.asset_id).toBeNull()
  })
})
