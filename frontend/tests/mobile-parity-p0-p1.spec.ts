import { describe, it, expect, beforeEach, vi } from 'vitest'
import { mount, flushPromises } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import { ElMessageBox } from 'element-plus'
import { initLocalDb, resetLocalDb, run } from '@/db/local'

vi.mock('@/views-mobile/MobileLayout.vue', () => ({ default: { template: '<router-view />' } }))
vi.mock('@/views-mobile/DashboardMobile.vue', () => ({ default: { template: '<div>dashboard</div>' } }))
vi.mock('@/views-mobile/LoginMobile.vue', () => ({ default: { template: '<div>login</div>' } }))
vi.mock('@/views/AssetDetail.vue', () => ({ default: { template: '<div>asset-detail</div>' } }))
vi.mock('@/views/Market.vue', () => ({ default: { template: '<div>market</div>' } }))
vi.mock('@/views/NewsPulse.vue', () => ({ default: { template: '<div>news</div>' } }))
vi.mock('@/views/Layout.vue', () => ({ default: { template: '<router-view />' } }))

vi.mock('@/api/system', () => ({
  systemApi: {
    status: vi.fn().mockResolvedValue({ initialized: true, user_count: 1 }),
  },
}))

vi.mock('@/api/daily_expenses', () => ({
  dailyExpensesApi: {
    list: vi.fn(),
    monthly: vi.fn(),
  },
}))

vi.mock('@/api/transactions', () => ({
  transactionsApi: {
    list: vi.fn(),
    monthly: vi.fn(),
  },
}))

vi.mock('@/api/tags', () => ({
  tagsApi: { list: vi.fn().mockResolvedValue({ list: [], total: 0 }) },
}))

vi.mock('@/api/assets', () => ({
  assetsApi: { list: vi.fn().mockResolvedValue({ list: [], total: 0 }) },
}))

vi.mock('@/api/reports', () => ({
  reportsApi: {
    holdings: vi.fn(),
    expenseTrend: vi.fn(),
    expenseYearly: vi.fn(),
    assetTrend: vi.fn(),
    assetBreakdown: vi.fn(),
  },
}))

vi.mock('@/api/market', () => ({
  marketApi: { getExchangeRates: vi.fn().mockResolvedValue({ USD: 7.2 }) },
}))

const mockChatStore = {
  sessions: [{ id: 1, title: '测试会话' }],
  currentSessionId: 1 as number | null,
  messages: [
    {
      id: 11,
      role: 'assistant',
      content: '{"draft_token":"tok-1","status":"pending"}',
    },
  ] as any[],
  isStreaming: false,
  models: [],
  selectedModel: '',
  workflows: [{ id: 'wf_monthly_review', title: '月度复盘', description: 'desc' }],
  pinnedWorkflowIds: [],
  enableThinking: false,
  enableTypewriterBuffer: false,
  fetchSessions: vi.fn().mockResolvedValue([]),
  fetchModels: vi.fn().mockResolvedValue([]),
  fetchWorkflowsList: vi.fn().mockResolvedValue([]),
  createNewSession: vi.fn().mockResolvedValue(undefined),
  selectSession: vi.fn().mockResolvedValue(undefined),
  sendMessage: vi.fn().mockResolvedValue(undefined),
  abortCurrentStream: vi.fn(),
  toggleThinking: vi.fn(),
  renameSession: vi.fn().mockResolvedValue(undefined),
  deleteSessionById: vi.fn().mockResolvedValue(undefined),
  regenerateLastMessage: vi.fn().mockResolvedValue(undefined),
}

vi.mock('@/stores/chat', () => ({
  useChatStore: () => mockChatStore,
}))

import { useAuthStore } from '@/stores/auth'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { transactionsApi } from '@/api/transactions'
import { reportsApi } from '@/api/reports'
import DailyExpensesMobile from '@/views-mobile/DailyExpensesMobile.vue'
import TransactionsMobile from '@/views-mobile/TransactionsMobile.vue'
import HoldingsMobile from '@/views-mobile/HoldingsMobile.vue'
import ReportsMobile from '@/views-mobile/ReportsMobile.vue'
import ChatMobile from '@/views-mobile/ChatMobile.vue'

