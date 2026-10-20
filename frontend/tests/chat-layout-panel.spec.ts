import { describe, it, expect, beforeEach } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import Chat from '@/views/Chat.vue'
import AnalysisPanel from '@/components/AnalysisPanel.vue'
import { useChatStore } from '@/stores/chat'

describe('Chat.vue with AnalysisPanel integration', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
  })

  it('mounts without crashing and contains analysis panel element', () => {
    const wrapper = mount(Chat, {
      global: {
        stubs: {
          ChatMessageContent: true,
          PromptStarters: true,
          WorkflowBar: true,
          WorkflowSlashMenu: true,
          Icon: true,
          ElSelect: true,
          ElOption: true,
          ElButton: true
        }
      }
    })
    const panel = wrapper.findComponent(AnalysisPanel)
    expect(panel.exists()).toBe(true)
    expect(panel.props('visible')).toBe(false)
  })

  it('resets panel when session is switched', async () => {
    const chatStore = useChatStore()
    const wrapper = mount(Chat, {
      global: {
        stubs: {
          ChatMessageContent: true,
          PromptStarters: true,
          WorkflowBar: true,
          WorkflowSlashMenu: true,
          Icon: true,
          ElSelect: true,
          ElOption: true,
          ElButton: true
        }
      }
    })

    // 模拟会话切换
    chatStore.currentSessionId = 999
    await wrapper.vm.$nextTick()

    const panel = wrapper.findComponent(AnalysisPanel)
    expect(panel.props('visible')).toBe(false)
    expect(panel.props('items')).toEqual([])
  })
})
