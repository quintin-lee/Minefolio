import { describe, it, expect, vi, beforeEach } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import LoginMobile from '@/views-mobile/LoginMobile.vue'
import { setServerUrl, resetServerUrl, STORAGE_KEY_SERVER_URL } from '@/utils/http'

// Mock router
vi.mock('vue-router', () => ({
  useRouter: () => ({
    push: vi.fn(),
    replace: vi.fn(),
  }),
}))

// Mock authApi
vi.mock('@/api/auth', () => ({
  authApi: {
    getOAuthProviders: vi.fn().mockResolvedValue({ providers: [] }),
    login: vi.fn(),
    register: vi.fn(),
  },
}))

describe('LoginMobile.vue', () => {
  beforeEach(() => {
    localStorage.removeItem(STORAGE_KEY_SERVER_URL)
    resetServerUrl()
    setActivePinia(createPinia())
  })

  it('renders brand header, server bar and login form', () => {
    const wrapper = mount(LoginMobile, {
      global: {
        stubs: {
          AppLogo: { template: '<div class="app-logo-stub" />' },
          ServerConfigDialog: { template: '<div class="server-config-dialog-stub" />' },
          'el-form': { template: '<form class="el-form"><slot /></form>' },
          'el-form-item': { template: '<div class="el-form-item"><slot /></div>' },
          'el-input': {
            props: ['modelValue'],
            template: '<input :value="modelValue" />',
          },
          'el-button': { template: '<button><slot /></button>' },
          'el-icon': { template: '<i><slot /></i>' },
          'el-tag': { template: '<span><slot /></span>' },
        },
      },
    })

    expect(wrapper.find('.brand-header').exists()).toBe(true)
    const serverBar = wrapper.find('.server-bar')
    expect(serverBar.exists()).toBe(true)
    expect(serverBar.text()).toContain('服务端：')
  })

  it('displays custom server address when configured', async () => {
    setServerUrl('http://192.168.1.99:8080')

    const wrapper = mount(LoginMobile, {
      global: {
        stubs: {
          AppLogo: { template: '<div class="app-logo-stub" />' },
          ServerConfigDialog: { template: '<div class="server-config-dialog-stub" />' },
          'el-form': { template: '<form class="el-form"><slot /></form>' },
          'el-form-item': { template: '<div class="el-form-item"><slot /></div>' },
          'el-input': { template: '<input />' },
          'el-button': { template: '<button><slot /></button>' },
          'el-icon': { template: '<i><slot /></i>' },
          'el-tag': { template: '<span><slot /></span>' },
        },
      },
    })

    const serverBar = wrapper.find('.server-bar')
    expect(serverBar.text()).toContain('http://192.168.1.99:8080')
  })
})
