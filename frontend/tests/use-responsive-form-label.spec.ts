import { describe, expect, it, vi, afterEach } from 'vitest'
import { defineComponent } from 'vue'
import { mount } from '@vue/test-utils'
import { useResponsiveFormLabel } from '@/composables/useResponsiveFormLabel'

describe('useResponsiveFormLabel', () => {
  const originalMatchMedia = window.matchMedia

  afterEach(() => {
    window.matchMedia = originalMatchMedia
    vi.restoreAllMocks()
  })

  function mountWithComposable() {
    let state!: ReturnType<typeof useResponsiveFormLabel>
    const Comp = defineComponent({
      setup() {
        state = useResponsiveFormLabel()
        return state
      },
      template: '<div>{{ labelPosition }}</div>',
    })
    const wrapper = mount(Comp)
    return { wrapper, state }
  }

  it('defaults to right on desktop viewports and updates dynamically on change', async () => {
    let listener: ((e: MediaQueryListEvent) => void) | null = null
    const addEventListener = vi.fn((_ev: string, cb: (e: MediaQueryListEvent) => void) => {
      listener = cb
    })
    const removeEventListener = vi.fn()

    window.matchMedia = vi.fn().mockReturnValue({
      matches: false,
      media: '(max-width: 640px)',
      addEventListener,
      removeEventListener,
    } as unknown as MediaQueryList)

    const { wrapper, state } = mountWithComposable()
    expect(state.isCompact.value).toBe(false)
    expect(state.labelPosition.value).toBe('right')
    expect(wrapper.text()).toBe('right')
    expect(addEventListener).toHaveBeenCalledWith('change', expect.any(Function))

    listener?.({ matches: true } as MediaQueryListEvent)
    await wrapper.vm.$nextTick()
    expect(state.isCompact.value).toBe(true)
    expect(state.labelPosition.value).toBe('top')
    expect(wrapper.text()).toBe('top')

    wrapper.unmount()
    expect(removeEventListener).toHaveBeenCalledWith('change', expect.any(Function))
  })

  it('starts in top label mode when initial viewport matches narrow breakpoint and supports legacy listeners', () => {
    const addListener = vi.fn()
    const removeListener = vi.fn()

    window.matchMedia = vi.fn().mockReturnValue({
      matches: true,
      media: '(max-width: 640px)',
      addListener,
      removeListener,
    } as unknown as MediaQueryList)

    const { wrapper, state } = mountWithComposable()
    expect(state.isCompact.value).toBe(true)
    expect(state.labelPosition.value).toBe('top')
    expect(addListener).toHaveBeenCalledTimes(1)

    wrapper.unmount()
    expect(removeListener).toHaveBeenCalledTimes(1)
  })

  it('falls back safely to right when window.matchMedia is unavailable', () => {
    // @ts-expect-error testing non-browser environment fallback
    window.matchMedia = undefined
    const { wrapper, state } = mountWithComposable()
    expect(state.isCompact.value).toBe(false)
    expect(state.labelPosition.value).toBe('right')
    wrapper.unmount()
  })
})
