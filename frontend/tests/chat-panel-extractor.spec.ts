import { describe, it, expect } from 'vitest'
import { extractPanelItems } from '@/utils/chat-panel-extractor'

describe('chat-panel-extractor', () => {
  it('extracts mermaid block into panel item', () => {
    const content = '分析如下：\n```mermaid\npie title 资产\n "现金" : 50\n```\n以上是占比。'
    const result = extractPanelItems(101, content)
    expect(result.panelItems.length).toBe(1)
    expect(result.panelItems[0].type).toBe('mermaid')
    expect(result.panelItems[0].label).toContain('图表')
    expect(result.panelItems[0].content).toContain('pie title 资产')
  })

  it('extracts markdown table into panel item', () => {
    const tableContent = '这是收支表：\n| 项目 | 金额 |\n| --- | --- |\n| 餐饮 | 100 |\n总结完毕。'
    const result = extractPanelItems(102, tableContent)
    expect(result.panelItems.some(i => i.type === 'markdown')).toBe(true)
    const tableItem = result.panelItems.find(i => i.type === 'markdown')
    expect(tableItem?.label).toContain('表格')
  })

  it('does not extract simple conversational text without tables or code', () => {
    const plain = '你好！这是关于你财务状况的简短分析，请注意控制开支。'
    const result = extractPanelItems(103, plain)
    expect(result.panelItems.length).toBe(0)
  })

  it('extracts action card JSON blocks', () => {
    const actionContent = '请确认以下记账：\n```action\n{"action_type":"daily_expense","amount":35,"category_name":"餐饮"}\n```'
    const result = extractPanelItems(104, actionContent)
    expect(result.panelItems.length).toBe(1)
    expect(result.panelItems[0].type).toBe('action')
    expect(result.panelItems[0].label).toContain('记账')
    expect(result.panelItems[0].actionData?.action_type).toBe('daily_expense')
  })

  it('extracts code block into panel item', () => {
    const codeContent = '```typescript\nconst a = 1;\n```'
    const result = extractPanelItems(105, codeContent)
    expect(result.panelItems.length).toBe(1)
    expect(result.panelItems[0].type).toBe('code')
    expect(result.panelItems[0].lang).toBe('typescript')
  })

  it('extracts workflow cards when provided in params', () => {
    const wfProgress = { workflow_id: 'wf_health_score', status: 'completed', steps: [] } as any
    const result = extractPanelItems(106, '已执行工作流', wfProgress)
    expect(result.panelItems.some(i => i.type === 'workflow-progress')).toBe(true)
  })
})
