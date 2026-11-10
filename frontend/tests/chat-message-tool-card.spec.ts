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
  it('renders collapsed tool card with steps preview for a completed tool call on historical message', () => {
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
    // 头部包含步骤预览胶囊
    const preview = wrapper.find('.tool-steps-preview')
    expect(preview.exists()).toBe(true)
    expect(preview.text()).toContain('全网实时检索')
    // 历史消息：默认折叠主体
    expect(wrapper.find('.tool-card-body').exists()).toBe(false)
  })

  it('expands on header click and shows tool timeline step with semantic label and accordion toggle', async () => {
    const wrapper = mount(ChatMessageContent, {
      props: {
        messageId: 1,
        content: '这是回答正文',
        toolCalls: [
          { id: 't1', name: 'web_search', args: '{"q":"x"}', result: '搜索结果...' },
          { id: 't2', name: 'query_assets', args: '{"currency":"CNY"}', result: '{"total":100}' },
        ],
        isStreaming: false,
      },
      global: { stubs: baseStubs },
    })

    const header = wrapper.find('.tool-card-header')
    expect(header.exists()).toBe(true)

    await header.trigger('click')
    const body = wrapper.find('.tool-card-body')
    expect(body.exists()).toBe(true)

    const steps = body.findAll('.tool-timeline-step')
    expect(steps.length).toBe(2)

    // 第一步：语义化名称为 "全网实时检索"，同时保留技术标识类名 .tool-call-name
    expect(steps[0].find('.tool-step-label').text()).toBe('全网实时检索')
    expect(steps[0].find('.tool-call-name').text()).toBe('web_search')

    // 默认单步详情（参数与返回值）折叠，不一次性平铺
    expect(steps[0].find('.tool-call-args').exists()).toBe(false)
    expect(steps[0].find('.tool-call-result').exists()).toBe(false)

    // 点击第一步的展开按钮/头部
    const toggleBtn = steps[0].find('.tool-step-toggle')
    expect(toggleBtn.exists()).toBe(true)
    await toggleBtn.trigger('click')

    // 第一步展开参数与结果
    expect(steps[0].find('.tool-call-args').exists()).toBe(true)
    expect(steps[0].find('.tool-call-args').text()).toBe('{"q":"x"}')
    expect(steps[0].find('.tool-call-result').exists()).toBe(true)
    expect(steps[0].find('.tool-call-result').text()).toBe('搜索结果...')

    // 第二步依然保持独立折叠
    expect(steps[1].find('.tool-call-args').exists()).toBe(false)

    // 再次点击折叠第一步
    await toggleBtn.trigger('click')
    expect(steps[0].find('.tool-call-args').exists()).toBe(false)

    // 再次点击卡片头部收起整体卡片
    await header.trigger('click')
    expect(wrapper.find('.tool-card-body').exists()).toBe(false)
  })

  it('formats fallback label for unknown tool name cleanly', async () => {
    const wrapper = mount(ChatMessageContent, {
      props: {
        messageId: 1,
        content: '这是回答正文',
        toolCalls: [{ id: 't1', name: 'calculate_portfolio_risk_beta', args: '{}', result: '1.2' }],
        isStreaming: false,
      },
      global: { stubs: baseStubs },
    })

    await wrapper.find('.tool-card-header').trigger('click')
    const step = wrapper.find('.tool-timeline-step')
    expect(step.exists()).toBe(true)
    // 未知工具名转为首字母大写可读格式
    expect(step.find('.tool-step-label').text()).toBe('Calculate Portfolio Risk Beta')
    expect(step.find('.tool-call-name').text()).toBe('calculate_portfolio_risk_beta')
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
