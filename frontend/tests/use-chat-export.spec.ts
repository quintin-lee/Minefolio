import { describe, it, expect } from 'vitest'
import { buildTranscriptMarkdown, buildSessionMarkdown } from '@/composables/chat/useChatExport'

describe('useChatExport helpers', () => {
  it('formats current chat transcript markdown with model and messages', () => {
    const md = buildTranscriptMarkdown({
      title: '月度财务体检',
      sessionId: 42,
      model: 'gpt-4o',
      provider: 'openai',
      messages: [
        { role: 'user', content: '分析本月支出' },
        { role: 'assistant', content: '餐饮占比 32%' },
      ],
    })
    expect(md).toContain('# 月度财务体检')
    expect(md).toContain('gpt-4o (openai)')
    expect(md).toContain('会话 ID**: 42')
    expect(md).toContain('分析本月支出')
    expect(md).toContain('餐饮占比 32%')
  })

  it('formats single session markdown including thinking blocks', () => {
    const md = buildSessionMarkdown('估值分析', [
      { role: 'user', content: '查茅台', thinking_content: '' },
      { role: 'assistant', content: '估值合理', thinking_content: '先看现金流\n再看增速' },
    ])
    expect(md).toContain('# 估值分析')
    expect(md).toContain('> 💭 **深度思考过程**:')
    expect(md).toContain('> 先看现金流\n> 再看增速')
    expect(md).toContain('估值合理')
  })
})
