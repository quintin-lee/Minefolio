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
})
