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

  it('toggles expansion state when clicking thinking header on completed message', async () => {
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

    // 已有正文的历史消息：默认收起以释放垂直阅读空间
    expect(wrapper.find('.thinking-content-body').exists()).toBe(false)

    // 点击展开
    await header.trigger('click')
    expect(wrapper.find('.thinking-content-body').exists()).toBe(true)
    expect(wrapper.find('.thinking-content-body').text()).toContain('这是思考分析过程')

    // 再次点击收起
    await header.trigger('click')
    expect(wrapper.find('.thinking-content-body').exists()).toBe(false)
  })

  it('auto-expands during streaming thinking and auto-collapses when content starts streaming', async () => {
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

    // 思考中阶段且无正文：默认实时展开
    expect(wrapper.find('.thinking-content-body').exists()).toBe(true)
    expect(wrapper.find('.thinking-content-body').text()).toContain('实时推理中...')

    // 正文开始吐字：自动平滑收起为紧凑状态
    await wrapper.setProps({ content: '你好，我是 Minefolio 助手' })
    expect(wrapper.find('.thinking-content-body').exists()).toBe(false)
    expect(wrapper.find('.thinking-status').text()).toBe('已深度思考')
  })

  it('respects manual user toggle even when content starts streaming', async () => {
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

    const header = wrapper.find('.thinking-header')
    // 用户主动点击收起再点击展开（人工固定展开）
    await header.trigger('click')
    expect(wrapper.find('.thinking-content-body').exists()).toBe(false)
    await header.trigger('click')
    expect(wrapper.find('.thinking-content-body').exists()).toBe(true)

    // 此时正文到达，用户手动选择优先级最高，依然保持展开
    await wrapper.setProps({ content: '正文生成开始' })
    expect(wrapper.find('.thinking-content-body').exists()).toBe(true)
  })
})
