import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import AppLogo from '@/components/AppLogo.vue'

describe('AppLogo.vue', () => {
  it('renders SVG icon with default size', () => {
    const wrapper = mount(AppLogo)
    const svg = wrapper.find('svg')
    expect(svg.exists()).toBe(true)
    expect(svg.attributes('viewBox')).toBe('0 0 512 512')
    expect(wrapper.find('.logo-title').exists()).toBe(false)
  })

  it('renders brand text when withText is true', () => {
    const wrapper = mount(AppLogo, {
      props: {
        withText: true,
        size: 36,
      },
    })
    const title = wrapper.find('.logo-title')
    expect(title.exists()).toBe(true)
    expect(title.text()).toBe('Minefolio')
  })

  it('applies custom size style', () => {
    const wrapper = mount(AppLogo, {
      props: {
        size: 48,
      },
    })
    const iconWrapper = wrapper.find('.app-logo-icon')
    expect(iconWrapper.attributes('style')).toContain('48px')
  })
})
