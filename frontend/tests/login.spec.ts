import { describe, it, expect, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import Login from '@/views/Login.vue'

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
    verify2FaLogin: vi.fn(),
  },
}))

describe('Login.vue', () => {
  it('renders particle canvas, hud-frame, and form inputs correctly without blocking layout', async () => {
    setActivePinia(createPinia())

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
})