const stubs = {
  Icon: true,
  'el-button': { emits: ['click'], template: '<button @click="$emit(\'click\')"><slot /></button>' },
  'el-date-picker': { props: ['modelValue'], emits: ['update:modelValue', 'change'], template: '<div class="date-picker-stub" />' },
  'el-select': { props: ['modelValue'], template: '<div><slot /></div>' },
  'el-option': { template: '<div />' },
  'el-input': { template: '<input />' },
  'el-tag': { template: '<span><slot /></span>' },
  'el-empty': { template: '<div class="empty-stub" />' },
  'el-alert': { props: ['title'], template: '<div class="alert-stub">{{ title }}</div>' },
  'el-drawer': { props: ['modelValue'], template: '<div><slot /></div>' },
  ExpenseQuickSheet: { template: '<div />' },
  ReceiptScannerModal: { template: '<div />' },
  TransactionSheet: { template: '<div />' },
  ExpenseCategoryPie: { template: '<div class="pie-stub" />' },
  MonthlyChart: { template: '<div class="monthly-stub" />' },
  ExpenseTrendBar: { template: '<div class="trend-bar-stub" />' },
  YearlyChart: { template: '<div class="yearly-stub" />' },
  AssetTrendLine: { template: '<div class="asset-trend-stub" />' },
  AssetBreakdownPie: { template: '<div class="asset-pie-stub" />' },
  HoldingsTypePie: { template: '<div class="holdings-pie-stub" />' },
  HoldingsCostBar: { template: '<div class="holdings-bar-stub" />' },
  ConcentrationBadge: { props: ['hhi', 'maxHoldingWeight'], template: '<div class="hhi-stub">{{ hhi.toFixed(2) }}</div>' },
  PromptStarters: { template: '<div />' },
  WorkflowBar: { template: '<div class="workflow-bar-stub" />' },
  ChatMessageContent: {
    props: ['messageId', 'content', 'workflowData', 'workflowConfig', 'enableBuffer'],
    emits: ['action-executed'],
    template: '<div class="chat-msg-stub" @click="$emit(\'action-executed\', { draftToken: \'tok-1\' })" />',
  },
}

