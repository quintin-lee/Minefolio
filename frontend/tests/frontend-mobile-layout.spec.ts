import { describe, it, expect } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

describe('mobile layout responsiveness & accessibility', () => {
  it('MobileLayout uses a flex footer tab bar so full-bleed chat input is never covered and preserves ledger name', () => {
    const src = readFileSync(resolve(process.cwd(), 'src/views-mobile/MobileLayout.vue'), 'utf8')
    expect(src).toMatch(/\.tab-bar\s*\{[^}]*flex-shrink:\s*0/s)
    expect(src).not.toMatch(/\.tab-bar\s*\{[^}]*position:\s*fixed/s)
    expect(src).toMatch(/\.mobile-content\s*\{[^}]*min-height:\s*0/s)
    expect(src).toMatch(/\.mobile-ledger\s*:deep\(\.current-ledger-btn\s+\.ledger-name\)/)
  })

  it('chat-page.css fixes mobile sidebar overlay z-index, valid box-shadow, and touch action visibility', () => {
    const css = readFileSync(resolve(process.cwd(), 'src/styles/chat-page.css'), 'utf8')
    expect(css).toMatch(/\.sidebar-overlay\s*\{[^}]*z-index:\s*199/s)
    expect(css).toContain('z-index: 200;')
    expect(css).toContain('box-shadow: var(--mf-shadow-lg);')
    expect(css).toMatch(/\.session-item-actions\s*\{\s*opacity:\s*1/)
  })

  it('index.css constrains dialogs inside mobile viewports and index.mobile.html allows pinch-zoom', () => {
    const css = readFileSync(resolve(process.cwd(), 'src/styles/index.css'), 'utf8')
    expect(css).toMatch(/\.el-dialog\s*\{[^}]*max-width:\s*calc\(100vw - 24px\)/s)

    const html = readFileSync(resolve(process.cwd(), 'index.mobile.html'), 'utf8')
    expect(html).not.toContain('user-scalable=no')
    expect(html).not.toContain('maximum-scale=1.0')
  })

  it('AssetsMobile preserves scroll-chain ergonomics and prevents long asset names from squeezing amounts', () => {
    const assetsSrc = readFileSync(resolve(process.cwd(), 'src/views-mobile/AssetsMobile.vue'), 'utf8')
    expect(assetsSrc).toContain('overscroll-behavior-y: auto;')
    expect(assetsSrc).toMatch(/\.asset-info\s*\{[^}]*min-width:\s*0/s)

    const pieSrc = readFileSync(resolve(process.cwd(), 'src/components/ExpenseCategoryPie.vue'), 'utf8')
    expect(pieSrc).toContain('isNarrow')
    expect(pieSrc).toContain('< 360')
  })
})
