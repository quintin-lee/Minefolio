import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import ChatMessageContent from '@/components/ChatMessageContent.vue'

describe('ChatMessageContent panel extraction', () => {
  it('renders a compact panel-ref button when a segment is marked as extracted', async () => {
    const content = '```mermaid\npie title 资产\n "现金": 100\n```'
    const wrapper = mount(ChatMessageContent, {
      props: {
        messageId: 10,
        content,
        isStreaming: false,
        extractedItemIds: ['mmd-0']
      },
      global: {
        stubs: {
          MermaidBlock: true,
          CodeBlock: true,
          ActionCard: true,
          WorkflowConfigCard: true,
          WorkflowProgressCard: true,
          Icon: true
        }
      }
    })
    const refBtn = wrapper.find('.panel-ref-pill')
    expect(refBtn.exists()).toBe(true)
    await refBtn.trigger('click')
    expect(wrapper.emitted('open-panel-item')).toBeTruthy()
  })

  it('keeps normal inline rendering when isStreaming is true even if IDs provided', () => {
    const content = '```mermaid\npie title 资产\n "现金": 100\n```'
    const wrapper = mount(ChatMessageContent, {
      props: {
        messageId: 10,
        content,
        isStreaming: true,
        extractedItemIds: ['mmd-0']
      },
      global: {
        stubs: {
          MermaidBlock: true,
          CodeBlock: true,
          ActionCard: true,
          WorkflowConfigCard: true,
          WorkflowProgressCard: true,
          Icon: true
        }
      }
    })
    expect(wrapper.find('.panel-ref-pill').exists()).toBe(false)
  })
})
