import { describe, it, expect, vi, beforeEach } from 'vitest'
import { mount, flushPromises } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import ElementPlus from 'element-plus'

const mocks = vi.hoisted(() => ({
  httpGet: vi.fn(),
  importRulesList: vi.fn(),
  skillsList: vi.fn(),
  mcpList: vi.fn(),
  schedulerListJobs: vi.fn(),
  schedulerGetDashboard: vi.fn(),
  newsList: vi.fn(),
  categoriesList: vi.fn(),
  imBindings: vi.fn(),
  imInstances: vi.fn(),
  accountsList: vi.fn(),
  assetsList: vi.fn(),
  summaryGet: vi.fn(),
  ratesGet: vi.fn(),
}))

vi.mock('vue-router', () => ({
  useRouter: () => ({ push: vi.fn(), replace: vi.fn() }),
  useRoute: () => ({ query: {} }),
}))

vi.mock('@/utils/http', () => ({
  default: {
    get: mocks.httpGet,
    post: vi.fn(),
    put: vi.fn(),
    delete: vi.fn(),
  },
}))

vi.mock('@/api/importRules', () => ({
  importRulesApi: {
    list: mocks.importRulesList,
    create: vi.fn(),
    update: vi.fn(),
    delete: vi.fn(),
    resetDefaults: vi.fn(),
  },
}))

vi.mock('@/api/skills', () => ({
  listSkills: mocks.skillsList,
  deleteSkill: vi.fn(),
  exportSkillMarkdown: vi.fn(),
  resetDefaultSkills: vi.fn(),
}))

vi.mock('@/api/ai-mcp', () => ({
  listMcpServers: mocks.mcpList,
  getMcpServerTools: vi.fn().mockResolvedValue([]),
  deleteMcpServer: vi.fn(),
  testMcpServer: vi.fn(),
  refreshMcpServer: vi.fn(),
}))

vi.mock('@/api/scheduler', () => ({
  schedulerApi: {
    listJobs: mocks.schedulerListJobs,
    getDashboard: mocks.schedulerGetDashboard,
    toggleJob: vi.fn(),
    runJob: vi.fn(),
    deleteJob: vi.fn(),
  },
}))

vi.mock('@/api/ai-news', () => ({
  listNewsReports: mocks.newsList,
  triggerNewsAnalysis: vi.fn(),
  getNewsReportByDate: vi.fn(),
}))

vi.mock('@/api/categories', () => ({
  categoriesApi: {
    list: mocks.categoriesList,
    children: vi.fn().mockResolvedValue([]),
    create: vi.fn(),
    update: vi.fn(),
    delete: vi.fn(),
  },
}))

vi.mock('@/api/im', () => ({
  imApi: {
    getBindings: mocks.imBindings,
    listInstances: mocks.imInstances,
    generateBindToken: vi.fn(),
    unbind: vi.fn(),
  },
  renderQrContentToImage: vi.fn().mockResolvedValue(''),
}))

vi.mock('@/api/accounts', () => ({
  accountsApi: {
    list: mocks.accountsList,
  },
}))

vi.mock('@/api/assets', () => ({
  assetsApi: {
    list: mocks.assetsList,
    syncAllAsync: vi.fn(),
    syncOne: vi.fn(),
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
    getExchangeRates: mocks.ratesGet,
  },
}))

vi.mock('@/composables/useAsyncTaskProgress', () => ({
  useAsyncTaskProgress: () => ({
    running: { value: false },
    progress: { value: 0 },
    start: vi.fn(),
  }),
}))

import { fetchAllPages } from '@/utils/pagination'
import { tasksApi } from '@/api/tasks'
import ImportRulesManager from '@/components/settings/ImportRulesManager.vue'
import Skills from '@/views/Skills.vue'
import Mcp from '@/views/Mcp.vue'
import Scheduler from '@/views/Scheduler.vue'
import NewsPulse from '@/views/NewsPulse.vue'
import Categories from '@/views/Categories.vue'
import ImBotSettings from '@/components/settings/ImBotSettings.vue'
import Assets from '@/views/Assets.vue'

