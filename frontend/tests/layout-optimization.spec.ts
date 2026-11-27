import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

function readSrc(relPath: string): string {
  return readFileSync(resolve(process.cwd(), relPath), 'utf8')
}

describe('frontend layout optimization', () => {
  it('defines centralized layout and z-index tokens plus overlay viewport guards in index.css', () => {
    const css = readSrc('src/styles/index.css')
    expect(css).toContain('--mf-content-max-width: 1600px;')
    expect(css).toContain('--mf-z-header: 20;')
    expect(css).toContain('--mf-z-backdrop: 90;')
    expect(css).toContain('--mf-z-sidebar: 100;')
    expect(css).toContain('--mf-z-floating: 120;')
    expect(css).toContain('--mf-z-popover: 2500;')
    expect(css).toContain('--mf-z-command: 3000;')
    expect(css).toContain('--mf-z-fullscreen: 3500;')

    expect(css).toMatch(/\.el-dialog__body\s*\{[^}]*max-height:\s*calc\(100dvh - 180px\);[^}]*overflow-y:\s*auto;/s)
    expect(css).toMatch(/\.el-drawer\s*\{[^}]*max-width:\s*100vw\s*!important;/s)
    expect(css).toMatch(/\.el-message-box\s*\{[^}]*max-width:\s*calc\(100vw - 24px\)\s*!important;/s)
    expect(css).toMatch(/@media\s*\(max-width:\s*768px\)\s*\{[\s\S]*?\.page-header\s*\{[^}]*flex-wrap:\s*wrap;/s)
  })

  it('uses centralized z-index tokens across overlay components without hardcoded 9999', () => {
    const mermaid = readSrc('src/components/MermaidBlock.vue')
    const palette = readSrc('src/components/CommandPalette.vue')
    const popover = readSrc('src/components/FinancialEntityPopover.vue')
    const analysis = readSrc('src/components/AnalysisPanel.vue')

    expect(mermaid).not.toContain('z-index: 9999;')
    expect(mermaid).toContain('var(--mf-z-fullscreen, 3500)')
    expect(palette).toContain('var(--mf-z-command, 3000)')
    expect(popover).toContain('var(--mf-z-popover, 2500)')
    expect(analysis).toContain('var(--mf-z-floating, 120)')
  })

  it('adds 100dvh fallbacks and uses flex scrolling instead of magic calc(100vh) in McpToolsDrawer', () => {
    for (const file of [
      'src/views/Layout.vue',
      'src/components/AnalysisPanel.vue',
      'src/views/Login.vue',
      'src/views/Setup.vue',
      'src/views/NotFound.vue',
      'src/views-mobile/LoginMobile.vue',
    ]) {
      expect(readSrc(file)).toContain('100dvh')
    }
    expect(readSrc('src/components/settings/UserManagement.vue')).toContain('100dvh')

    const mcpDrawer = readSrc('src/components/mcp/McpToolsDrawer.vue')
    expect(mcpDrawer).not.toContain('calc(100vh - 240px)')
    expect(mcpDrawer).toMatch(/\.tools-list\s*\{[^}]*flex:\s*1;[^}]*min-height:\s*0;[^}]*overflow-y:\s*auto;/s)
  })

  it('unlocks narrow-screen scrolling under @media (max-width: 768px) for locked table views', () => {
    for (const file of [
      'src/views/AiTraces.vue',
      'src/views/AuditLogs.vue',
      'src/views/Holdings.vue',
      'src/views/Plans.vue',
    ]) {
      const src = readSrc(file)
      expect(src).toMatch(/@media\s*\(max-width:\s*768px\)\s*\{[\s\S]*height:\s*auto;[\s\S]*overflow:\s*visible;/s)
    }
  })

  it('polishes Layout.vue with single active indicator, collapsed dividers, and ultrawide max-width', () => {
    const layout = readSrc('src/views/Layout.vue')
    expect(layout).not.toContain('is-active)::after')
    expect(layout).toMatch(/\.aside\.is-collapsed\s+\.nav-group-label\s*\{/)
    expect(layout).toMatch(/\.main:not\(\.is-full-bleed\)\s*>\s*:deep\(\*\)\s*\{[^}]*max-width:\s*var\(--mf-content-max-width/s)
    expect(layout).toContain('@media (max-width: 1024px)')
    expect(layout).toContain('var(--mf-z-sidebar, 100)')
    expect(layout).toContain('var(--mf-z-backdrop, 90)')
  })

  it('deduplicates MobileLayout topbar styles and enforces comfortable touch targets', () => {
    const mobileLayout = readSrc('src/views-mobile/MobileLayout.vue')
    const btnBlocks = mobileLayout.match(/\.mobile-ledger\s*:deep\(\.current-ledger-btn\)\s*\{/g) || []
    expect(btnBlocks).toHaveLength(1)
    expect(mobileLayout).toMatch(/\.mobile-ledger\s*\{[^}]*min-width:\s*0;[^}]*flex:\s*1;/s)
    expect(mobileLayout).toMatch(/\.top-bar-actions\s*\{[^}]*flex-shrink:\s*0;/s)
    expect(mobileLayout).toMatch(/\.tab-item\s*\{[^}]*min-height:\s*44px;/s)

    const css = readSrc('src/styles/index.css')
    expect(css).toMatch(/\.mobile-segmented\s+\.segmented-item\s*\{[^}]*min-height:\s*32px;/s)
  })

  it('unlocks Categories narrow stacking, wraps category tabs, and deduplicates .node-icon', () => {
    const categories = readSrc('src/views/Categories.vue')
    expect(categories).toMatch(/\.category-tabs\s*\{[^}]*flex-wrap:\s*wrap;/s)
    expect(categories).toMatch(/@media\s*\(max-width:\s*992px\)\s*\{[\s\S]*?\.categories-page\s*\{[^}]*height:\s*auto;/s)
    expect(categories.match(/^\.node-icon\s*\{/gm) || []).toHaveLength(1)
  })

  it('uses safe grid floor bounds and responsive collapses across grid components', () => {
    for (const file of ['src/views/Mcp.vue', 'src/views/Skills.vue']) {
      expect(readSrc(file)).toContain('minmax(min(320px, 100%), 1fr)')
    }
    for (const file of [
      'src/components/market/GlobalIndicesBoard.vue',
      'src/components/market/MarketSkeleton.vue',
    ]) {
      expect(readSrc(file)).toContain('minmax(min(280px, 100%), 1fr)')
    }
    expect(readSrc('src/components/WorkflowConfigCard.vue')).toMatch(/@media\s*\(max-width:\s*640px\)/)
    expect(readSrc('src/components/market/FxHoldingsSummary.vue')).toMatch(/@media\s*\(max-width:\s*768px\)/)
    expect(readSrc('src/components/market/FxTrendChart.vue')).toMatch(/@media\s*\(max-width:\s*768px\)/)
    expect(readSrc('src/components/market/GlobalIndicesPlaceholder.vue')).toMatch(/@media\s*\(max-width:\s*768px\)/)
    expect(readSrc('src/components/CashflowCalendar.vue')).toContain('repeat(7, minmax(0, 1fr))')
  })

  it('reflows Assets rows on narrow screens and uses design tokens for .institution-tag', () => {
    const assets = readSrc('src/views/Assets.vue')
    expect(assets).toMatch(/\.institution-tag\s*\{[^}]*var\(--mf-primary-light\)/s)
    expect(assets).toMatch(/@media\s*\(max-width:\s*768px\)\s*\{[\s\S]*?\.asset-row\s*\{[^}]*grid-template-areas:/s)
    expect(assets).not.toContain('rgba(0, 212, 255, 0.08)')
  })

  it('centers floating batch bars relative to Layout sidebar width and uses centralized tokens', () => {
    const layout = readSrc('src/views/Layout.vue')
    expect(layout).toContain('--mf-sidebar-width: 260px;')
    expect(layout).toContain('--mf-sidebar-width: 64px;')

    for (const file of ['src/views/Transactions.vue', 'src/views/DailyExpenses.vue']) {
      const src = readSrc(file)
      expect(src).toContain('calc(50% + var(--mf-sidebar-width, 0px) / 2)')
      expect(src).toContain('z-index: var(--mf-z-floating, 120);')
      expect(src).toContain('max-width: calc(100vw - 24px);')
    }
  })

  it('wraps Reports metric cards, sizes charts on narrow viewports, expands spacing scale, and tokenizes remaining z-index overlays', () => {
    const reports = readSrc('src/views/Reports.vue')
    expect(reports).toMatch(/\.metric-cards\s*\{[^}]*flex-wrap:\s*wrap;/s)
    expect(reports).toMatch(/@media\s*\(max-width:\s*1200px\)\s*\{[\s\S]*?\.chart-container\s*\{[^}]*min-height:\s*300px;/s)

    const css = readSrc('src/styles/index.css')
    expect(css).toContain('--mf-spacing-xs: 4px;')
    expect(css).toContain('--mf-spacing-sm: 8px;')
    expect(css).toContain('--mf-spacing-xl: 32px;')

    for (const file of [
      'src/components/AssetMentionMenu.vue',
      'src/components/WorkflowSlashMenu.vue',
      'src/views-mobile/DailyExpensesMobile.vue',
    ]) {
      expect(readSrc(file)).toContain('var(--mf-z-floating, 120)')
    }
    const chatCss = readSrc('src/styles/chat-page.css')
    expect(chatCss).toMatch(/\.panel-overlay-backdrop\s*\{[^}]*var\(--mf-z-floating,\s*120\)/s)
  })
})
