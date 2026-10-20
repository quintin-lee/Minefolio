import type { PanelItem, ExtractResult } from '@/types/chat-panel'
import type { WorkflowRunState, WorkflowConfigState } from '@/types'
import type { ProposedAction } from '@/components/ActionCard.vue'

/**
 * 扫描消息文本及工作流元数据，抽取可放入右侧分析面板的结构化卡片项
 */
export function extractPanelItems(
  messageId: number,
  content: string,
  workflowData?: WorkflowRunState,
  workflowConfig?: WorkflowConfigState
): ExtractResult {
  const panelItems: PanelItem[] = []
  const extractedSegmentIds = new Set<string>()

  // 1. 工作流配置卡片
  if (workflowConfig) {
    const id = `msg-${messageId}-wf-config`
    panelItems.push({
      id,
      label: '工作流参数配置',
      icon: 'ph:sliders-horizontal-bold',
      type: 'workflow-config',
      content: '',
      workflowConfig,
      messageId,
      createdAt: Date.now()
    })
    extractedSegmentIds.add(id)
  }

  // 2. 工作流执行进度卡片
  if (workflowData) {
    const id = `msg-${messageId}-wf-progress`
    panelItems.push({
      id,
      label: '工作流执行进度',
      icon: 'ph:git-fork-bold',
      type: 'workflow-progress',
      content: '',
      workflowData,
      messageId,
      createdAt: Date.now()
    })
    extractedSegmentIds.add(id)
  }

  const text = content || ''
  if (!text) {
    return { panelItems, extractedSegmentIds }
  }

  const blockRegex = /```([\w\-+#.]+)?\s*\n([\s\S]*?)(?:```|$)/g
  let match: RegExpExecArray | null
  let lastIndex = 0
  let segIdx = 0
  let chartCount = 0
  let tableCount = 0

  while ((match = blockRegex.exec(text)) !== null) {
    const matchStart = match.index
    const fullMatch = match[0]
    const lang = (match[1] || '').trim().toLowerCase()
    const code = match[2] ?? ''

    // 检查此代码块之前的 Markdown 文本是否包含表格
    if (matchStart > lastIndex) {
      const textBefore = text.slice(lastIndex, matchStart).trim()
      checkAndExtractTable(textBefore, `md-${segIdx++}`)
    }

    // 检查是否为 action 卡片
    let isAction = false
    let actionObj: ProposedAction | null = null
    if (lang === 'action') {
      try {
        actionObj = JSON.parse(code)
        if (actionObj && (actionObj.action_type === 'daily_expense' || actionObj.action_type === 'transfer')) {
          isAction = true
        }
      } catch { /* ignore */ }
    } else if (lang === 'json' && (code.includes('"action_type"') || code.includes('"daily_expense"') || code.includes('"transfer"'))) {
      try {
        const parsed = JSON.parse(code)
        if (parsed && (parsed.action_type === 'daily_expense' || parsed.action_type === 'transfer')) {
          actionObj = parsed
          isAction = true
        }
      } catch { /* ignore */ }
    }

    if (isAction && actionObj) {
      const segId = `act-${segIdx++}`
      const isExpense = actionObj.action_type === 'daily_expense'
      panelItems.push({
        id: `msg-${messageId}-${segId}`,
        segmentId: segId,
        label: isExpense ? '日常记账确认' : '账户转账操作',
        icon: isExpense ? 'ph:receipt-bold' : 'ph:arrows-left-right-bold',
        type: 'action',
        content: code.trim(),
        actionData: actionObj,
        messageId,
        createdAt: Date.now()
      })
      extractedSegmentIds.add(segId)
    } else if (lang === 'mermaid') {
      chartCount++
      const segId = `mmd-${segIdx++}`
      panelItems.push({
        id: `msg-${messageId}-${segId}`,
        segmentId: segId,
        label: chartCount === 1 ? '财务分析图表' : `财务图表 #${chartCount}`,
        icon: 'ph:chart-pie-slice-bold',
        type: 'mermaid',
        content: code.trim(),
        messageId,
        createdAt: Date.now()
      })
      extractedSegmentIds.add(segId)
    } else {
      // 普通代码块
      const segId = `code-${segIdx++}`
      panelItems.push({
        id: `msg-${messageId}-${segId}`,
        segmentId: segId,
        label: lang ? `${lang.toUpperCase()} 脚本` : '代码片段',
        icon: 'ph:code-bold',
        type: 'code',
        content: code.trim(),
        lang: lang || undefined,
        messageId,
        createdAt: Date.now()
      })
      extractedSegmentIds.add(segId)
    }

    lastIndex = matchStart + fullMatch.length
  }

  // 检查尾部 Markdown 文本中是否包含表格
  if (lastIndex < text.length) {
    const trailingText = text.slice(lastIndex).trim()
    checkAndExtractTable(trailingText, `md-${segIdx++}`)
  }

  function checkAndExtractTable(markdownPart: string, segId: string) {
    if (!markdownPart) return
    // 表格识别规则：包含 | ... | 且下一行或附近包含 | --- | 分隔线
    const hasTablePattern = /\|[^\n\r]+\|[\r\n]+\|[\s\-:|]+\|/.test(markdownPart)
    if (hasTablePattern) {
      tableCount++
      panelItems.push({
        id: `msg-${messageId}-${segId}`,
        segmentId: segId,
        label: tableCount === 1 ? '财务数据表格' : `数据表格 #${tableCount}`,
        icon: 'ph:table-bold',
        type: 'markdown',
        content: markdownPart,
        messageId,
        createdAt: Date.now()
      })
      extractedSegmentIds.add(segId)
    }
  }

  return { panelItems, extractedSegmentIds }
}
