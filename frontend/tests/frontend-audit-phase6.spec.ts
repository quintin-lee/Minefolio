import { describe, it, expect } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'
import { zhCN } from '@/locales/zh-CN'
import { enUS } from '@/locales/en-US'

describe('frontend audit phase 6 fixes', () => {
  it('defines all previously missing i18n keys in both zhCN and enUS', () => {
    expect(zhCN.common.actions).toBeTruthy()
    expect(enUS.common.actions).toBeTruthy()
    expect(zhCN.common.refresh).toBeTruthy()
    expect(enUS.common.refresh).toBeTruthy()
    expect(zhCN.categories.selectCategory).toBeTruthy()
    expect(enUS.categories.selectCategory).toBeTruthy()
    expect(zhCN.settings.generateTokenFailed).toBeTruthy()
    expect(enUS.settings.generateTokenFailed).toBeTruthy()
    expect(zhCN.settings.passwordFailed).toBeTruthy()
    expect(enUS.settings.passwordFailed).toBeTruthy()
  })

  it('keeps :focus-visible outline-only, narrows mermaid error selectors, and uses safe-area toast inset in index.css', () => {
    const css = readFileSync(resolve(process.cwd(), 'src/styles/index.css'), 'utf8')
    expect(css).not.toMatch(/:focus-visible\s*\{[^}]*background\s*:/s)
    expect(css).toContain('env(safe-area-inset-bottom)')
    expect(css).not.toContain('body > div[id^="d-"]')
  })

  it('removes hardcoded legacy #00d4ff cyan colors in chat-page.css', () => {
    const css = readFileSync(resolve(process.cwd(), 'src/styles/chat-page.css'), 'utf8')
    expect(css).not.toContain('#00d4ff')
    expect(css).not.toMatch(/0,\s*212,\s*255/)
    expect(css).toContain('rgba(0, 0, 0, 0.45)')
  })
})
