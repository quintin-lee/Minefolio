import { describe, it, expect, vi, beforeEach } from 'vitest'
import { mount, flushPromises } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'

const mocks = vi.hoisted(() => ({
  assetsList: vi.fn(),
  accountsList: vi.fn(),
  summaryGet: vi.fn(),
  marketGetExchangeRates: vi.fn(),
  routeQuery: {} as Record<string, string>,
}))

vi.mock('vue-router', () => ({
  useRouter: () => ({
    push: vi.fn().mockResolvedValue(undefined),
    replace: vi.fn().mockResolvedValue(undefined),
  }),
  useRoute: () => ({
    query: mocks.routeQuery,
  }),
}))

vi.mock('@/api/assets', () => ({
  assetsApi: {
    list: mocks.assetsList,
    syncAllAsync: vi.fn(),
    syncOne: vi.fn(),
  },
}))

vi.mock('@/api/accounts', () => ({
  accountsApi: {
    list: mocks.accountsList,
  },
}))

vi.mock('@/api/summary', () => ({
  summaryApi: {
    get: mocks.summaryGet,
  },
}))

vi.mock('@/api/market', () => ({
  marketApi: {
    search: vi.fn(),
    getHistory: vi.fn(),
    getExchangeRates: mocks.marketGetExchangeRates,
  },
}))

vi.mock('@/stores/category', () => ({
  useCategoryStore: () => ({
    loadCategories: vi.fn().mockResolvedValue(undefined),
    assetCategories: [],
  }),
}))

vi.mock('@/stores/ledger', () => ({
  useLedgerStore: () => ({
    currentLedgerId: 1,
  }),
}))

vi.mock('@/composables/useAsyncTaskProgress', () => ({
  useAsyncTaskProgress: () => ({
    running: { value: false },
    progress: { value: 0 },
    start: vi.fn(),
  }),
}))

vi.mock('@/db/local', () => ({
  query: vi.fn(() => []),
  rowsFrom: vi.fn(() => []),
}))

vi.mock('@/utils/offline-http', () => ({
  offlineApi: {
    delete: vi.fn(),
  },
}))

vi.mock('@/utils/locale', () => ({
  t: (key: string) => key,
}))

import Assets from '@/views/Assets.vue'
import AssetsMobile from '@/views-mobile/AssetsMobile.vue'

const stubs = {
  'el-button': {
    emits: ['click'],
    template: '<button @click="$emit(\'click\', $event)"><slot /></button>',
  },
  'el-icon': { template: '<i><slot /></i>' },
  'el-tag': { template: '<span><slot /></span>' },
  'el-tooltip': { template: '<span><slot /></span>' },
  'el-empty': { template: '<div><slot /></div>' },
  'el-dialog': { template: '<div><slot /><slot name="title" /><slot name="footer" /></div>' },
  'el-form': { template: '<form><slot /></form>' },
  'el-form-item': { template: '<div><slot /></div>' },
  'el-input': { template: '<input />' },
  'el-input-number': { template: '<input />' },
  'el-select': { template: '<select><slot /></select>' },
  'el-option': { template: '<option />' },
  'el-cascader': { template: '<div />' },
  'el-radio-group': { template: '<div><slot /></div>' },
  'el-radio-button': { template: '<button><slot /></button>' },
  'el-progress': { template: '<div />' },
  Icon: { template: '<span />' },
  SymbolSelect: { template: '<div />' },
  PriceHistoryChart: { template: '<div />' },
  AccountDialog: { template: '<div />' },
  AssetSheet: { template: '<div />' },
  AccountSheet: { template: '<div />' },
}

const sampleAccounts = [
  { id: 1, name: '默认账户', institution: 'cash_wallet', archived: 0, sort_order: 0 },
  { id: 2, name: '证券账户', institution: 'broker', archived: 0, sort_order: 1 },
]

