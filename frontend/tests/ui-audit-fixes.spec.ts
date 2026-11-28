import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'
import { computeFxYAxisBounds } from '@/utils/fx'

const read = (rel: string) => readFileSync(resolve(process.cwd(), rel), 'utf8')

describe('UI audit & responsive layout fixes', () => {
  it('unlocks natural vertical flow on Holdings, Transactions, DailyExpenses, Plans, and Scheduler', () => {
    const targets: Array<[string, string]> = [
      ['src/views/Holdings.vue', 'holdings-page'],
      ['src/views/Transactions.vue', 'transactions-page'],
      ['src/views/DailyExpenses.vue', 'daily-expenses-page'],
      ['src/views/Plans.vue', 'plans-page'],
      ['src/views/Scheduler.vue', 'scheduler-page'],
    ]
    for (const [file, cls] of targets) {
      const src = read(file)
      expect(src).toContain('min-height: 100%;')
      expect(src).not.toMatch(new RegExp(`\\.${cls}\\s*\\{[^}]*height:\\s*100%;[^}]*overflow:\\s*hidden;`, 's'))
    }
  })

  it('protects action columns from clipping/overlap on Categories and Transactions', () => {
    const categories = read('src/views/Categories.vue')
    expect(categories).toContain('<el-table-column label="操作" width="106" align="center" fixed="right">')
    expect(categories).toContain(':xs="24" :md="16" :lg="17"')

    const transactions = read('src/views/Transactions.vue')
    expect(transactions).toContain("const COMPACT_TABLE_QUERY = '(max-width: 1440px)'")
    expect(transactions).toContain('isCompactTable && row.linked_asset_name')
    expect(transactions).toContain('<el-table-column label="操作" width="88" align="center" fixed="right">')
  })

  it('uses unified PageHeader with canonical Chinese titles and breadcrumb top-bar in Layout', () => {
    const pageHeader = read('src/components/PageHeader.vue')
    expect(pageHeader).toContain('<h1 class="mf-page-title">{{ title }}</h1>')

    const layout = read('src/views/Layout.vue')
    expect(layout).toContain('class="header-breadcrumb"')
    expect(layout).toContain('currentNavGroup')

    const desktopViews = [
      'src/views/Dashboard.vue',
      'src/views/Assets.vue',
      'src/views/AssetDetail.vue',
      'src/views/Holdings.vue',
      'src/views/Market.vue',
      'src/views/Transactions.vue',
      'src/views/DailyExpenses.vue',
      'src/views/Plans.vue',
      'src/views/Categories.vue',
      'src/views/Reports.vue',
      'src/views/Scheduler.vue',
      'src/views/Skills.vue',
      'src/views/Mcp.vue',
      'src/views/NewsPulse.vue',
      'src/views/AiTraces.vue',
      'src/views/AuditLogs.vue',
      'src/views/Settings.vue',
    ]
    for (const file of desktopViews) {
      expect(read(file)).toContain('<PageHeader')
    }

    expect(read('src/views/Plans.vue')).not.toContain('(DCA Plans)')
    expect(read('src/views/Plans.vue')).not.toContain('(Cashflow Calendar)')
    expect(read('src/views/Scheduler.vue')).not.toContain('(Scheduler Center)')
    expect(read('src/views/Mcp.vue')).not.toContain('(Model Context Protocol)')
  })

  it('adapts tablet viewports: Chat drawer, AssetDetail no-overflow grid, and pinned fixed-right table columns at 768px', () => {
    const chat = read('src/views/Chat.vue')
    expect(chat).toContain('window.innerWidth > 1024')
    expect(chat).toContain('isSidebarDrawer')

    const chatCss = read('src/styles/chat-page.css')
    expect(chatCss).toMatch(/@media\s*\(max-width:\s*1024px\)\s*\{[\s\S]*?\.chat-sidebar\s*\{/s)

    const detail = read('src/views/AssetDetail.vue')
    expect(detail).toContain('overflow-x: hidden;')
    expect(detail).toContain('.metric-grid')

    const indexCss = read('src/styles/index.css')
    expect(indexCss).toMatch(/@media\s*\(max-width:\s*640px\)\s*\{[\s\S]*?el-table-fixed-column--right/s)

    const scheduler = read('src/views/Scheduler.vue')
    expect(scheduler).toContain("const COMPACT_TABLE_QUERY = '(max-width: 1024px)'")
  })

  it('fixes mobile KPI hero grid, removes size="small" buttons on mobile, and compacts PromptStarters', () => {
    const dashboardMobile = read('src/views-mobile/DashboardMobile.vue')
    expect(dashboardMobile).toContain('grid-template-columns: repeat(2, minmax(0, 1fr))')
    expect(dashboardMobile).toContain('hero-kpi')
    expect(dashboardMobile).not.toContain('flex: 1 0 132px;')

    const mobileFiles = [
      'src/views-mobile/AssetsMobile.vue',
      'src/views-mobile/CategoryManageMobile.vue',
      'src/views-mobile/PlansMobile.vue',
      'src/views-mobile/SettingsMobile.vue',
      'src/views-mobile/TransactionsMobile.vue',
      'src/views-mobile/ExpenseQuickSheet.vue',
      'src/components/mobile/ConflictList.vue',
    ]
    for (const file of mobileFiles) {
      const src = read(file)
      expect(src).not.toMatch(/<el-button[^>]*size="small"/)
    }

    const starters = read('src/components/PromptStarters.vue')
    expect(starters).toMatch(/@media\s*\(max-width:\s*640px\)\s*\{[\s\S]*?\.card-footer\s*\{\s*display:\s*none;\s*\}/s)
  })

  it('computes tight FX y-axis bounds, prevents empty Chat auto-scroll, styles dark el-empty, and hides empty pagination', () => {
    const constBounds = computeFxYAxisBounds([6.65, 6.65, 6.65])
    expect(constBounds).not.toBeNull()
    expect(constBounds!.min).toBeGreaterThan(6.5)
    expect(constBounds!.max).toBeLessThan(6.8)

    const narrowBounds = computeFxYAxisBounds([7.21, 7.25, 7.23])
    expect(narrowBounds).not.toBeNull()
    expect(narrowBounds!.min).toBeGreaterThan(7.1)
    expect(narrowBounds!.max).toBeLessThan(7.35)

    expect(computeFxYAxisBounds([])).toBeNull()

    const chat = read('src/views/Chat.vue')
    expect(chat).toMatch(/if\s*\(chat\.messages\.length === 0\)\s*\{\s*el\.scrollTop = 0/s)

    const chatMobile = read('src/views-mobile/ChatMobile.vue')
    expect(chatMobile).toContain('chat.messages.length === 0 ? 0 : el.scrollHeight')

    const indexCss = read('src/styles/index.css')
    expect(indexCss).toContain('--el-empty-fill-color-0:')
    expect(indexCss).toContain('--el-empty-fill-color-9:')

    expect(read('src/views/Holdings.vue')).toContain('v-if="(report?.holdings.length ?? 0) > 0"')
    expect(read('src/views/Transactions.vue')).toContain('v-if="total > 0"')
    expect(read('src/views/DailyExpenses.vue')).toContain('v-if="total > 0"')
    expect(read('src/views/Plans.vue')).toContain('v-if="dcaTotal > 0"')
    expect(read('src/views/AiTraces.vue')).toContain('v-if="total > 0"')
    expect(read('src/views/AuditLogs.vue')).toContain('v-if="total > 0"')
  })
})
