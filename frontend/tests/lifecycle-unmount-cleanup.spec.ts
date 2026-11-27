import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { mount, flushPromises } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import { defineComponent, h } from 'vue'

function createDeferred<T>() {
  let resolve!: (val: T) => void
  let reject!: (err: unknown) => void
  const promise = new Promise<T>((res, rej) => {
    resolve = res
    reject = rej
  })
  return { promise, resolve, reject }
}

const mocks = vi.hoisted(() => ({
  getOAuthProviders: vi.fn(),
  get2FaStatus: vi.fn(),
  setup2Fa: vi.fn(),
  listInstances: vi.fn(),
  getBindings: vi.fn(),
  getInstance: vi.fn(),
  generateBindToken: vi.fn(),
  renderQrContentToImage: vi.fn(),
  marketSearch: vi.fn(),
  marketGetHistory: vi.fn(),
  marketGetExchangeRates: vi.fn(),
  assetsList: vi.fn(),
  accountsList: vi.fn(),
  summaryGet: vi.fn(),
  qrToCanvas: vi.fn(),
  chartSetOption: vi.fn(),
  chartResize: vi.fn(),
  chartDispose: vi.fn(),
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

vi.mock('@/api/auth', () => ({
  authApi: {
    getOAuthProviders: mocks.getOAuthProviders,
    get2FaStatus: mocks.get2FaStatus,
    setup2Fa: mocks.setup2Fa,
    login: vi.fn(),
    register: vi.fn(),
    verify2FaLogin: vi.fn(),
    enable2Fa: vi.fn(),
    disable2Fa: vi.fn(),
  },
}))

vi.mock('@/api/im', () => ({
  imApi: {
    listInstances: mocks.listInstances,
    getBindings: mocks.getBindings,
    getInstance: mocks.getInstance,
    generateBindToken: mocks.generateBindToken,
    createInstance: vi.fn(),
    reloginInstance: vi.fn(),
    verifyInstanceCode: vi.fn(),
    enableInstance: vi.fn(),
    disableInstance: vi.fn(),
    setInstanceCapability: vi.fn(),
    deleteInstance: vi.fn(),
    unbind: vi.fn(),
  },
  renderQrContentToImage: mocks.renderQrContentToImage,
}))

vi.mock('@/api/market', () => ({
  marketApi: {
    search: mocks.marketSearch,
    getHistory: mocks.marketGetHistory,
    getExchangeRates: mocks.marketGetExchangeRates,
  },
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

vi.mock('@/stores/category', () => ({
  useCategoryStore: () => ({
    loadCategories: vi.fn().mockResolvedValue(undefined),
    assetCategories: [],
  }),
}))

vi.mock('@/composables/useAsyncTaskProgress', () => ({
  useAsyncTaskProgress: () => ({
    running: { value: false },
    progress: { value: 0 },
    start: vi.fn(),
  }),
}))

vi.mock('qrcode', () => ({
  default: {
    toCanvas: mocks.qrToCanvas,
  },
  toCanvas: mocks.qrToCanvas,
}))

vi.mock('@/utils/echarts', () => ({
  default: {
    init: vi.fn(() => ({
      setOption: mocks.chartSetOption,
      resize: mocks.chartResize,
      dispose: mocks.chartDispose,
    })),
    graphic: {
      LinearGradient: vi.fn(() => ({})),
    },
  },
}))

vi.mock('@/utils/echarts-theme', () => ({
  resolveChartPalette: () => ({
    surfaceCard: '#fff',
    border: '#ccc',
    borderSubtle: '#eee',
    textMain: '#111',
    textMuted: '#666',
    primary: '#0af',
  }),
  useChartThemeSync: vi.fn(),
  withAlpha: (c: string) => c,
}))

import Login from '@/views/Login.vue'
import ImBotSettings from '@/components/settings/ImBotSettings.vue'
import SymbolSelect from '@/components/SymbolSelect.vue'
import PriceHistoryChart from '@/components/PriceHistoryChart.vue'
import TwoFactorSettings from '@/components/settings/TwoFactorSettings.vue'
import Assets from '@/views/Assets.vue'

const simpleStubs = {
  'el-card': { template: '<div><slot name="header" /><slot /></div>' },
  'el-form': { template: '<form><slot /></form>' },
  'el-form-item': { template: '<div><slot /></div>' },
  'el-input': { template: '<input />' },
  'el-button': {
    emits: ['click'],
    template: '<button @click="$emit(\'click\')"><slot /></button>',
  },
  'el-icon': { template: '<i><slot /></i>' },
  'el-tag': { template: '<span><slot /></span>' },
  'el-alert': { template: '<div><slot /></div>' },
  'el-table': { template: '<div><slot /></div>' },
  'el-table-column': { template: '<div />' },
  'el-popconfirm': { template: '<div><slot name="reference" /></div>' },
  'el-dialog': {
    props: ['modelValue'],
    template: '<div v-if="modelValue"><slot /><slot name="footer" /></div>',
  },
  'el-row': { template: '<div><slot /></div>' },
  'el-col': { template: '<div><slot /></div>' },
  'el-empty': { template: '<div><slot /></div>' },
  'el-dropdown': { template: '<div><slot /><slot name="dropdown" /></div>' },
  'el-dropdown-menu': { template: '<div><slot /></div>' },
  'el-dropdown-item': { template: '<div><slot /></div>' },
  'el-radio-group': { template: '<div><slot /></div>' },
  'el-radio-button': { template: '<div><slot /></div>' },
  Icon: { template: '<span />' },
  AppLogo: { template: '<span />' },
  SummaryCard: { template: '<div />' },
  SymbolSelect: { template: '<div />' },
  PriceHistoryChart: { template: '<div />' },
  AccountDialog: { template: '<div />' },
}

describe('lifecycle unmount cleanup & async sequence guards', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
    vi.useRealTimers()
    mocks.routeQuery = {}

    if (typeof globalThis.ResizeObserver === 'undefined') {
      globalThis.ResizeObserver = class {
        observe() {}
        unobserve() {}
        disconnect() {}
      } as unknown as typeof ResizeObserver
    }
  })

  afterEach(() => {
    vi.restoreAllMocks()
    vi.useRealTimers()
  })

  it('Login.vue does not start rAF loop or attach listeners when unmounted before getOAuthProviders resolves', async () => {
    const deferred = createDeferred<{ providers: any[] }>()
    mocks.getOAuthProviders.mockReturnValue(deferred.promise)

    const rafSpy = vi.spyOn(window, 'requestAnimationFrame').mockReturnValue(1)
    const winAddSpy = vi.spyOn(window, 'addEventListener')
    const docAddSpy = vi.spyOn(document, 'addEventListener')

    const wrapper = mount(Login, {
      global: { stubs: simpleStubs },
    })

    wrapper.unmount()

    rafSpy.mockClear()
    winAddSpy.mockClear()
    docAddSpy.mockClear()

    deferred.resolve({ providers: [] })
    await flushPromises()

    expect(rafSpy).not.toHaveBeenCalled()
    expect(winAddSpy).not.toHaveBeenCalledWith('resize', expect.any(Function))
    expect(winAddSpy).not.toHaveBeenCalledWith('pointermove', expect.any(Function))
    expect(docAddSpy).not.toHaveBeenCalledWith('visibilitychange', expect.any(Function))
  })

  it('ImBotSettings.vue does not start polling interval when unmounted before listInstances resolves', async () => {
    const deferredInstances = createDeferred<any[]>()
    mocks.listInstances.mockReturnValue(deferredInstances.promise)
    mocks.getBindings.mockResolvedValue([])
    mocks.renderQrContentToImage.mockResolvedValue('')

    const setIntervalSpy = vi.spyOn(globalThis, 'setInterval')

    const wrapper = mount(ImBotSettings, {
      global: { stubs: simpleStubs },
    })

    wrapper.unmount()
    setIntervalSpy.mockClear()

    deferredInstances.resolve([
      {
        id: 1,
        platform: 'wechat',
        adapter: 'ilink',
        status: 'qr_ready',
        enabled: true,
        capability: 'readonly',
      },
    ])
    await flushPromises()

    expect(setIntervalSpy).not.toHaveBeenCalled()
  })

  it('ImBotSettings.vue does not start bind-token countdown interval when unmounted before generateBindToken resolves', async () => {
    mocks.listInstances.mockResolvedValue([])
    mocks.getBindings.mockResolvedValue([])
    const deferredToken = createDeferred<{ bind_token: string; ttl_sec: number }>()
    mocks.generateBindToken.mockReturnValue(deferredToken.promise)

    const setIntervalSpy = vi.spyOn(globalThis, 'setInterval')

    const wrapper = mount(ImBotSettings, {
      global: { stubs: simpleStubs },
    })
    await flushPromises()

    const genBtn = wrapper.find('.action-btn')
    await genBtn.trigger('click')

    wrapper.unmount()
    setIntervalSpy.mockClear()

    deferredToken.resolve({ bind_token: 'tok-999', ttl_sec: 300 })
    await flushPromises()

    expect(setIntervalSpy).not.toHaveBeenCalled()
  })

  it('SymbolSelect.vue discards stale slower search response when a newer query resolves first', async () => {
    vi.useFakeTimers()
    const slowDeferred = createDeferred<any[]>()
    const fastDeferred = createDeferred<any[]>()

    mocks.marketSearch.mockImplementation((q: string) => {
      if (q === 'SLOW') return slowDeferred.promise
      return fastDeferred.promise
    })

    const ElSelectStub = defineComponent({
      name: 'ElSelect',
      props: ['modelValue', 'remoteMethod', 'loading'],
      setup(props, { slots }) {
        return () => h('div', { class: 'el-select-stub' }, slots.default?.())
      },
    })
    const ElOptionStub = defineComponent({
      name: 'ElOption',
      props: ['label', 'value'],
      setup(props) {
        return () => h('div', { class: 'el-option-stub', 'data-value': props.value }, props.label)
      },
    })

    const wrapper = mount(SymbolSelect, {
      global: {
        stubs: {
          'el-select': ElSelectStub,
          'el-option': ElOptionStub,
          'el-tag': true,
        },
      },
    })

    const remoteMethod = wrapper.findComponent(ElSelectStub).props('remoteMethod') as (q: string) => void

    remoteMethod('SLOW')
    vi.advanceTimersByTime(300)
    expect(mocks.marketSearch).toHaveBeenCalledWith('SLOW')

    remoteMethod('FAST')
    vi.advanceTimersByTime(300)
    expect(mocks.marketSearch).toHaveBeenCalledWith('FAST')

    fastDeferred.resolve([{ symbol: 'FAST', name: 'Fast Asset', source: 'us' }])
    await flushPromises()

    expect(wrapper.findAll('.el-option-stub').map(n => n.attributes('data-value'))).toEqual(['FAST'])

    slowDeferred.resolve([{ symbol: 'SLOW', name: 'Slow Asset', source: 'us' }])
    await flushPromises()

    expect(wrapper.findAll('.el-option-stub').map(n => n.attributes('data-value'))).toEqual(['FAST'])
  })

  it('SymbolSelect.vue clears pending debounce timer on unmount', async () => {
    vi.useFakeTimers()
    const clearTimeoutSpy = vi.spyOn(globalThis, 'clearTimeout')

    const ElSelectStub = defineComponent({
      name: 'ElSelect',
      props: ['modelValue', 'remoteMethod', 'loading'],
      setup(_, { slots }) {
        return () => h('div', slots.default?.())
      },
    })

    const wrapper = mount(SymbolSelect, {
      global: {
        stubs: {
          'el-select': ElSelectStub,
          'el-option': true,
          'el-tag': true,
        },
      },
    })

    const remoteMethod = wrapper.findComponent(ElSelectStub).props('remoteMethod') as (q: string) => void
    remoteMethod('AAPL')

    clearTimeoutSpy.mockClear()
    wrapper.unmount()

    expect(clearTimeoutSpy).toHaveBeenCalled()

    vi.advanceTimersByTime(500)
    await flushPromises()
    expect(mocks.marketSearch).not.toHaveBeenCalled()
  })

  it('PriceHistoryChart.vue discards stale slower history response when a newer load resolves first', async () => {
    const firstDeferred = createDeferred<any[]>()
    const secondDeferred = createDeferred<any[]>()

    mocks.marketGetHistory.mockImplementation((assetId: number) => {
      if (assetId === 1) return firstDeferred.promise
      return secondDeferred.promise
    })

    const wrapper = mount(PriceHistoryChart, {
      props: {
        assetId: 1,
        assetName: 'Asset One',
      },
      global: {
        stubs: simpleStubs,
        directives: {
          loading: {},
        },
      },
    })

    const chartEl = wrapper.find('.chart-body').element as HTMLElement
    Object.defineProperty(chartEl, 'clientWidth', { configurable: true, value: 400 })
    Object.defineProperty(chartEl, 'clientHeight', { configurable: true, value: 300 })

    await wrapper.setProps({ assetId: 2, assetName: 'Asset Two' })
    expect(mocks.marketGetHistory).toHaveBeenCalledWith(1, 90)
    expect(mocks.marketGetHistory).toHaveBeenCalledWith(2, 90)

    secondDeferred.resolve([{ price_date: '2026-02-01', price: 250 }])
    await flushPromises()

    expect(mocks.chartSetOption).toHaveBeenCalledTimes(1)
    expect(mocks.chartSetOption.mock.calls[0]?.[0]?.series?.[0]?.data).toEqual([250])

    firstDeferred.resolve([{ price_date: '2026-01-01', price: 100 }])
    await flushPromises()

    expect(mocks.chartSetOption).toHaveBeenCalledTimes(1)
  })

  it('TwoFactorSettings.vue clears pending QR render timeout on unmount', async () => {
    vi.useFakeTimers()
    mocks.get2FaStatus.mockResolvedValue({ enabled: false })
    mocks.setup2Fa.mockResolvedValue({
      secret: 'TESTSECRET',
      otpauth_url: 'otpauth://totp/Minefolio:user?secret=TESTSECRET',
    })

    const clearTimeoutSpy = vi.spyOn(globalThis, 'clearTimeout')

    const wrapper = mount(TwoFactorSettings, {
      global: { stubs: simpleStubs },
    })
    await flushPromises()

    await wrapper.find('.action-btn').trigger('click')
    await flushPromises()

    clearTimeoutSpy.mockClear()
    wrapper.unmount()

    expect(clearTimeoutSpy).toHaveBeenCalled()

    vi.advanceTimersByTime(200)
    await flushPromises()
    expect(mocks.qrToCanvas).not.toHaveBeenCalled()
  })

  it('Assets.vue clears deep-link highlight/scroll timeouts on unmount', async () => {
    vi.useFakeTimers()
    mocks.routeQuery = { asset_id: '42' }
    mocks.accountsList.mockResolvedValue([
      { id: 1, name: '默认账户', institution: 'cash_wallet', archived: 0, sort_order: 0 },
    ])
    mocks.assetsList.mockResolvedValue({
      list: [{ id: 42, name: 'Asset 42', asset_type: 'cash', account_id: 1, current_value: 100 }],
      total: 1,
    })
    mocks.summaryGet.mockResolvedValue({
      total_assets: 100,
      total_liabilities: 0,
      net_worth: 100,
      category_breakdown: [],
      trend: [],
    })
    mocks.marketGetExchangeRates.mockResolvedValue({})

    const clearTimeoutSpy = vi.spyOn(globalThis, 'clearTimeout')
    const getByIdSpy = vi.spyOn(document, 'getElementById').mockReturnValue(null)

    const wrapper = mount(Assets, {
      global: { stubs: simpleStubs },
    })
    await flushPromises()

    clearTimeoutSpy.mockClear()
    getByIdSpy.mockClear()
    wrapper.unmount()

    expect(clearTimeoutSpy).toHaveBeenCalled()

    vi.advanceTimersByTime(3000)
    await flushPromises()
    expect(getByIdSpy).not.toHaveBeenCalledWith('asset-row-42')
  })
})
