import { describe, it, expect, beforeEach } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'
import { setActivePinia, createPinia } from 'pinia'
import { useDisplaySettingsStore } from '@/stores/display-settings'

describe('frontend audit phase 5 fixes', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    localStorage.clear()
    document.documentElement.removeAttribute('data-amount-masked')
  })

  it('displaySettings toggles data-amount-masked attribute for charts and views', () => {
    const store = useDisplaySettingsStore()
    expect(store.showAmount).toBe(true)
    expect(document.documentElement.getAttribute('data-amount-masked')).toBeNull()

    store.set(false)
    expect(store.showAmount).toBe(false)
    expect(document.documentElement.getAttribute('data-amount-masked')).toBe('true')

    store.set(true)
    expect(document.documentElement.getAttribute('data-amount-masked')).toBeNull()
  })

  it('Layout.vue guards quick-record shortcut against modifier keys and highlights nested asset routes', () => {
    const src = readFileSync(resolve(process.cwd(), 'src/views/Layout.vue'), 'utf8')
    expect(src).toMatch(/!e\.ctrlKey\s*&&\s*!e\.metaKey\s*&&\s*!e\.altKey\s*&&\s*\(e\.key === 'n'\s*\|\|\s*e\.key === 'N'\)/)
    expect(src).toMatch(/route\.path\.startsWith\('\/assets\/'\)\)\s*return\s*'\/assets'/)
  })

  it('AssetPriceLine.vue uses concrete chart palette rather than CSS variables on canvas', () => {
    const src = readFileSync(resolve(process.cwd(), 'src/components/AssetPriceLine.vue'), 'utf8')
    expect(src).toContain('@/utils/echarts')
    expect(src).toContain('resolveChartPalette')
    expect(src).not.toContain('var(--mf-border)')
  })

  it('http.ts surfaces non-code HTTP error responses and ignores ERR_CANCELED', () => {
    const src = readFileSync(resolve(process.cwd(), 'src/utils/http.ts'), 'utf8')
    expect(src).toContain("err?.code !== 'ERR_CANCELED'")
    expect(src).toContain('err.response.status === 401')
  })
})
