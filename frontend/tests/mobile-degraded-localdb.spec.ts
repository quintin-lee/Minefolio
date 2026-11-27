import { describe, it, expect, beforeEach, vi } from 'vitest'
import { mount, flushPromises } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import { initLocalDb, resetLocalDb } from '@/db/local'
import { loadDraft, saveDraft, clearDraft } from '@/utils/chat-draft'
import { loadViewCache, saveViewCache } from '@/composables/mobile/useViewCache'
import { useCategoryStore } from '@/stores/category'
import { categoriesApi } from '@/api/categories'
import { tagsApi } from '@/api/tags'
import { offlineApi } from '@/utils/offline-http'
import { reportsApi } from '@/api/reports'
import { marketApi } from '@/api/market'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { dcaApi } from '@/api/dca'
import { cashflowApi } from '@/api/cashflow'
import { assetsApi } from '@/api/assets'
import CategoryManageMobile from '@/views-mobile/CategoryManageMobile.vue'
import ChatMobile from '@/views-mobile/ChatMobile.vue'
import HoldingsMobile from '@/views-mobile/HoldingsMobile.vue'
import ReportsMobile from '@/views-mobile/ReportsMobile.vue'
import PlansMobile from '@/views-mobile/PlansMobile.vue'

vi.mock('@/api/categories', () => ({
  categoriesApi: { list: vi.fn() },
}))

vi.mock('@/api/tags', () => ({
  tagsApi: { list: vi.fn() },
}))

vi.mock('@/utils/offline-http', () => ({
  offlineApi: {
    get: vi.fn(),
    post: vi.fn(),
    put: vi.fn(),
    delete: vi.fn(),
  },
}))

vi.mock('@/api/reports', () => ({
  reportsApi: { holdings: vi.fn() },
}))

vi.mock('@/api/market', () => ({
  marketApi: { getExchangeRates: vi.fn() },
}))

vi.mock('@/api/daily_expenses', () => ({
  dailyExpensesApi: { monthly: vi.fn() },
}))

vi.mock('@/api/dca', () => ({
  dcaApi: {
    listPlans: vi.fn(),
    listPendingExecutions: vi.fn(),
    confirmExecution: vi.fn(),
    skipExecution: vi.fn(),
    setPlanStatus: vi.fn(),
    deletePlan: vi.fn(),
    createPlan: vi.fn(),
  },
}))

vi.mock('@/api/cashflow', () => ({
  cashflowApi: {
    getCalendar: vi.fn(),
    confirmIncome: vi.fn(),
  },
}))

vi.mock('@/api/assets', () => ({
  assetsApi: { list: vi.fn() },
}))

const mockChatStore = {
  sessions: [],
  currentSessionId: null as number | null,
  messages: [],
  isStreaming: true,
  models: [],
  selectedModel: '',
  fetchSessions: vi.fn().mockResolvedValue([]),
  fetchModels: vi.fn().mockResolvedValue([]),
  createNewSession: vi.fn().mockResolvedValue(undefined),
  selectSession: vi.fn().mockResolvedValue(undefined),
  sendMessage: vi.fn().mockResolvedValue(undefined),
  abortCurrentStream: vi.fn(),
}

vi.mock('@/stores/chat', () => ({
  useChatStore: () => mockChatStore,
}))

vi.mock('element-plus', async (importOriginal) => {
  const actual = (await importOriginal()) as any
  return {
    ...actual,
    ElMessage: { success: vi.fn(), warning: vi.fn(), error: vi.fn(), info: vi.fn() },
    ElMessageBox: { confirm: vi.fn().mockResolvedValue('confirm') },
  }
})

const commonStubs = {
  'el-button': { emits: ['click'], template: '<button @click="$emit(\'click\')"><slot /></button>' },
  'el-alert': { props: ['title'], template: '<div class="el-alert-stub">{{ title }}</div>' },
  'el-date-picker': { template: '<div class="date-picker-stub" />' },
  'el-drawer': { template: '<div><slot /></div>' },
  'el-dialog': { template: '<div><slot /><slot name="footer" /></div>' },
  'el-form': { template: '<div><slot /></div>' },
  'el-form-item': { template: '<div><slot /></div>' },
  'el-input': { template: '<input />' },
  'el-input-number': { template: '<input />' },
  'el-select': { template: '<div><slot /></div>' },
  'el-option': { template: '<div />' },
  'el-tag': { template: '<span><slot /></span>' },
  'el-empty': { template: '<div />' },
  ExpenseCategoryPie: { template: '<div class="pie-stub" />' },
  MonthlyChart: { template: '<div class="monthly-chart-stub" />' },
  ChatMessageContent: { template: '<div />' },
  PromptStarters: { template: '<div />' },
  CategorySheet: {
    name: 'CategorySheet',
    props: ['modelValue', 'record', 'categories'],
    emits: ['update:modelValue', 'saved'],
    template: '<div class="category-sheet-stub" />',
  },
  TagSheet: {
    name: 'TagSheet',
    props: ['modelValue', 'record'],
    emits: ['update:modelValue', 'saved'],
    template: '<div class="tag-sheet-stub" />',
  },
}

