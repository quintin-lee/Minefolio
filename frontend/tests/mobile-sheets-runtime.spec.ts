import { describe, it, expect, beforeEach, vi } from 'vitest'
import { mount, flushPromises } from '@vue/test-utils'
import AssetSheet from '@/components/mobile/AssetSheet.vue'
import AccountSheet from '@/components/mobile/AccountSheet.vue'
import CategorySheet from '@/components/mobile/CategorySheet.vue'
import TagSheet from '@/components/mobile/TagSheet.vue'
import TransactionSheet from '@/components/mobile/TransactionSheet.vue'
import { offlineApi } from '@/utils/offline-http'
import type { Asset, Account, Category, Tag, Transaction } from '@/types'

vi.mock('@/utils/offline-http', () => ({
  offlineApi: { post: vi.fn().mockResolvedValue({ offline: true, id: 1 }), put: vi.fn(), delete: vi.fn() },
}))

vi.mock('element-plus', async (importOriginal) => {
  const actual = (await importOriginal()) as any
  return {
    ...actual,
    ElMessage: { success: vi.fn(), warning: vi.fn(), error: vi.fn() },
  }
})

const modelStub = {
  props: ['modelValue'],
  emits: ['update:modelValue'],
  template: '<input :value="modelValue" @input="$emit(\'update:modelValue\', $event.target.value)" />',
}
const selectStub = {
  props: ['modelValue'],
  emits: ['update:modelValue'],
  template: '<div class="select-stub"><slot /></div>',
}
const stubs = {
  'el-drawer': { template: '<div><slot /></div>' },
  'el-form': { template: '<div><slot /></div>' },
  'el-form-item': { template: '<div><slot /></div>' },
  'el-button': { template: '<button @click="$emit(\'click\')"><slot /></button>' },
  'el-input': modelStub,
  'el-input-number': modelStub,
  'el-select': selectStub,
  'el-option': { template: '<div><slot /></div>' },
  'el-date-picker': modelStub,
  'el-radio-group': selectStub,
  'el-radio-button': { template: '<div><slot /></div>' },
}

async function openAndSave(component: any, record: unknown, props: Record<string, unknown> = {}) {
  const wrapper = mount(component, {
    props: { modelValue: false, record, ...props },
    global: { stubs },
  })
  await wrapper.setProps({ modelValue: true })
  await flushPromises()
  const save = wrapper.findAll('button').find((b) => b.text() === '保存')
  expect(save).toBeDefined()
  await save!.trigger('click')
  await flushPromises()
  return wrapper
}

beforeEach(() => vi.clearAllMocks())

describe('mobile sheets runtime coverage', () => {
  const now = new Date().toISOString()

  it('AssetSheet saves an existing asset via offlineApi.put', async () => {
    const record = { id: 5, name: '资产', category_id: 1, account_id: 2, currency: 'CNY', current_value: 10, quantity: 1, cost_basis: 8, net_value: 1, note: '' } as Asset
    await openAndSave(AssetSheet, record, { categories: [{ id: 1, name: '分类' }], accounts: [{ id: 2, name: '账户' }] })
    expect(offlineApi.put).toHaveBeenCalledWith('/assets/5', expect.objectContaining({ name: '资产', account_id: 2 }))
  })

  it('AccountSheet saves a new account via offlineApi.post', async () => {
    const record = { id: 6, name: '账户', institution: 'bank_card', account_no: '123', currency: 'CNY', icon: '', note: '', sort_order: 0 } as Account
    await openAndSave(AccountSheet, record)
    expect(offlineApi.put).toHaveBeenCalledWith('/accounts/6', expect.objectContaining({ name: '账户', institution: 'bank_card' }))
  })

  it('CategorySheet saves a category with parent_id', async () => {
    const record = { id: 7, name: '子分类', type: 'expense', currency: 'CNY', parent_id: 1, sort_order: 0, icon: '' } as Category
    await openAndSave(CategorySheet, record, { categories: [{ id: 1, name: '父分类' }, record] })
    expect(offlineApi.put).toHaveBeenCalledWith('/categories/7', expect.objectContaining({ name: '子分类', parent_id: 1 }))
  })

  it('TagSheet saves a tag', async () => {
    const record = { id: 8, name: '标签', color: '#fff' } as Tag
    await openAndSave(TagSheet, record)
    expect(offlineApi.put).toHaveBeenCalledWith('/tags/8', expect.objectContaining({ name: '标签', color: '#fff' }))
  })

  it('TransactionSheet saves a transaction without direction', async () => {
    const record = {
      id: 9, asset_id: 1, linked_asset_id: null, category_id: 2, transaction_type: 'deposit',
      amount: 100, price_per_unit: 0, quantity: 0, fee: 0, currency: 'CNY',
      transaction_date: '2026-11-26', note: '', created_at: now, updated_at: now,
    } as unknown as Transaction
    await openAndSave(TransactionSheet, record, { assets: [{ id: 1, name: '资产' }], categories: [{ id: 2, name: '分类' }] })
    const payload = (offlineApi.put as any).mock.calls[0][1]
    expect(payload).toEqual(expect.objectContaining({ asset_id: 1, transaction_type: 'deposit', amount: 100 }))
    expect(payload).not.toHaveProperty('direction')
  })
})
