// @vitest-environment jsdom
import { describe, it, expect, vi, beforeEach } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import ElementPlus from 'element-plus'
import AiMcpManager from '@/components/settings/AiMcpManager.vue'
import AiProviderManager from '@/components/settings/AiProviderManager.vue'
import Settings from '@/views/Settings.vue'
import { useAuthStore } from '@/stores/auth'

const mockAiSettings = {
  providers: [
    {
      id: "Agnes",
      name: "agnes",
      base_url: "https://apihub.agnes-ai.com/v1",
      has_api_key: true,
      models: [
        "agnes-2.5-pro-alpha",
        "agnes-video-2.5-flash",
        "agnes-3.0-flash",
        "agnes-image-2.1-flash",
        "agnes-video-2.5",
        "agnes-2.0-flash",
        "agnes-image-2.0-flash",
        "agnes-2.5-flash",
        "agnes-2.5-pro",
        "agnes-2.5-pro-beta",
        "agnes-video-v2.0",
        "agnes-image-2.5-flash"
      ]
    }
  ],
  default_provider: "Agnes",
  default_model: "agnes-3.0-flash",
  context_size: 20,
  system_prompt: ""
}

vi.mock('@/api/ai-mcp', () => ({
  listMcpServers: vi.fn().mockImplementation(() => Promise.resolve([])),
  listMcpScripts: vi.fn().mockImplementation(() => Promise.resolve([])),
  createMcpServer: vi.fn(),
  updateMcpServer: vi.fn(),
  deleteMcpServer: vi.fn(),
  getMcpServerTools: vi.fn().mockResolvedValue([]),
  testMcpServer: vi.fn(),
  refreshMcpServer: vi.fn(),
  uploadMcpScript: vi.fn(),
  deleteMcpScript: vi.fn(),
}))

vi.mock('@/api/ai', () => ({
  getSettings: vi.fn().mockImplementation(() => Promise.resolve(mockAiSettings)),
  updateSettings: vi.fn(),
  testAiConnection: vi.fn(),
  fetchAiModels: vi.fn(),
}))

vi.mock('@/api/system', () => ({
  systemApi: {
    status: vi.fn().mockResolvedValue({ version: '1.3.1', is_initialized: true }),
  },
}))

vi.mock('@/api/ledger', () => ({
  ledgerApi: {
    list: vi.fn().mockResolvedValue([]),
  },
}))

describe('Settings.vue AI Tab & MCP Manager', () => {
  beforeEach(() => {
    localStorage.clear()
    localStorage.setItem('minefolio_token', 'test-token')
    localStorage.setItem('minefolio_user', JSON.stringify({ id: 1, username: 'admin', created_at: '2026-01-01' }))
  })

  it('mounts AiMcpManager directly with empty servers', async () => {
    setActivePinia(createPinia())
    const wrapper = mount(AiMcpManager, {
      global: {
        plugins: [ElementPlus],
      },
    })
    await new Promise((r) => setTimeout(r, 50))
    expect(wrapper.exists()).toBe(true)
  })

  it('mounts AiProviderManager directly with realistic settings', async () => {
    setActivePinia(createPinia())
    const wrapper = mount(AiProviderManager, {
      global: {
        plugins: [ElementPlus],
      },
    })
    await new Promise((r) => setTimeout(r, 50))
    expect(wrapper.exists()).toBe(true)
  })

  it('clicks addServer and renders add card', async () => {
    setActivePinia(createPinia())
    const wrapper = mount(AiMcpManager, {
      global: {
        plugins: [ElementPlus],
      },
    })
    await new Promise((r) => setTimeout(r, 50))
    const addBtn = wrapper.find('.add-server-btn')
    expect(addBtn.exists()).toBe(true)
    await addBtn.trigger('click')
    await wrapper.vm.$nextTick()
    expect(wrapper.find('.server-form-wrap').exists()).toBe(true)
  })

  it('mounts Settings.vue, switches to AI tab, and checks for error capture', async () => {
    setActivePinia(createPinia())
    const auth = useAuthStore()
    auth.token = 'test-token'
    auth.user = { id: 1, username: 'admin', created_at: '2026-01-01', role: 'admin' } as any

    const capturedErrors: any[] = []
    const wrapper = mount(Settings, {
      global: {
        plugins: [ElementPlus],
        config: {
          errorHandler: (err) => {
            capturedErrors.push(err)
          }
        }
      },
    })

    expect(wrapper.exists()).toBe(true)

    // Switch to AI tab
    wrapper.vm.activeTab = 'ai'
    await wrapper.vm.$nextTick()
    await new Promise((r) => setTimeout(r, 100))

    if (capturedErrors.length > 0) {
      console.error('CAPTURED ERRORS IN TEST:', capturedErrors)
    }
  })
})
