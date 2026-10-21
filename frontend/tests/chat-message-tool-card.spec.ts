import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import ChatMessageContent from '@/components/ChatMessageContent.vue'

const baseStubs = {
  MermaidBlock: true,
  CodeBlock: true,
  ActionCard: true,
  WorkflowConfigCard: true,
  WorkflowProgressCard: true,
  Icon: true,
}

describe('ChatMessageContent tool-call card', () => {
  it('renders collapsed tool card for a completed tool call on historical message', () => {
    const wrapper = mount(ChatMessageContent, {
      props: {
        messageId: 1,
        content: '这是回答正文',
        toolCalls: [{ id: 't1', name: 'web_search', args: '{"q":"x"}', result: '搜索结果...' }],
        isStreaming: false,
      },
      global: { stubs: baseStubs },
    })

    const card = wrapper.find('.tool-card')
    expect(card.exists()).toBe(true)
    expect(wrapper.find('.tool-card-header').text()).toContain('已调用 1 个工具')
    // 历史消息：默认折叠
    expect(wrapper.find('.tool-card-body').exists()).toBe(false)
  })

  it('expands on header click and shows tool name, args and result', async () => {
    const wrapper = mount(ChatMessageContent, {
      props: {
        messageId: 1,
        content: '这是回答正文',
        toolCalls: [{ id: 't1', name: 'web_search', args: '{"q":"x"}', result: '搜索结果...' }],
        isStreaming: false,
      },
      global: { stubs: baseStubs },
    })

    const header = wrapper.find('.tool-card-header')
    expect(header.exists()).toBe(true)

    await header.trigger('click')
    const body = wrapper.find('.tool-card-body')
    expect(body.exists()).toBe(true)
    expect(body.find('.tool-call-name').text()).toBe('web_search')
    expect(body.find('.tool-call-args').text()).toBe('{"q":"x"}')
    expect(body.find('.tool-call-result').text()).toBe('搜索结果...')

    // 再次点击收起
    await header.trigger('click')
    expect(wrapper.find('.tool-card-body').exists()).toBe(false)
  })

  it('auto-expands during streaming with a pending tool and shows 执行中 badge', () => {
    const wrapper = mount(ChatMessageContent, {
      props: {
        messageId: 1,
        content: '',
        toolCalls: [{ id: 't1', name: 'web_search', args: '{"q":"x"}' }],
        isStreaming: true,
      },
      global: { stubs: baseStubs },
    })

    // 流式中且有待完成工具：默认展开
    expect(wrapper.find('.tool-card-body').exists()).toBe(true)
    expect(wrapper.find('.tool-item-pending').text()).toBe('执行中...')
    expect(wrapper.find('.tool-card-header').text()).toContain('正在调用工具...')
  })

  it('does not render tool card when toolCalls is empty or absent', () => {
    const withEmpty = mount(ChatMessageContent, {
      props: {
        messageId: 1,
        content: '这是回答正文',
        toolCalls: [],
        isStreaming: false,
      },
      global: { stubs: baseStubs },
    })
    expect(withEmpty.find('.tool-card').exists()).toBe(false)

    const without = mount(ChatMessageContent, {
      props: {
        messageId: 1,
        content: '这是回答正文',
        isStreaming: false,
      },
      global: { stubs: baseStubs },
    })
    expect(without.find('.tool-card').exists()).toBe(false)
  })
})
