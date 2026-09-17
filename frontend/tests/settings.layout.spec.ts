import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const settingsSource = readFileSync(resolve(process.cwd(), 'src/views/Settings.vue'), 'utf8')

describe('Settings layout', () => {
  it('allows AI settings content to scroll when the MCP panel exceeds the viewport', () => {
    expect(settingsSource).toMatch(/\.settings-tabs :deep\(\.el-tab-pane\)\s*{[^}]*overflow-y:\s*auto;/s)
  })
})
