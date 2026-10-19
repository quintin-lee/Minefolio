import { describe, it, expect, vi, beforeEach } from 'vitest'
import { mount } from '@vue/test-utils'
import { setActivePinia, createPinia } from 'pinia'
import CommandPalette from '@/components/CommandPalette.vue'

// Mock vue-router
const pushMock = vi.fn()
vi.mock('vue-router', () => ({
  useRouter: () => ({
    push: pushMock,
  }),
}))

describe('CommandPalette.vue', () => {
  beforeEach(() => {
    Object.defineProperty(window, 'matchMedia', {
      writable: true,
      value: vi.fn().mockImplementation((query) => ({
        matches: false,
        media: query,
        onchange: null,
        addListener: vi.fn(),
        removeListener: vi.fn(),
        addEventListener: vi.fn(),
        removeEventListener: vi.fn(),
        dispatchEvent: vi.fn(),
      })),
    })
    setActivePinia(createPinia())
    pushMock.mockClear()
  })

  it('renders command palette when modelValue is true', () => {
    const wrapper = mount(CommandPalette, {
      props: {
        modelValue: true,
      },
      global: {
        stubs: {
          Icon: { template: '<span class="mock-icon" />' },
        },
      },
    })

    expect(wrapper.find('.command-palette-dialog').exists()).toBe(true)
    expect(wrapper.find('input.command-search-input').exists()).toBe(true)
  })

  it('filters commands by keyword', async () => {
    const wrapper = mount(CommandPalette, {
      props: {
        modelValue: true,
      },
      global: {
        stubs: {
          Icon: { template: '<span class="mock-icon" />' },
        },
      },
    })

    const input = wrapper.find('input.command-search-input')
    await input.setValue('资产')

    const items = wrapper.findAll('.command-item')
    expect(items.length).toBeGreaterThan(0)
    // All filtered items should be related to assets
    const texts = items.map((i) => i.text())
    expect(texts.some((t) => t.includes('资产') || t.includes('持仓'))).toBe(true)
  })

  it('navigates when a navigation command is clicked', async () => {
    const wrapper = mount(CommandPalette, {
      props: {
        modelValue: true,
      },
      global: {
        stubs: {
          Icon: { template: '<span class="mock-icon" />' },
        },
      },
    })

    const input = wrapper.find('input.command-search-input')
    await input.setValue('报表')

    const reportItem = wrapper.findAll('.command-item').find((i) => i.text().includes('报表'))
    expect(reportItem).toBeDefined()
    await reportItem!.trigger('click')

    expect(pushMock).toHaveBeenCalledWith('/reports')
    expect(wrapper.emitted('update:modelValue')?.[0]).toEqual([false])
  })

  it('emits quick-record event when quick record action is selected', async () => {
    const wrapper = mount(CommandPalette, {
      props: {
        modelValue: true,
      },
      global: {
        stubs: {
          Icon: { template: '<span class="mock-icon" />' },
        },
      },
    })

    const input = wrapper.find('input.command-search-input')
    await input.setValue('记账')

    const quickRecordItem = wrapper.findAll('.command-item').find((i) => i.text().includes('记账'))
    expect(quickRecordItem).toBeDefined()
    await quickRecordItem!.trigger('click')

    expect(wrapper.emitted('quick-record')).toBeTruthy()
    expect(wrapper.emitted('update:modelValue')?.[0]).toEqual([false])
  })
})
