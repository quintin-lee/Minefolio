import { describe, it, expect, vi, beforeEach } from 'vitest'
import { mount, flushPromises } from '@vue/test-utils'
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

  it('switches to 2FA challenge when login requires 2fa', async () => {
    const { useAuthStore } = await import('@/stores/auth')
    const auth = useAuthStore()
    vi.spyOn(auth, 'login').mockResolvedValueOnce({
      require_2fa: true,
      temp_token: 'temp_token_123',
    })

    const wrapper = mount(LoginMobile, {
      global: {
        stubs: {
          AppLogo: { template: '<div class="app-logo-stub" />' },
          ServerConfigDialog: { template: '<div class="server-config-dialog-stub" />' },
          'el-form': { template: '<form class="el-form"><slot /></form>' },
          'el-form-item': { template: '<div class="el-form-item"><slot /></div>' },
          'el-input': {
            props: ['modelValue', 'size'],
            emits: ['update:modelValue'],
            template: '<input :value="modelValue" @input="$emit(\'update:modelValue\', $event.target.value)" />',
          },
          'el-button': {
            emits: ['click'],
            template: '<button @click="$emit(\'click\')"><slot /></button>',
          },
          'el-icon': { template: '<i><slot /></i>' },
          'el-tag': { template: '<span><slot /></span>' },
        },
      },
    })

    const inputs = wrapper.findAll('input')
    await inputs[0].setValue('admin')
    await inputs[1].setValue('password123')

    const submitBtn = wrapper.findAll('button').find((b) => b.text().includes('登录系统'))
    await submitBtn?.trigger('click')
    await flushPromises()

    expect(wrapper.find('.two-factor-mobile-box').exists()).toBe(true)
    expect(wrapper.text()).toContain('两步验证')
  })

  it('cleans whitespace and converts backup code case when submitting 2FA', async () => {
    const { useAuthStore } = await import('@/stores/auth')
    const auth = useAuthStore()
    const verifySpy = vi.spyOn(auth, 'verify2FaLogin').mockResolvedValueOnce({ token: 'jwt_ok' } as any)

    sessionStorage.setItem('minefolio_2fa_temp_token', 'temp_token_456')

    const wrapper = mount(LoginMobile, {
      global: {
        stubs: {
          AppLogo: { template: '<div class="app-logo-stub" />' },
          ServerConfigDialog: { template: '<div class="server-config-dialog-stub" />' },
          'el-form': { template: '<form class="el-form"><slot /></form>' },
          'el-form-item': { template: '<div class="el-form-item"><slot /></div>' },
          'el-input': {
            props: ['modelValue', 'size'],
            emits: ['update:modelValue'],
            template: '<input :value="modelValue" @input="$emit(\'update:modelValue\', $event.target.value)" />',
          },
          'el-button': {
            emits: ['click'],
            template: '<button @click="$emit(\'click\')"><slot /></button>',
          },
          'el-icon': { template: '<i><slot /></i>' },
          'el-tag': { template: '<span><slot /></span>' },
        },
      },
    })
    await flushPromises()

    expect(wrapper.find('.two-factor-mobile-box').exists()).toBe(true)

    // 输入带空格与大写字母的动态码/备用码
    const codeInput = wrapper.find('input')
    await codeInput.setValue('  A1B2 - C3D4  ')

    const verifyBtn = wrapper.findAll('button').find((b) => b.text().includes('验证并登录'))
    await verifyBtn?.trigger('click')
    await flushPromises()

    expect(verifySpy).toHaveBeenCalledWith('temp_token_456', 'a1b2-c3d4')
    expect(sessionStorage.getItem('minefolio_2fa_temp_token')).toBeNull()
  })

  it('clears sessionStorage and returns to password form when clicking back', async () => {
    sessionStorage.setItem('minefolio_2fa_temp_token', 'temp_token_789')

    const wrapper = mount(LoginMobile, {
      global: {
        stubs: {
          AppLogo: { template: '<div class="app-logo-stub" />' },
          ServerConfigDialog: { template: '<div class="server-config-dialog-stub" />' },
          'el-form': { template: '<form class="el-form"><slot /></form>' },
          'el-form-item': { template: '<div class="el-form-item"><slot /></div>' },
          'el-input': {
            props: ['modelValue', 'size'],
            emits: ['update:modelValue'],
            template: '<input :value="modelValue" @input="$emit(\'update:modelValue\', $event.target.value)" />',
          },
          'el-button': {
            emits: ['click'],
            template: '<button @click="$emit(\'click\')"><slot /></button>',
          },
          'el-icon': { template: '<i><slot /></i>' },
          'el-tag': { template: '<span><slot /></span>' },
        },
      },
    })
    await flushPromises()

    expect(wrapper.find('.two-factor-mobile-box').exists()).toBe(true)

    const backBtn = wrapper.findAll('button').find((b) => b.text().includes('返回密码登录'))
    await backBtn?.trigger('click')
    await flushPromises()

    expect(wrapper.find('.two-factor-mobile-box').exists()).toBe(false)
    expect(sessionStorage.getItem('minefolio_2fa_temp_token')).toBeNull()
  })
})
