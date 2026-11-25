import { describe, it, expect, beforeEach, vi } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'
import { setActivePinia, createPinia } from 'pinia'
import { mount } from '@vue/test-utils'
import { sanitizeMermaidSvg } from '@/utils/mermaid'
import { renderSafeMarkdown } from '@/utils/markdown'
import { useDisplaySettingsStore } from '@/stores/display-settings'

vi.mock('vue-i18n', () => ({
  createI18n: () => ({
    global: {
      locale: { value: 'zh-CN' },
      t: (k: string) => k,
    },
  }),
  useI18n: () => ({
    t: (k: string) => k,
    locale: { value: 'zh-CN' },
  }),
}))

describe('Frontend Audit Phase 1 fixes', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    localStorage.clear()
  })

  it('mermaid.ts enforces securityLevel strict and strips XSS payloads in sanitizeMermaidSvg', () => {
    const src = readFileSync(resolve(process.cwd(), 'src/utils/mermaid.ts'), 'utf8')
    expect(src).not.toMatch(/securityLevel:\s*['"]loose['"]/)
    expect(src).toMatch(/securityLevel:\s*['"]strict['"]/)
    expect(src).not.toMatch(/HTML_INTEGRATION_POINTS/)

    const dirty = '<svg><style>.n{fill:red}</style><foreignObject><div><img src="x" onerror="alert(1)">Hello</div></foreignObject><script>alert(2)</script></svg>'
    const clean = sanitizeMermaidSvg(dirty)
    expect(clean).not.toContain('onerror')
    expect(clean).not.toContain('<script')
    expect(clean).toContain('<style>')
    expect(clean.toLowerCase()).toContain('<foreignobject')
  })

  it('renderSafeMarkdown adds target="_blank" rel="noopener noreferrer" and strips javascript: hrefs', () => {
    const safeHtml = renderSafeMarkdown('[OpenAI](https://openai.com)')
    expect(safeHtml).toContain('target="_blank"')
    expect(safeHtml).toContain('rel="noopener noreferrer"')

    const xssHtml = renderSafeMarkdown('[Click](javascript:alert(1))')
    expect(xssHtml.toLowerCase()).not.toContain('javascript:')
  })

  it('PasswordSettings passes plaintext passwords to auth.changePassword (no double encryption)', async () => {
    const src = readFileSync(resolve(process.cwd(), 'src/components/settings/PasswordSettings.vue'), 'utf8')
    expect(src).not.toMatch(/encryptText/)
    expect(src).toMatch(/auth\.changePassword\(form\.old_password,\s*form\.new_password\)/)
  })

  it('AppearanceSettings and SettingsMobile amount-mask switches toggle cleanly without snapping back', async () => {
    const AppearanceSettings = (await import('@/components/settings/AppearanceSettings.vue')).default
    const store = useDisplaySettingsStore()
    store.set(true)

    const wrapper = mount(AppearanceSettings, {
      global: {
        mocks: { $t: (k: string) => k },
        stubs: {
          Icon: true,
          'el-radio-group': { template: '<div><slot /></div>' },
          'el-radio': { template: '<div><slot /></div>' },
          'el-form': { template: '<form><slot /></form>' },
          'el-form-item': { template: '<div><slot /></div>' },
          'el-text': { template: '<span><slot /></span>' },
          'el-switch': {
            props: ['modelValue'],
            emits: ['change'],
            template: `<input type="checkbox" :checked="modelValue" @change="$emit('change', !modelValue)" />`,
          },
        },
      },
    })

    const checkbox = wrapper.find('input[type="checkbox"]')
    await checkbox.trigger('change')
    expect(store.showAmount).toBe(false)
    await checkbox.trigger('change')
    expect(store.showAmount).toBe(true)

    const mobileSrc = readFileSync(resolve(process.cwd(), 'src/views-mobile/SettingsMobile.vue'), 'utf8')
    expect(mobileSrc).toMatch(/:model-value="displaySettings\.showAmount"/)
    expect(mobileSrc).not.toMatch(/handleShowAmountToggle/)
  })
})
