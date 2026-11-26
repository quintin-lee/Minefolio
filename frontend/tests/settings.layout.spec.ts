import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const settingsSource = readFileSync(resolve(process.cwd(), 'src/views/Settings.vue'), 'utf8')

describe('Settings layout', () => {
  it('allows AI settings content to scroll when the MCP panel exceeds the viewport', () => {
    expect(settingsSource).toMatch(/\.settings-tabs :deep\(\.el-tab-pane\)\s*{[^}]*overflow-y:\s*auto;/s)
    expect(settingsSource).toMatch(/\.settings-tabs :deep\(\.el-tabs__content\)\s*{[^}]*min-height:\s*0;[^}]*height:\s*100%;[^}]*overflow:\s*hidden;/s)
  })

  it('switches tab position responsively and unlocks scrolling below 768px', () => {
    expect(settingsSource).toContain(':tab-position="tabPosition"')
    expect(settingsSource).toContain("window.innerWidth < 900 ? 'top' : 'left'")
    expect(settingsSource).toMatch(/@media\s*\(max-width:\s*768px\)\s*\{[\s\S]*?\.settings-page\s*\{[^}]*height:\s*auto;[^}]*overflow:\s*visible;/s)
  })
})
