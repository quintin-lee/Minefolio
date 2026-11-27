import { describe, it, expect, vi, beforeEach } from 'vitest'
import { mount, flushPromises } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'
import { amountClass, positiveAmountClass } from '@/utils/format'
import SummaryCard from '@/components/SummaryCard.vue'
import Reports from '@/views/Reports.vue'
import Transactions from '@/views/Transactions.vue'

const reportMocks = vi.hoisted(() => ({
  expenseMonthly: vi.fn(),
  expenseTrend: vi.fn(),
  assetTrend: vi.fn(),
  assetBreakdown: vi.fn(),
  expenseTag: vi.fn(),
  transactionPerformance: vi.fn(),
  fxPnl: vi.fn(),
}))

vi.mock('@/api/reports', () => ({
  reportsApi: {
    expenseMonthly: reportMocks.expenseMonthly,
    expenseTrend: reportMocks.expenseTrend,
    assetTrend: reportMocks.assetTrend,
    assetBreakdown: reportMocks.assetBreakdown,
    expenseTag: reportMocks.expenseTag,
    transactionPerformance: reportMocks.transactionPerformance,
    fxPnl: reportMocks.fxPnl,
  },
}))

vi.mock('@/api/transactions', () => ({
  transactionsApi: {
    list: vi.fn().mockResolvedValue({ list: [], total: 0 }),
    monthly: vi.fn().mockResolvedValue({ total_volume: 0, inflows: 0, outflows: 0, count: 0 }),
  },
}))

vi.mock('@/api/assets', () => ({
  assetsApi: {
    list: vi.fn().mockResolvedValue({ list: [], total: 0 }),
  },
}))

vi.mock('@/stores/category', () => ({
  useCategoryStore: () => ({
    transactionCategories: [],
    loadCategories: vi.fn().mockResolvedValue(undefined),
  }),
}))

function readSrc(relPath: string): string {
  return readFileSync(resolve(process.cwd(), relPath), 'utf8')
}

describe('financial semantic tokens and zero-value handling', () => {
  it('returns zero-text for zero, null, and undefined amounts', () => {
    expect(amountClass(0)).toBe('zero-text')
    expect(amountClass(-0)).toBe('zero-text')
    expect(amountClass(null)).toBe('zero-text')
    expect(amountClass(undefined)).toBe('zero-text')
    expect(amountClass(100)).toBe('income-text')
    expect(amountClass(-50)).toBe('expense-text')
    expect(amountClass(100, 'gain-text', 'loss-text')).toBe('gain-text')
    expect(amountClass(-50, 'gain-text', 'loss-text')).toBe('loss-text')

    expect(positiveAmountClass(0, 'asset-text')).toBe('zero-text')
    expect(positiveAmountClass(null, 'liability-text')).toBe('zero-text')
    expect(positiveAmountClass(1200, 'asset-text')).toBe('asset-text')
  })

  it('defines centralized financial semantic tokens and opaque sticky table tokens in index.css', () => {
    const css = readSrc('src/styles/index.css')
    expect(css).toContain('--mf-income:')
    expect(css).toContain('--mf-expense:')
    expect(css).toContain('--mf-asset:')
    expect(css).toContain('--mf-liability:')
    expect(css).toContain('--mf-amount-zero:')
    expect(css).toContain('--mf-table-sticky-bg:')
    expect(css).toContain('--mf-table-sticky-header-bg:')
    expect(css).toContain('.el-table th.el-table-fixed-column--right')
  })

  it('renders SummaryCard with neutral-value for neutral type and semantic classes for asset/liability', () => {
    const neutralCard = mount(SummaryCard, { props: { label: '总资产', value: '¥0.00', type: 'neutral' } })
    expect(neutralCard.find('.summary-value').classes()).toContain('neutral-value')

    const assetCard = mount(SummaryCard, { props: { label: '总资产', value: '¥100.00', type: 'asset' } })
    expect(assetCard.find('.summary-value').classes()).toContain('asset-text')

    const liabilityCard = mount(SummaryCard, { props: { label: '总负债', value: '¥50.00', type: 'liability' } })
    expect(liabilityCard.find('.summary-value').classes()).toContain('liability-text')
  })
})

