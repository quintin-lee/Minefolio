import { describe, it, expect, vi, beforeEach } from 'vitest'
import { mount, flushPromises } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import { ElMessage } from 'element-plus'
import Login from '@/views/Login.vue'

const pushMock = vi.fn()
const replaceMock = vi.fn()

// Mock router
vi.mock('vue-router', () => ({
  useRouter: () => ({
    push: pushMock,
    replace: replaceMock,
  }),
}))

// Mock authApi
vi.mock('@/api/auth', () => ({
  authApi: {
    getOAuthProviders: vi.fn().mockResolvedValue({ providers: [] }),
    login: vi.fn(),
    register: vi.fn(),
    verify2FaLogin: vi.fn(),
  },
}))

describe('Login.vue', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
    pushMock.mockResolvedValue(undefined)
  })

  it('renders particle canvas, hud-frame, and form inputs correctly without blocking layout', async () => {
    const wrapper = mount(Login, {
      global: {
        stubs: {
          'el-card': { template: '<div class="el-card"><slot name="header" /><slot /></div>' },
          'el-form': { template: '<form class="el-form"><slot /></form>' },
          'el-form-item': { template: '<div class="el-form-item"><slot /></div>' },
          'el-input': {
            props: ['modelValue', 'type', 'placeholder', 'size', 'prefixIcon', 'showPassword'],
            emits: ['update:modelValue'],
            template: '<input :value="modelValue" :type="type" :placeholder="placeholder" @input="$emit(\'update:modelValue\', $event.target.value)" />',
          },
          'el-button': { template: '<button><slot /></button>' },
          'el-icon': { template: '<i><slot /></i>' },
        },
      },
    })

    // Verify canvas exists
    const canvas = wrapper.find('canvas.particle-canvas')
    expect(canvas.exists()).toBe(true)

    // Verify HUD frame and login card exist
    const hudFrame = wrapper.find('.hud-frame')
    expect(hudFrame.exists()).toBe(true)

    const loginCard = wrapper.find('.login-card')
    expect(loginCard.exists()).toBe(true)

    // Verify input fields can be typed into
    const inputs = wrapper.findAll('input')
    expect(inputs.length).toBeGreaterThanOrEqual(2)

    await inputs[0].setValue('admin')
    expect(wrapper.vm.form.username).toBe('admin')

    await inputs[1].setValue('password123')
    expect(wrapper.vm.form.password).toBe('password123')
  })

  it('prevents duplicate 2FA submissions while loading and does not misreport navigation failure as wrong 2FA code', async () => {
    const { useAuthStore } = await import('@/stores/auth')
    const auth = useAuthStore()

    let resolveVerify!: (v: any) => void
    const verifySpy = vi.spyOn(auth, 'verify2FaLogin').mockImplementation(
      () => new Promise((resolve) => { resolveVerify = resolve })
    )
    const errSpy = vi.spyOn(ElMessage, 'error').mockImplementation(() => ({} as any))
    const okSpy = vi.spyOn(ElMessage, 'success').mockImplementation(() => ({} as any))

    pushMock.mockRejectedValueOnce(new TypeError('Failed to fetch dynamically imported module'))

    const assignSpy = vi.fn()
    Object.defineProperty(window, 'location', {
      configurable: true,
      value: { ...window.location, assign: assignSpy },
    })

    const wrapper = mount(Login, {
      global: {
        stubs: {
          'el-card': { template: '<div class="el-card"><slot name="header" /><slot /></div>' },
          'el-form': { template: '<form class="el-form" @submit.prevent><slot /></form>' },
          'el-form-item': { template: '<div class="el-form-item"><slot /></div>' },
          'el-input': {
            props: ['modelValue', 'type', 'placeholder', 'size', 'prefixIcon', 'showPassword'],
            emits: ['update:modelValue', 'keyup'],
            template: '<input :value="modelValue" @input="$emit(\'update:modelValue\', $event.target.value)" @keyup.enter="$emit(\'keyup\', $event)" />',
          },
          'el-button': {
            emits: ['click'],
            template: '<button @click="$emit(\'click\')"><slot /></button>',
          },
          'el-icon': { template: '<i><slot /></i>' },
        },
      },
    })

    ;(wrapper.vm as any).isTwoFactorStep = true
    ;(wrapper.vm as any).tempToken = 'temp_tok_123'
    ;(wrapper.vm as any).twoFactorCode = '704491'
    await flushPromises()

    // 连续触发两次提交（模拟 Enter 同时触发或双击），应仅发起一次请求
    const p1 = (wrapper.vm as any).handle2FaSubmit()
    const p2 = (wrapper.vm as any).handle2FaSubmit()
    expect(verifySpy).toHaveBeenCalledTimes(1)

    resolveVerify({ token: 'jwt_ok' })
    await Promise.all([p1, p2])
    await flushPromises()

    expect(okSpy).toHaveBeenCalledTimes(1)
    expect(errSpy).not.toHaveBeenCalled()
    expect(assignSpy).toHaveBeenCalledWith('/dashboard')
  })
})