beforeEach(() => {
  vi.clearAllMocks()
  localStorage.clear()
  localStorage.setItem('minefolio_active_ledger_id', '1')
  resetLocalDb()
  setActivePinia(createPinia())
})

describe('mobile degraded localDb & online mutations', () => {
  it('saveDraft, clearDraft, and saveViewCache do not throw when localDb is uninitialized', () => {
    expect(() => saveDraft('new', '离线草稿')).not.toThrow()
    expect(() => clearDraft('new')).not.toThrow()
    expect(loadDraft('new')).toBe('')

    expect(() => saveViewCache('exchange_rates', { USD: 7.2 })).not.toThrow()
    expect(loadViewCache('exchange_rates')).toEqual({ data: null, updatedAt: null })
  })

  it('CategoryManageMobile loads flattened tree from online API, reflects online delete, and invalidates category store', async () => {
    const store = useCategoryStore()
    const invalidateSpy = vi.spyOn(store, 'invalidate')

    ;(categoriesApi.list as any)
      .mockResolvedValueOnce({
        list: [
          {
            id: 1,
            name: '餐饮',
            type: 'expense',
            sort_order: 1,
            children: [{ id: 2, name: '外卖', type: 'expense', parent_id: 1, sort_order: 1 }],
          },
        ],
        total: 1,
        page: 1,
        page_size: 20,
      })
      .mockResolvedValueOnce({
        list: [{ id: 1, name: '餐饮', type: 'expense', sort_order: 1, children: [] }],
        total: 1,
        page: 1,
        page_size: 20,
      })
      .mockResolvedValueOnce({
        list: [
          { id: 1, name: '餐饮', type: 'expense', sort_order: 1, children: [] },
          { id: 3, name: '交通', type: 'expense', sort_order: 2, children: [] },
        ],
        total: 2,
        page: 1,
        page_size: 20,
      })
    ;(tagsApi.list as any).mockResolvedValue({
      list: [{ id: 10, name: '刚需', color: '#ff0000' }],
      total: 1,
      page: 1,
      page_size: 20,
    })
    ;(offlineApi.delete as any).mockResolvedValue({})

    const wrapper = mount(CategoryManageMobile, {
      global: { stubs: commonStubs },
    })
    await flushPromises()

    expect(wrapper.text()).toContain('餐饮')
    expect(wrapper.text()).toContain('外卖')

    // 删除子分类
    const deleteButtons = wrapper.findAll('button').filter((b) => b.text() === '删除')
    expect(deleteButtons.length).toBe(2)
    await deleteButtons[1]!.trigger('click')
    await flushPromises()

    expect(offlineApi.delete).toHaveBeenCalledWith('/categories/2')
    expect(invalidateSpy).toHaveBeenCalledTimes(1)
    expect(wrapper.text()).toContain('餐饮')
    expect(wrapper.text()).not.toContain('外卖')

    // CategorySheet @saved 同样触发 invalidate 并刷新
    wrapper.findComponent({ name: 'CategorySheet' }).vm.$emit('saved')
    await flushPromises()
    expect(invalidateSpy).toHaveBeenCalledTimes(2)
    expect(wrapper.text()).toContain('交通')
  })

  it('CategoryManageMobile handles online tag delete without throwing when localDb is uninitialized', async () => {
    ;(categoriesApi.list as any).mockResolvedValue({ list: [], total: 0, page: 1, page_size: 20 })
    ;(tagsApi.list as any)
      .mockResolvedValueOnce({ list: [{ id: 10, name: '刚需', color: '#ff0000' }], total: 1, page: 1, page_size: 20 })
      .mockResolvedValueOnce({ list: [], total: 0, page: 1, page_size: 20 })
    ;(offlineApi.delete as any).mockResolvedValue({})

    const wrapper = mount(CategoryManageMobile, {
      global: { stubs: commonStubs },
    })
    await flushPromises()

    const tagTab = wrapper.findAll('button').find((b) => b.text() === '标签')!
    await tagTab.trigger('click')
    await flushPromises()
    expect(wrapper.text()).toContain('刚需')

    const delBtn = wrapper.findAll('button').find((b) => b.text() === '删除')!
    await delBtn.trigger('click')
    await flushPromises()

    expect(offlineApi.delete).toHaveBeenCalledWith('/tags/10')
    expect(wrapper.text()).not.toContain('刚需')
  })

  it('ChatMobile always calls abortCurrentStream on unmount even when localDb is uninitialized', async () => {
    const wrapper = mount(ChatMobile, {
      global: { stubs: commonStubs },
    })
    await flushPromises()
    wrapper.unmount()
    expect(mockChatStore.abortCurrentStream).toHaveBeenCalledTimes(1)
  })

  it('HoldingsMobile completes online fetch when localDb is uninitialized', async () => {
    ;(reportsApi.holdings as any).mockResolvedValue({
      summary: {
        total_market_value: 2000,
        total_cost_basis: 1500,
        total_floating_pnl: 500,
        total_realized_pnl: 0,
        floating_pct: 33.3,
      },
      holdings: [
        {
          asset_id: 1,
          name: '沪深300ETF',
          asset_type: 'fund',
          currency: 'CNY',
          quantity: 100,
          net_value: 20,
          cost_basis: 1500,
          current_value: 2000,
          floating_pnl: 500,
          floating_pct: 33.3,
          realized_pnl: 0,
        },
      ],
    })
    ;(marketApi.getExchangeRates as any).mockResolvedValue({ USD: 7.2 })

    const wrapper = mount(HoldingsMobile, {
      global: { stubs: commonStubs, mocks: { $router: { push: vi.fn() } } },
    })
    await flushPromises()

    expect(reportsApi.holdings).toHaveBeenCalledTimes(1)
    expect(wrapper.text()).toContain('沪深300ETF')
    expect(wrapper.find('.el-alert-stub').exists()).toBe(false)
  })

  it('ReportsMobile completes online fetch when localDb is uninitialized', async () => {
    ;(dailyExpensesApi.monthly as any).mockResolvedValue({
      year: 2026,
      month: 11,
      total_income: 5000,
      total_expense: 1200,
      balance: 3800,
      by_category: [],
      by_tag: [],
      daily_breakdown: [],
    })

    const wrapper = mount(ReportsMobile, {
      global: { stubs: commonStubs },
    })
    await flushPromises()

    expect(dailyExpensesApi.monthly).toHaveBeenCalledTimes(1)
    expect(wrapper.find('.el-alert-stub').exists()).toBe(false)
    expect(wrapper.text()).not.toContain('离线报表')
  })

  it('PlansMobile isolates view_cache keys by ledgerId', async () => {
    await initLocalDb()

    // 账本 1 写入缓存并模拟切到账本 2 离线/失败场景
    localStorage.setItem('minefolio_active_ledger_id', '1')
    ;(dcaApi.listPlans as any).mockResolvedValueOnce([{ id: 1, name: '账本1定投', amount: 500, frequency: 'monthly', status: 'active' }])
    ;(dcaApi.listPendingExecutions as any).mockResolvedValueOnce([])
    ;(cashflowApi.getCalendar as any).mockResolvedValueOnce({
      year: 2026,
      month: 11,
      total_expected_income: 1000,
      total_expected_expense: 0,
      total_confirmed_income: 0,
      events: [],
    })
    ;(assetsApi.list as any).mockResolvedValueOnce({ list: [] })

    const w1 = mount(PlansMobile, { global: { stubs: commonStubs } })
    await flushPromises()
    expect(w1.text()).toContain('账本1定投')
    expect(loadViewCache('dca_plans:1').data).toHaveLength(1)
    w1.unmount()

    // 切换到账本 2，远端请求失败时不应读到账本 1 的计划
    localStorage.setItem('minefolio_active_ledger_id', '2')
    ;(dcaApi.listPlans as any).mockRejectedValueOnce(new Error('offline'))
    ;(dcaApi.listPendingExecutions as any).mockRejectedValueOnce(new Error('offline'))
    ;(cashflowApi.getCalendar as any).mockRejectedValueOnce(new Error('offline'))
    ;(assetsApi.list as any).mockRejectedValueOnce(new Error('offline'))

    const w2 = mount(PlansMobile, { global: { stubs: commonStubs } })
    await flushPromises()
    expect(w2.text()).not.toContain('账本1定投')
    expect(loadViewCache('dca_plans:2').data).toBeNull()
  })
})