describe('Reports.vue empty state detection for zero-filled backend payloads', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
  })

  it('shows empty states across tabs when backend returns zero-filled objects', async () => {
    reportMocks.expenseMonthly.mockResolvedValue({
      year: 2026,
      month: 11,
      total_income: 0,
      total_expense: 0,
      balance: 0,
      by_category: [],
      by_tag: [],
      daily_breakdown: [],
    })
    reportMocks.expenseTrend.mockResolvedValue({ labels: [], income: [], expense: [] })
    reportMocks.assetTrend.mockResolvedValue({
      period: '30d',
      labels: ['2026-10-28', '2026-11-27'],
      net_worth: [0, 0],
      assets: [0, 0],
      liabilities: [0, 0],
    })
    reportMocks.assetBreakdown.mockResolvedValue({
      assets: [],
      liabilities: [],
      total_assets: 0,
      total_liabilities: 0,
      net_worth: 0,
    })
    reportMocks.expenseTag.mockResolvedValue({ period: '2026-11', items: [] })
    reportMocks.transactionPerformance.mockResolvedValue({
      total_trades: 0,
      total_gain: 0,
      total_loss: 0,
      net_gain: 0,
      trades: [],
    })
    reportMocks.fxPnl.mockResolvedValue({
      total_foreign_market_base: 0,
      total_foreign_cost_base: 0,
      total_asset_pnl_base: 0,
      total_fx_pnl_base: 0,
      total_combined_pnl_base: 0,
      assets: [],
    })

    const wrapper = mount(Reports, {
      global: {
        directives: { loading: () => {} },
        stubs: {
          ExpenseCategoryPie: { template: '<div class="stub-pie" />' },
          ExpenseTrendBar: { template: '<div class="stub-trend" />' },
          AssetTrendLine: { template: '<div class="stub-asset-trend" />' },
          AssetBreakdownPie: { template: '<div class="stub-asset-pie" />' },
          'el-tabs': { template: '<div><slot /></div>' },
          'el-tab-pane': { template: '<div><slot /></div>' },
          'el-row': { template: '<div><slot /></div>' },
          'el-col': { template: '<div><slot /></div>' },
          'el-button': { template: '<button><slot /></button>' },
          'el-icon': { template: '<i />' },
          'el-date-picker': { template: '<input />' },
          'el-radio-group': { template: '<div><slot /></div>' },
          'el-radio-button': { template: '<button><slot /></button>' },
          'el-divider': { template: '<hr />' },
          'el-table': { template: '<div><slot /></div>' },
          'el-table-column': { template: '<div />' },
          'el-tag': { template: '<span />' },
          'el-progress': { template: '<div />' },
        },
      },
    })
    await flushPromises()

    const text = wrapper.text()
    expect(text).toContain('暂无收支数据，完成第一笔交易后这里会出现图表')
    expect(text).toContain('暂无资产数据，先添加资产账户')
    expect(text).toContain('暂无外币资产')
    expect(wrapper.find('.stub-trend').exists()).toBe(false)
    expect(wrapper.find('.stub-asset-trend').exists()).toBe(false)
  })

  it('renders charts when real financial data exists', async () => {
    reportMocks.expenseMonthly.mockResolvedValue({
      year: 2026,
      month: 11,
      total_income: 10000,
      total_expense: 2500,
      balance: 7500,
      by_category: [{ name: '餐饮', expense_type: 'expense', amount: 2500, pct: 100 }],
      by_tag: [],
      daily_breakdown: [],
    })
    reportMocks.expenseTrend.mockResolvedValue({ labels: ['2026-11'], income: [10000], expense: [2500] })
    reportMocks.assetTrend.mockResolvedValue({
      period: '30d',
      labels: ['2026-10-28', '2026-11-27'],
      net_worth: [50000, 57500],
      assets: [50000, 57500],
      liabilities: [0, 0],
    })
    reportMocks.assetBreakdown.mockResolvedValue({
      assets: [{ name: '银行卡', value: 57500, pct: 100 }],
      liabilities: [],
      total_assets: 57500,
      total_liabilities: 0,
      net_worth: 57500,
    })
    reportMocks.expenseTag.mockResolvedValue({ period: '2026-11', items: [] })
    reportMocks.transactionPerformance.mockResolvedValue({
      total_trades: 1,
      total_gain: 500,
      total_loss: 0,
      net_gain: 500,
      trades: [],
    })
    reportMocks.fxPnl.mockResolvedValue({
      total_foreign_market_base: 0,
      total_foreign_cost_base: 0,
      total_asset_pnl_base: 0,
      total_fx_pnl_base: 0,
      total_combined_pnl_base: 0,
      assets: [],
    })

    const wrapper = mount(Reports, {
      global: {
        directives: { loading: () => {} },
        stubs: {
          ExpenseCategoryPie: { template: '<div class="stub-pie" />' },
          ExpenseTrendBar: { template: '<div class="stub-trend" />' },
          AssetTrendLine: { template: '<div class="stub-asset-trend" />' },
          AssetBreakdownPie: { template: '<div class="stub-asset-pie" />' },
          'el-tabs': { template: '<div><slot /></div>' },
          'el-tab-pane': { template: '<div><slot /></div>' },
          'el-row': { template: '<div><slot /></div>' },
          'el-col': { template: '<div><slot /></div>' },
          'el-button': { template: '<button><slot /></button>' },
          'el-icon': { template: '<i />' },
          'el-date-picker': { template: '<input />' },
          'el-radio-group': { template: '<div><slot /></div>' },
          'el-radio-button': { template: '<button><slot /></button>' },
          'el-divider': { template: '<hr />' },
          'el-table': { template: '<div><slot /></div>' },
          'el-table-column': { template: '<div />' },
          'el-tag': { template: '<span />' },
          'el-progress': { template: '<div />' },
        },
      },
    })
    await flushPromises()

    expect(wrapper.find('.stub-pie').exists()).toBe(true)
    expect(wrapper.find('.stub-trend').exists()).toBe(true)
    expect(wrapper.find('.stub-asset-trend').exists()).toBe(true)
    expect(wrapper.find('.stub-asset-pie').exists()).toBe(true)
  })
})