describe('Unified backend/frontend pagination', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    localStorage.clear()
    vi.clearAllMocks()
  })

  it('fetchAllPages iterates through multiple pages until total is reached', async () => {
    const fetchPage = vi.fn()
      .mockResolvedValueOnce({ list: [{ id: 1 }, { id: 2 }], total: 3, page: 1, page_size: 2 })
      .mockResolvedValueOnce({ list: [{ id: 3 }], total: 3, page: 2, page_size: 2 })

    const all = await fetchAllPages(fetchPage, 2, 10)
    expect(all).toEqual([{ id: 1 }, { id: 2 }, { id: 3 }])
    expect(fetchPage).toHaveBeenCalledTimes(2)
    expect(fetchPage).toHaveBeenNthCalledWith(1, 1, 2)
    expect(fetchPage).toHaveBeenNthCalledWith(2, 2, 2)
  })

  it('tasksApi.list forwards pagination parameters', async () => {
    mocks.httpGet.mockResolvedValueOnce({ list: [], total: 0, page: 2, page_size: 10 })
    await tasksApi.list({ status: 'completed', page: 2, page_size: 10 })
    expect(mocks.httpGet).toHaveBeenCalledWith('/tasks', {
      params: { status: 'completed', page: 2, page_size: 10 },
    })
  })

  it('ImportRulesManager paginates via API and refetches on page change', async () => {
    mocks.categoriesList.mockResolvedValue({ list: [], total: 0, page: 1, page_size: 500 })
    mocks.importRulesList
      .mockResolvedValueOnce({
        list: [{ id: 1, keyword: 'coffee', match_field: 'all', target_type: 'expense', priority: 10, is_active: true }],
        total: 25,
        page: 1,
        page_size: 20,
      })
      .mockResolvedValueOnce({
        list: [{ id: 21, keyword: 'taxi', match_field: 'all', target_type: 'expense', priority: 20, is_active: true }],
        total: 25,
        page: 2,
        page_size: 20,
      })

    const wrapper = mount(ImportRulesManager, {
      global: { plugins: [ElementPlus] },
    })
    await flushPromises()

    expect(mocks.importRulesList).toHaveBeenCalledWith({ page: 1, page_size: 20 })
    expect(wrapper.text()).toContain('coffee')

    const pager = wrapper.findComponent({ name: 'ElPagination' })
    expect(pager.exists()).toBe(true)
    pager.vm.$emit('update:current-page', 2)
    pager.vm.$emit('current-change', 2)
    await flushPromises()

    expect(mocks.importRulesList).toHaveBeenCalledWith({ page: 2, page_size: 20 })
    expect(wrapper.text()).toContain('taxi')
  })

  it('Skills.vue requests paginated skills and refetches on page change', async () => {
    mocks.skillsList
      .mockResolvedValueOnce({
        list: [{ id: 1, name: 'skill-one', description: 'desc', category: 'finance', enabled: 1, is_builtin: 1 }],
        total: 22,
        page: 1,
        page_size: 20,
      })
      .mockResolvedValueOnce({
        list: [{ id: 21, name: 'skill-page2', description: 'desc2', category: 'finance', enabled: 1, is_builtin: 0 }],
        total: 22,
        page: 2,
        page_size: 20,
      })

    const wrapper = mount(Skills, {
      global: {
        plugins: [ElementPlus],
        stubs: { Icon: true, SkillCard: { props: ['skill'], template: '<div>{{ skill.name }}</div>' }, SkillDialog: true, SkillImportDialog: true },
      },
    })
    await flushPromises()

    expect(mocks.skillsList).toHaveBeenCalledWith(expect.objectContaining({ page: 1, page_size: 20 }))
    expect(wrapper.text()).toContain('skill-one')

    const pager = wrapper.findComponent({ name: 'ElPagination' })
    pager.vm.$emit('update:current-page', 2)
    pager.vm.$emit('current-change', 2)
    await flushPromises()

    expect(mocks.skillsList).toHaveBeenCalledWith(expect.objectContaining({ page: 2, page_size: 20 }))
    expect(wrapper.text()).toContain('skill-page2')
  })

  it('Mcp.vue requests paginated MCP servers and refetches on page change', async () => {
    mocks.mcpList
      .mockResolvedValueOnce({
        list: [{ id: 1, name: 'mcp-one', transport: 'http', url: 'http://a', enabled: 1, tool_count: 1 }],
        total: 21,
        page: 1,
        page_size: 20,
      })
      .mockResolvedValueOnce({
        list: [{ id: 21, name: 'mcp-two', transport: 'stdio', command: 'echo', enabled: 1, tool_count: 0 }],
        total: 21,
        page: 2,
        page_size: 20,
      })

    const wrapper = mount(Mcp, {
      global: {
        plugins: [ElementPlus],
        stubs: { Icon: true, McpCard: { props: ['server'], template: '<div>{{ server.name }}</div>' }, McpDialog: true, McpToolsDrawer: true },
      },
    })
    await flushPromises()

    expect(mocks.mcpList).toHaveBeenCalledWith({ page: 1, page_size: 20 })
    expect(wrapper.text()).toContain('mcp-one')

    const pager = wrapper.findComponent({ name: 'ElPagination' })
    pager.vm.$emit('update:current-page', 2)
    pager.vm.$emit('current-change', 2)
    await flushPromises()

    expect(mocks.mcpList).toHaveBeenCalledWith({ page: 2, page_size: 20 })
    expect(wrapper.text()).toContain('mcp-two')
  })

  it('Scheduler.vue uses server-side pagination with page_size=20 and refetches page 2', async () => {
    mocks.schedulerGetDashboard.mockResolvedValue({ active_jobs: 1, today_runs: 0, next_run_job: null, push_channel: null })
    mocks.schedulerListJobs
      .mockResolvedValueOnce({
        items: [{ id: 1, name: 'job-p1', job_type: 'ai_prompt', trigger_type: 'cron', trigger_config: '{}', delivery_channel: 'silent_db', enabled: 1, is_builtin: 0 }],
        total: 25,
        page: 1,
        page_size: 20,
      })
      .mockResolvedValueOnce({
        items: [{ id: 21, name: 'job-p2', job_type: 'ai_prompt', trigger_type: 'cron', trigger_config: '{}', delivery_channel: 'silent_db', enabled: 1, is_builtin: 0 }],
        total: 25,
        page: 2,
        page_size: 20,
      })

    const wrapper = mount(Scheduler, {
      global: {
        plugins: [ElementPlus],
        stubs: { Icon: true, ChannelConfigDialog: true, JobDialog: true, JobLogDrawer: true },
      },
    })
    await flushPromises()

    expect(mocks.schedulerListJobs).toHaveBeenCalledWith(1, 20)
    expect(wrapper.text()).toContain('job-p1')

    const pager = wrapper.findComponent({ name: 'ElPagination' })
    pager.vm.$emit('update:current-page', 2)
    pager.vm.$emit('current-change', 2)
    await flushPromises()

    expect(mocks.schedulerListJobs).toHaveBeenCalledWith(2, 20)
    expect(wrapper.text()).toContain('job-p2')
    wrapper.unmount()
  })

  it('NewsPulse.vue uses server-side ElPagination and refetches page 2', async () => {
    mocks.newsList
      .mockResolvedValueOnce({
        list: [{ id: 1, report_date: '2026-11-28', title: 'Report P1', summary: 's1', model_used: 'ai', token_cost: 0, status: 'done', error_msg: '', created_at: '' }],
        total: 15,
        page: 1,
        page_size: 10,
        has_more: true,
      })
      .mockResolvedValueOnce({
        list: [{ id: 11, report_date: '2026-11-18', title: 'Report P2', summary: 's2', model_used: 'ai', token_cost: 0, status: 'done', error_msg: '', created_at: '' }],
        total: 15,
        page: 2,
        page_size: 10,
        has_more: false,
      })

    const wrapper = mount(NewsPulse, {
      global: { plugins: [ElementPlus], stubs: { Icon: true } },
    })
    await flushPromises()

    expect(mocks.newsList).toHaveBeenCalledWith(1, 10)
    expect(wrapper.text()).toContain('Report P1')

    const pager = wrapper.findComponent({ name: 'ElPagination' })
    pager.vm.$emit('update:current-page', 2)
    pager.vm.$emit('current-change', 2)
    await flushPromises()

    expect(mocks.newsList).toHaveBeenCalledWith(2, 10)
    expect(wrapper.text()).toContain('Report P2')
    wrapper.unmount()
  })

  it('Categories.vue paginates roots from server and refetches page 2', async () => {
    mocks.categoriesList.mockImplementation(async (params: any) => {
      if (params?.page_size === 500) {
        return { list: [{ id: 1, name: '餐饮', type: 'expense', parent_id: null, sort_order: 1 }], total: 25, page: 1, page_size: 500 }
      }
      if (params?.page === 2) {
        return { list: [{ id: 21, name: '医疗', type: 'expense', parent_id: null, sort_order: 21 }], total: 25, page: 2, page_size: 20 }
      }
      return { list: [{ id: 1, name: '餐饮', type: 'expense', parent_id: null, sort_order: 1 }], total: 25, page: 1, page_size: 20 }
    })

    const wrapper = mount(Categories, {
      global: { plugins: [ElementPlus] },
    })
    await flushPromises()

    expect(wrapper.text()).toContain('餐饮')
    const pager = wrapper.findComponent({ name: 'ElPagination' })
    pager.vm.$emit('update:current-page', 2)
    pager.vm.$emit('current-change', 2)
    await flushPromises()

    expect(mocks.categoriesList).toHaveBeenCalledWith(expect.objectContaining({ page: 2, page_size: 20 }))
    expect(wrapper.text()).toContain('医疗')
  })

  it('ImBotSettings.vue passes pagination params to bindings and instances', async () => {
    mocks.imInstances.mockResolvedValue({ list: [], total: 0, page: 1, page_size: 20 })
    mocks.imBindings.mockResolvedValue({
      list: [{ platform: 'qq', im_user_id: 'qq_123', scope: 'readonly', enabled: 1, created_at: '' }],
      total: 25,
      page: 1,
      page_size: 20,
    })

    const wrapper = mount(ImBotSettings, {
      global: { plugins: [ElementPlus], stubs: { Icon: true } },
    })
    await flushPromises()

    expect(mocks.imInstances).toHaveBeenCalledWith({ page: 1, page_size: 20 })
    expect(mocks.imBindings).toHaveBeenCalledWith({ page: 1, page_size: 20 })
    expect(wrapper.text()).toContain('25')
    wrapper.unmount()
  })

  it('Assets.vue paginates accounts and does not misclassify off-page account assets as ungrouped', async () => {
    mocks.categoriesList.mockResolvedValue({ list: [], total: 0, page: 1, page_size: 500 })
    mocks.summaryGet.mockResolvedValue({ total_assets: 300, total_liabilities: 0, net_worth: 300, category_breakdown: [], trend: [] })
    mocks.ratesGet.mockResolvedValue({})
    mocks.assetsList.mockResolvedValue({
      list: [
        { id: 101, name: 'Page1Asset', asset_type: 'cash', account_id: 1, current_value: 100, currency: 'CNY', category_name: '现金' },
        { id: 102, name: 'Page2AccountAsset', asset_type: 'cash', account_id: 2, current_value: 200, currency: 'CNY', category_name: '现金' },
      ],
      total: 2,
    })
    mocks.accountsList.mockImplementation(async (params: any) => {
      if (params?.page === 2) {
        return { list: [{ id: 2, name: 'AccountPage2', institution: 'broker', archived: 0, sort_order: 1 }], total: 2, page: 2, page_size: 20 }
      }
      return { list: [{ id: 1, name: 'AccountPage1', institution: 'cash_wallet', archived: 0, sort_order: 0 }], total: 2, page: 1, page_size: 20 }
    })

    const wrapper = mount(Assets, {
      global: {
        plugins: [ElementPlus],
        stubs: { Icon: true, SummaryCard: true, SymbolSelect: true, PriceHistoryChart: true, AccountDialog: true },
      },
    })
    await flushPromises()

    expect(wrapper.text()).toContain('AccountPage1')
    expect(wrapper.text()).not.toContain('未分组')
    wrapper.unmount()
  })
})
