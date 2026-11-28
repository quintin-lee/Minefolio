import { describe, it, expect, beforeEach, vi } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'
import { setActivePinia, createPinia } from 'pinia'
import { mount, flushPromises } from '@vue/test-utils'
import { useDisplaySettingsStore, maskAmountText } from '@/stores/display-settings'
import SummaryCard from '@/components/SummaryCard.vue'
import PromptStarters from '@/components/PromptStarters.vue'
import { summaryApi } from '@/api/summary'

vi.mock('@/api/summary', () => ({
  summaryApi: {
    get: vi.fn(),
  },
}))

function readSrc(relPath: string): string {
  return readFileSync(resolve(process.cwd(), relPath), 'utf8')
}

describe('global amount masking', () => {
  beforeEach(() => {
    localStorage.clear()
    document.documentElement.removeAttribute('data-amount-masked')
    vi.clearAllMocks()
  })

  it('maskAmountText falls back to raw text when Pinia is not active and masks reactively when active', () => {
    // No active Pinia (isolated component tests)
    setActivePinia(undefined)
    expect(maskAmountText('¥123.45')).toBe('¥123.45')

    setActivePinia(createPinia())
    const store = useDisplaySettingsStore()
    expect(store.showAmount).toBe(true)
    expect(store.maskText('¥123.45')).toBe('¥123.45')
    expect(maskAmountText('¥123.45')).toBe('¥123.45')

    store.set(false)
    expect(document.documentElement.getAttribute('data-amount-masked')).toBe('true')
    expect(store.maskText('¥123.45')).toBe('****')
    expect(store.maskText('¥123.45', '***')).toBe('***')
    expect(maskAmountText('¥123.45')).toBe('****')
    expect(maskAmountText('¥123.45', '---')).toBe('---')

    store.set(true)
    expect(document.documentElement.getAttribute('data-amount-masked')).toBeNull()
    expect(maskAmountText('¥123.45')).toBe('¥123.45')
  })

  it('SummaryCard applies .mf-amount-mask by default and omits it when maskable is false', () => {
    const defaultCard = mount(SummaryCard, {
      props: { label: '总资产', value: '¥1,000.00', type: 'asset' },
    })
    expect(defaultCard.find('.summary-value').classes()).toContain('mf-amount-mask')

    const countCard = mount(SummaryCard, {
      props: { label: '本月交易笔数', value: '12 笔', type: 'highlight', maskable: false },
    })
    expect(countCard.find('.summary-value').classes()).not.toContain('mf-amount-mask')
  })

  it('PromptStarters masks starter card descriptions when showAmount is false while keeping AI prompt unmasked', async () => {
    const pinia = createPinia()
    setActivePinia(pinia)
    const store = useDisplaySettingsStore()
    store.set(false)

    vi.mocked(summaryApi.get).mockResolvedValueOnce({
      total_assets: 200000,
      total_liabilities: 50000,
      net_worth: 150000,
      category_breakdown: [],
      trend: [],
    } as any)

    const wrapper = mount(PromptStarters, {
      global: {
        plugins: [pinia],
        stubs: { Icon: true },
      },
    })
    await flushPromises()

    const cards = wrapper.findAll('.starter-card')
    const debtCard = cards.find(c => c.text().includes('负债健康度'))
    const healthCard = cards.find(c => c.text().includes('全景资产配置'))
    expect(debtCard?.text()).toContain('****')
    expect(debtCard?.text()).not.toContain('50,000')
    expect(healthCard?.text()).toContain('****')
    expect(healthCard?.text()).not.toContain('150,000')

    await debtCard!.trigger('click')
    const emitted = wrapper.emitted('select')
    expect(emitted).toBeTruthy()
    expect(emitted![0]![0]).toContain('50,000')
    expect(emitted![0]![0]).toContain('150,000')
  })

  it('initializes display settings in App.vue and registers canonical mask selectors in index.css', () => {
    const appSrc = readSrc('src/App.vue')
    expect(appSrc).toContain('useDisplaySettingsStore()')

    const css = readSrc('src/styles/index.css')
    expect(css).toContain('[data-amount-masked="true"] .mf-amount-mask')
    expect(css).toContain('[data-amount-masked="true"] .mf-amount-pill')
    expect(css).toContain('[data-amount-masked="true"] .mf-amount-unmasked')
  })

  it('avoids double masking in AssetBreakdownPie and keeps count cards unmasked', () => {
    const pieSrc = readSrc('src/components/AssetBreakdownPie.vue')
    expect(pieSrc).toContain('center-val mf-amount-mask')
    expect(pieSrc).not.toMatch(/center-val[^>]*\?\s*formatCurrency/)

    const txSrc = readSrc('src/views/Transactions.vue')
    expect(txSrc).toContain('label="本月交易笔数"')
    expect(txSrc).toMatch(/label="本月交易笔数"[^\n]*:maskable="false"/)

    const plansSrc = readSrc('src/views/Plans.vue')
    expect(plansSrc).toMatch(/label="活跃定投计划"[^\n]*:maskable="false"/)
  })

  it('covers all audited desktop views, mobile views, and shared components with masking classes or maskAmountText', () => {
    const cssMaskedFiles = [
      'src/views/Dashboard.vue',
      'src/views/Assets.vue',
      'src/views/Holdings.vue',
      'src/views/Transactions.vue',
      'src/views/DailyExpenses.vue',
      'src/views/Reports.vue',
      'src/views/Plans.vue',
      'src/views/AssetDetail.vue',
      'src/views/AuditLogs.vue',
      'src/views-mobile/DashboardMobile.vue',
      'src/views-mobile/AssetsMobile.vue',
      'src/views-mobile/HoldingsMobile.vue',
      'src/views-mobile/DailyExpensesMobile.vue',
      'src/views-mobile/PlansMobile.vue',
      'src/components/CashflowCalendar.vue',
      'src/components/market/FxHoldingsSummary.vue',
      'src/components/FinancialEntityPopover.vue',
      'src/components/BatchActionCard.vue',
      'src/components/ActionCard.vue',
    ]
    for (const file of cssMaskedFiles) {
      expect(readSrc(file), `${file} should use mf-amount-mask`).toContain('mf-amount-mask')
    }

    const textMaskedFiles = [
      'src/components/PromptStarters.vue',
      'src/components/ActionCard.vue',
      'src/components/ChatMessageContent.vue',
      'src/components/AssetMentionMenu.vue',
      'src/components/QuickRecordDialog.vue',
      'src/components/DcaPlanDialog.vue',
      'src/components/CashflowScheduleDialog.vue',
      'src/views/Transactions.vue',
      'src/views/DailyExpenses.vue',
    ]
    for (const file of textMaskedFiles) {
      expect(readSrc(file), `${file} should use maskAmountText`).toContain('maskAmountText')
    }
  })
})