describe('Mobile P0+P1 Parity', () => {
  beforeEach(async () => {
    vi.clearAllMocks()
    localStorage.clear()
    localStorage.setItem('minefolio_active_ledger_id', '1')
    setActivePinia(createPinia())
    resetLocalDb()
    await initLocalDb()
  })

  it('resolves new mobile routes (/m/assets/:id, /m/market, /m/news) and desktop redirects', async () => {
    const auth = useAuthStore()
    auth.token = 'valid-token'
    auth.isInitialized = true

    const mobileRouter = (await import('@/router/mobile')).default
    for (const target of ['/m/assets/42', '/m/market', '/m/news']) {
      await mobileRouter.push(target)
      await mobileRouter.isReady()
      expect(mobileRouter.currentRoute.value.path).toBe(target)
    }

    const desktopRouter = (await import('@/router/index')).default
    await desktopRouter.push('/m/assets/42')
    await desktopRouter.isReady()
    expect(desktopRouter.currentRoute.value.path).toBe('/assets/42')

    await desktopRouter.push('/m/market')
    expect(desktopRouter.currentRoute.value.path).toBe('/market')

    await desktopRouter.push('/m/news')
    expect(desktopRouter.currentRoute.value.path).toBe('/news')
  })

  it('ChatMobile renders WorkflowBar, handles action-executed, thinking toggle, and session rename/delete', async () => {
    vi.spyOn(ElMessageBox, 'prompt').mockResolvedValue({ value: '新标题', action: 'confirm' } as any)
    vi.spyOn(ElMessageBox, 'confirm').mockResolvedValue('confirm' as any)

    const wrapper = mount(ChatMobile, {
      global: { stubs },
    })
    await flushPromises()

    expect(mockChatStore.fetchWorkflowsList).toHaveBeenCalledTimes(1)
    expect(wrapper.find('.workflow-bar-stub').exists()).toBe(true)

    await wrapper.find('.chat-msg-stub').trigger('click')
    expect(mockChatStore.messages[0].content).toContain('"status": "executed"')

    await wrapper.find('button[aria-label="切换深度思考"]').trigger('click')
    expect(mockChatStore.toggleThinking).toHaveBeenCalledTimes(1)

    await wrapper.find('button[aria-label="重命名会话"]').trigger('click')
    await flushPromises()
    expect(mockChatStore.renameSession).toHaveBeenCalledWith(1, '新标题')

    await wrapper.find('button[aria-label="删除会话"]').trigger('click')
    await flushPromises()
    expect(mockChatStore.deleteSessionById).toHaveBeenCalledWith(1)
  })

  it('DailyExpensesMobile filters both online list and offline local SQLite', async () => {
    ;(dailyExpensesApi.list as any).mockResolvedValue({ list: [], total: 0, page: 1, page_size: 20 })
    ;(dailyExpensesApi.monthly as any).mockResolvedValue({ total_income: 100, total_expense: 40, balance: 60, by_category: [], by_tag: [], daily_breakdown: [] })

    const wrapper = mount(DailyExpensesMobile, {
      global: { stubs, mocks: { $router: { push: vi.fn() } } },
    })
    await flushPromises()

    const expenseChip = wrapper.findAll('.filter-chip').find((b) => b.text() === '支出')!
    await expenseChip.trigger('click')
    await flushPromises()

    expect(dailyExpensesApi.list).toHaveBeenLastCalledWith(expect.objectContaining({ expense_type: 'expense' }))

    // Offline fallback SQLite filtering
    run('INSERT INTO categories (id, name, type, currency, ledger_id) VALUES (1, "餐饮", "expense", "CNY", 1)')
    run('INSERT INTO categories (id, name, type, currency, ledger_id) VALUES (2, "工资", "income", "CNY", 1)')
    run('INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date, note, ledger_id) VALUES (10, 1, 1, "expense", 35, "CNY", "2026-11-10", "午饭", 1)')
    run('INSERT INTO daily_expenses (id, category_id, asset_id, expense_type, amount, currency, expense_date, note, ledger_id) VALUES (11, 2, 1, "income", 5000, "CNY", "2026-11-11", "月薪", 1)')

    ;(dailyExpensesApi.list as any).mockRejectedValueOnce(new Error('offline'))
    ;(dailyExpensesApi.monthly as any).mockRejectedValueOnce(new Error('offline'))

    await expenseChip.trigger('click')
    await flushPromises()
    expect(wrapper.text()).toContain('午饭')
    expect(wrapper.text()).not.toContain('月薪')
  })

  it('TransactionsMobile filters online API and offline SQLite', async () => {
    ;(transactionsApi.list as any).mockResolvedValue({ list: [], total: 0, page: 1, page_size: 30 })
    ;(transactionsApi.monthly as any).mockResolvedValue({ total_volume: 1000, inflows: 200, outflows: 800, count: 2 })

    const wrapper = mount(TransactionsMobile, {
      global: { stubs, mocks: { $router: { push: vi.fn() } } },
    })
    await flushPromises()

    const buyChip = wrapper.findAll('.filter-chip').find((b) => b.text() === '买入')!
    await buyChip.trigger('click')
    await flushPromises()
    expect(transactionsApi.list).toHaveBeenLastCalledWith(expect.objectContaining({ type: 'buy' }))

    run('INSERT INTO categories (id, name, type, currency, ledger_id) VALUES (10, "股票", "asset", "CNY", 1)')
    run('INSERT INTO assets (id, category_id, name, currency, current_value, ledger_id) VALUES (5, 10, "沪深300", "CNY", 1000, 1)')
    run('INSERT INTO transactions (id, asset_id, category_id, transaction_type, direction, amount, currency, transaction_date, ledger_id) VALUES (100, 5, 10, "buy", "out", 800, "CNY", "2026-11-15", 1)')
    run('INSERT INTO transactions (id, asset_id, category_id, transaction_type, direction, amount, currency, transaction_date, ledger_id) VALUES (101, 5, 10, "sell", "in", 300, "CNY", "2026-11-16", 1)')

    ;(transactionsApi.list as any).mockRejectedValueOnce(new Error('offline'))
    await buyChip.trigger('click')
    await flushPromises()
    expect(wrapper.text()).toContain('800.00')
    expect(wrapper.text()).not.toContain('300.00')
  })

  it('HoldingsMobile computes local HHI concentration, toggles charts, and navigates to asset detail', async () => {
    ;(reportsApi.holdings as any).mockResolvedValue({
      summary: { total_market_value: 2000, total_cost_basis: 1500, total_floating_pnl: 500, total_realized_pnl: 0, floating_pct: 33.3 },
      holdings: [
        { asset_id: 7, name: '标普500', asset_type: 'fund', currency: 'CNY', quantity: 10, net_value: 200, cost_basis: 1500, current_value: 2000, floating_pnl: 500, floating_pct: 33.3, realized_pnl: 0 },
      ],
    })
    const push = vi.fn()
    const wrapper = mount(HoldingsMobile, {
      global: { stubs, mocks: { $router: { push } } },
    })
    await flushPromises()

    expect(wrapper.find('.hhi-stub').text()).toBe('1.00')
    expect(wrapper.find('.holdings-pie-stub').exists()).toBe(true)

    const barBtn = wrapper.findAll('.toggle-pill').find((b) => b.text() === '成本对比')!
    await barBtn.trigger('click')
    expect(wrapper.find('.holdings-bar-stub').exists()).toBe(true)

    await wrapper.find('.holding-card').trigger('click')
    expect(push).toHaveBeenCalledWith('/m/assets/7')
  })

  it('ReportsMobile lazy-loads trend, yearly, and asset tabs when switched', async () => {
    ;(dailyExpensesApi.monthly as any).mockResolvedValue({
      year: 2026, month: 11, total_income: 5000, total_expense: 1000, balance: 4000,
      by_category: [], by_tag: [], daily_breakdown: [],
    })
    ;(reportsApi.expenseTrend as any).mockResolvedValue({ labels: ['2026-10', '2026-11'], income: [4000, 5000], expense: [900, 1000] })
    ;(reportsApi.expenseYearly as any).mockResolvedValue({ year: 2026, labels: ['1月'], income: [5000], expense: [1000] })
    ;(reportsApi.assetTrend as any).mockResolvedValue({ period: '30d', labels: ['11-01', '11-15'], net_worth: [10000, 12000], assets: [10000, 12000], liabilities: [0, 0] })
    ;(reportsApi.assetBreakdown as any).mockResolvedValue({ total_assets: 12000, total_liabilities: 0, net_worth: 12000, assets: [{ name: '现金', value: 12000, pct: 100 }], liabilities: [] })

    const wrapper = mount(ReportsMobile, { global: { stubs } })
    await flushPromises()

    expect(reportsApi.expenseTrend).not.toHaveBeenCalled()

    await wrapper.findAll('.report-tab-btn')[1]!.trigger('click')
    await flushPromises()
    expect(reportsApi.expenseTrend).toHaveBeenCalledWith(6)
    expect(wrapper.find('.trend-bar-stub').exists()).toBe(true)

    await wrapper.findAll('.report-tab-btn')[2]!.trigger('click')
    await flushPromises()
    expect(reportsApi.expenseYearly).toHaveBeenCalledTimes(1)
    expect(wrapper.find('.yearly-stub').exists()).toBe(true)

    await wrapper.findAll('.report-tab-btn')[3]!.trigger('click')
    await flushPromises()
    expect(reportsApi.assetTrend).toHaveBeenCalledWith('30d')
    expect(reportsApi.assetBreakdown).toHaveBeenCalledTimes(1)
    expect(wrapper.find('.asset-trend-stub').exists()).toBe(true)
    expect(wrapper.find('.asset-pie-stub').exists()).toBe(true)
  })
})
