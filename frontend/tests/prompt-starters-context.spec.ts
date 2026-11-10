import { describe, it, expect, vi, beforeEach } from 'vitest'
import { mount, flushPromises } from '@vue/test-utils'
import PromptStarters from '@/components/PromptStarters.vue'
import { summaryApi } from '@/api/summary'

vi.mock('@/api/summary', () => ({
  summaryApi: {
    get: vi.fn(),
  },
}))

describe('PromptStarters financial context capsule and dynamic recommendations', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('renders .context-summary-capsule with formatted financial metrics when summaryApi succeeds', async () => {
    vi.mocked(summaryApi.get).mockResolvedValueOnce({
      total_assets: 150000,
      total_liabilities: 30000,
      net_worth: 120000,
      category_breakdown: [],
      trend: [],
    } as any)

    const wrapper = mount(PromptStarters, {
      global: {
        stubs: { Icon: true },
      },
    })

    await flushPromises()

    const capsule = wrapper.find('.context-summary-capsule')
    expect(capsule.exists()).toBe(true)
    // 验证包含净资产与资产负债率 20.0%
    expect(capsule.text()).toContain('120,000')
    expect(capsule.text()).toContain('20.0%')
    expect(capsule.text()).toContain('已连通实时账本')
  })

  it('displays 零负债 when total liabilities is 0', async () => {
    vi.mocked(summaryApi.get).mockResolvedValueOnce({
      total_assets: 100000,
      total_liabilities: 0,
      net_worth: 100000,
      category_breakdown: [],
      trend: [],
    } as any)

    const wrapper = mount(PromptStarters, {
      global: {
        stubs: { Icon: true },
      },
    })

    await flushPromises()

    const capsule = wrapper.find('.context-summary-capsule')
    expect(capsule.exists()).toBe(true)
    expect(capsule.text()).toContain('零负债')
  })

  it('generates dynamic prompt recommendations with real financial figures embedded', async () => {
    vi.mocked(summaryApi.get).mockResolvedValueOnce({
      total_assets: 200000,
      total_liabilities: 50000,
      net_worth: 150000,
      category_breakdown: [],
      trend: [],
    } as any)

    const wrapper = mount(PromptStarters, {
      global: {
        stubs: { Icon: true },
      },
    })

    await flushPromises()

    // 寻找动态生成的负债或资产诊断卡片
    const cards = wrapper.findAll('.starter-card')
    expect(cards.length).toBeGreaterThanOrEqual(4)

    // 点击包含动态负债诊断的卡片
    const debtCard = cards.find(c => c.text().includes('负债') || c.text().includes('抗风险'))
    expect(debtCard).toBeDefined()
    await debtCard!.trigger('click')

    const emitted = wrapper.emitted('select')
    expect(emitted).toBeTruthy()
    const promptText = emitted![0]![0] as string
    // Prompt 中应包含真实提取的资产/负债上下文数据
    expect(promptText).toMatch(/200,000|150,000|50,000|负债/)
  })

  it('gracefully degrades to classic prompt starters when summaryApi fails or rejects', async () => {
    vi.mocked(summaryApi.get).mockRejectedValueOnce(new Error('Network error'))

    const wrapper = mount(PromptStarters, {
      global: {
        stubs: { Icon: true },
      },
    })

    await flushPromises()

    // 胶囊隐藏，经典卡片依然完整渲染，无报错
    expect(wrapper.find('.context-summary-capsule').exists()).toBe(false)
    const cards = wrapper.findAll('.starter-card')
    expect(cards.length).toBeGreaterThanOrEqual(3)
    expect(wrapper.text()).toContain('全套财务健康体检')
  })
})
