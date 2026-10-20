import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import AnalysisPanel from '@/components/AnalysisPanel.vue'
import type { PanelItem } from '@/types/chat-panel'

describe('AnalysisPanel.vue', () => {
  const dummyItems: PanelItem[] = [
    {
      id: 'item-1',
      label: '财务图表 #1',
      icon: 'ph:chart-pie',
      type: 'mermaid',
      content: 'pie title 测试\n "A": 10',
      createdAt: Date.now()
    },
    {
      id: 'item-2',
      label: '记账确认',
      icon: 'ph:receipt',
      type: 'action',
      content: '{"action_type":"daily_expense"}',
      actionData: { action_type: 'daily_expense', amount: 20 } as any,
      createdAt: Date.now()
    }
  ]

  it('renders panel header and tabs when items exist', () => {
    const wrapper = mount(AnalysisPanel, {
      props: {
        visible: true,
        items: dummyItems,
        activeTabId: 'item-1',
        overlay: false,
        width: 420
      },
      global: {
        stubs: {
          MermaidBlock: true,
          ActionCard: true,
          CodeBlock: true,
          WorkflowProgressCard: true,
          WorkflowConfigCard: true,
          Icon: true,
          'el-tabs': false,
          'el-tab-pane': false
        }
      }
    })
    expect(wrapper.find('.panel-title').text()).toContain('分析结果')
    expect(wrapper.text()).toContain('财务图表 #1')
  })

  it('emits close event when clicking close button', async () => {
    const wrapper = mount(AnalysisPanel, {
      props: {
        visible: true,
        items: dummyItems,
        activeTabId: 'item-1',
        overlay: false
      },
      global: {
        stubs: {
          MermaidBlock: true,
          ActionCard: true,
          CodeBlock: true,
          WorkflowProgressCard: true,
          WorkflowConfigCard: true,
          Icon: true
        }
      }
    })
    const closeBtn = wrapper.find('.panel-close-btn')
    expect(closeBtn.exists()).toBe(true)
    await closeBtn.trigger('click')
    expect(wrapper.emitted('close')).toBeTruthy()
  })

  it('shows empty state when items list is empty', () => {
    const wrapper = mount(AnalysisPanel, {
      props: {
        visible: true,
        items: [],
        activeTabId: '',
        overlay: false
      },
      global: {
        stubs: { Icon: true }
      }
    })
    expect(wrapper.find('.panel-empty-state').exists()).toBe(true)
  })

  it('applies overlay class when overlay prop is true', () => {
    const wrapper = mount(AnalysisPanel, {
      props: {
        visible: true,
        items: dummyItems,
        activeTabId: 'item-1',
        overlay: true
      },
      global: {
        stubs: { Icon: true, MermaidBlock: true, ActionCard: true }
      }
    })
    expect(wrapper.classes()).toContain('overlay')
  })
})
