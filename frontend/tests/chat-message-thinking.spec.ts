import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import ChatMessageContent from '@/components/ChatMessageContent.vue'

describe('ChatMessageContent thinking mode card', () => {
  it('renders thinking card when thinkingContent is provided', () => {
    const wrapper = mount(ChatMessageContent, {
      props: {
        messageId: 1,
        content: '这是回答正文',
        thinkingContent: '这是思考分析过程',
        isStreaming: false,
      },
      global: {
        stubs: {
          MermaidBlock: true,
          CodeBlock: true,
          ActionCard: true,
          WorkflowConfigCard: true,
          WorkflowProgressCard: true,
          Icon: true,
        },
      },
    })

    const card = wrapper.find('.thinking-card')
    expect(card.exists()).toBe(true)
    expect(wrapper.find('.thinking-header').text()).toContain('深度思考')
  })

  it('toggles expansion state when clicking thinking header', async () => {
    const wrapper = mount(ChatMessageContent, {
      props: {
        messageId: 1,
        content: '这是回答正文',
        thinkingContent: '这是思考分析过程',
        isStreaming: false,
      },
      global: {
        stubs: {
          MermaidBlock: true,
          CodeBlock: true,
          ActionCard: true,
          WorkflowConfigCard: true,
          WorkflowProgressCard: true,
          Icon: true,
        },
      },
    })

    const header = wrapper.find('.thinking-header')
    expect(header.exists()).toBe(true)

    // 默认保持展开，不折叠
    expect(wrapper.find('.thinking-content-body').exists()).toBe(true)
    expect(wrapper.find('.thinking-content-body').text()).toContain('这是思考分析过程')

    // 点击收起
    await header.trigger('click')
    expect(wrapper.find('.thinking-content-body').exists()).toBe(false)

    // 再次点击展开
    await header.trigger('click')
    expect(wrapper.find('.thinking-content-body').exists()).toBe(true)
  })

  it('auto-expands thinking card during streaming thinking phase', () => {
    const wrapper = mount(ChatMessageContent, {
      props: {
        messageId: 1,
        content: '',
        thinkingContent: '实时推理中...',
        isStreaming: true,
      },
      global: {
        stubs: {
          MermaidBlock: true,
          CodeBlock: true,
          ActionCard: true,
          WorkflowConfigCard: true,
          WorkflowProgressCard: true,
          Icon: true,
        },
      },
    })

    // 思考中阶段：默认实时展开
    expect(wrapper.find('.thinking-content-body').exists()).toBe(true)
    expect(wrapper.find('.thinking-content-body').text()).toContain('实时推理中...')
  })
})