const sampleAssets = [
  { id: 101, name: '现金资产', asset_type: 'cash', account_id: 1, current_value: 5000, currency: 'CNY', category_name: '现金' },
  { id: 102, name: '沪深300ETF', asset_type: 'fund', account_id: 2, current_value: 12000, currency: 'CNY', category_name: '基金' },
]

describe('Assets pages default collapsed accounts', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    mocks.routeQuery = {}
    mocks.accountsList.mockResolvedValue({
      list: sampleAccounts,
      total: sampleAccounts.length,
      page: 1,
      page_size: 20,
    })
    mocks.assetsList.mockResolvedValue({ list: sampleAssets, total: sampleAssets.length })
    mocks.summaryGet.mockResolvedValue({
      total_assets: 17000,
      total_liabilities: 0,
      net_worth: 17000,
      category_breakdown: [],
      trend: [],
    })
    mocks.marketGetExchangeRates.mockResolvedValue({})
  })

  it('Assets.vue collapses all account groups by default and toggles individually on click', async () => {
    const wrapper = mount(Assets, {
      global: {
        stubs,
        directives: { loading: () => {} },
      },
    })
    await flushPromises()

    const headers = wrapper.findAll('.account-group-header')
    expect(headers).toHaveLength(2)
    expect(wrapper.findAll('.account-group-body')).toHaveLength(0)

    await headers[0].trigger('click')
    expect(wrapper.findAll('.account-group-body')).toHaveLength(1)
    expect(wrapper.findAll('.asset-row')).toHaveLength(1)
    expect(wrapper.text()).toContain('现金资产')
    expect(wrapper.text()).not.toContain('沪深300ETF')

    await headers[0].trigger('click')
    expect(wrapper.findAll('.account-group-body')).toHaveLength(0)
  })

  it('Assets.vue auto-expands target account group for asset_id deep link while keeping others collapsed', async () => {
    mocks.routeQuery = { asset_id: '102' }

    const wrapper = mount(Assets, {
      global: {
        stubs,
        directives: { loading: () => {} },
      },
    })
    await flushPromises()

    expect(wrapper.findAll('.account-group-body')).toHaveLength(1)
    expect(wrapper.text()).toContain('沪深300ETF')
    expect(wrapper.text()).not.toContain('现金资产')
    wrapper.unmount()
  })

  it('Assets.vue auto-expands target account group for account_id deep link while keeping others collapsed', async () => {
    mocks.routeQuery = { account_id: '1' }

    const wrapper = mount(Assets, {
      global: {
        stubs,
        directives: { loading: () => {} },
      },
    })
    await flushPromises()

    expect(wrapper.findAll('.account-group-body')).toHaveLength(1)
    expect(wrapper.text()).toContain('现金资产')
    expect(wrapper.text()).not.toContain('沪深300ETF')
    wrapper.unmount()
  })

  it('AssetsMobile.vue collapses all account groups by default and toggles on click', async () => {
    const wrapper = mount(AssetsMobile, {
      global: {
        stubs,
        mocks: { $router: { push: vi.fn() } },
        directives: { loading: () => {} },
      },
    })
    await flushPromises()

    const headers = wrapper.findAll('.account-header')
    expect(headers).toHaveLength(2)
    expect(wrapper.findAll('.account-body')).toHaveLength(0)
    expect(wrapper.findAll('.chevron.collapsed')).toHaveLength(2)

    await headers[1].trigger('click')
    expect(wrapper.findAll('.account-body')).toHaveLength(1)
    expect(wrapper.findAll('.chevron.collapsed')).toHaveLength(1)
    expect(wrapper.text()).toContain('沪深300ETF')
    expect(wrapper.text()).not.toContain('现金资产')

    await headers[1].trigger('click')
    expect(wrapper.findAll('.account-body')).toHaveLength(0)
    expect(wrapper.findAll('.chevron.collapsed')).toHaveLength(2)
  })
})