describe('Transactions.vue compact narrow-screen columns and neutral zero cards', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
  })

  it('hides low-priority columns at <=1200px, cleans up media listener, and renders neutral zero cards', async () => {
    const addListener = vi.fn()
    const removeListener = vi.fn()
    const origMatchMedia = window.matchMedia
    window.matchMedia = vi.fn().mockImplementation((query: string) => ({
      matches: query.includes('1200px'),
      media: query,
      addEventListener: addListener,
      removeEventListener: removeListener,
    })) as any

    try {
      const wrapper = mount(Transactions, {
        global: {
          directives: { loading: () => {} },
          stubs: {
            SummaryCard: {
              props: ['label', 'value', 'type'],
              template: '<div class="stub-summary" :data-label="label" :data-type="type">{{ value }}</div>',
            },
            'el-row': { template: '<div><slot /></div>' },
            'el-col': { template: '<div><slot /></div>' },
            'el-form': { template: '<form><slot /></form>' },
            'el-form-item': { template: '<div><slot /></div>' },
            'el-select': { template: '<select><slot /></select>' },
            'el-option': { template: '<option />' },
            'el-input': { template: '<input />' },
            'el-input-number': { template: '<input />' },
            'el-date-picker': { template: '<input />' },
            'el-cascader': { template: '<input />' },
            'el-button': { template: '<button><slot /></button>' },
            'el-table': { template: '<div><slot /></div>' },
            'el-table-column': { props: ['label'], template: '<div class="stub-col" :data-col="label" />' },
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

      const colLabels = wrapper.findAll('.stub-col').map((el) => el.attributes('data-col'))
      expect(colLabels).not.toContain('手续费')
      expect(colLabels).not.toContain('币种')
      expect(colLabels).not.toContain('备注')
      expect(colLabels).toContain('交易分类')
      expect(colLabels).toContain('操作')

      const cards = wrapper.findAll('.stub-summary')
      expect(cards.map((c) => c.attributes('data-type'))).toEqual([
        'neutral',
        'neutral',
        'neutral',
        'neutral',
      ])

      wrapper.unmount()
      expect(addListener).toHaveBeenCalledTimes(1)
      expect(removeListener).toHaveBeenCalledTimes(1)
    } finally {
      window.matchMedia = origMatchMedia
    }
  })
})
