import type { AiMessage } from '@/api/ai'

/**
 * 构建当前会话全文 Markdown 导出文本
 */
export function buildTranscriptMarkdown(opts: {
  title?: string
  sessionId?: number | null
  model?: string
  provider?: string
  messages: Array<Pick<AiMessage, 'role' | 'content'>>
}): string {
  const title = opts.title || 'Minefolio AI 财务对话记录'
  const dateStr = new Date().toLocaleString()
  const modelStr = opts.model
    ? `\n- **AI 模型**: ${opts.model} (${opts.provider || '默认'})`
    : ''

  let md = `# ${title}\n\n- **导出时间**: ${dateStr}${modelStr}\n- **会话 ID**: ${opts.sessionId || '未命名'}\n\n---\n\n`

  for (const m of opts.messages) {
    const roleName =
      m.role === 'user' ? '👤 **用户 (User)**' : '🤖 **AI 财务助手 (Minefolio AI)**'
    md += `### ${roleName}\n\n${m.content}\n\n---\n\n`
  }
  return md
}

/**
 * 构建单场历史会话 Markdown 导出文本（含深度思考过程）
 */
export function buildSessionMarkdown(
  title: string,
  messages: Array<Pick<AiMessage, 'role' | 'content' | 'thinking_content'>>,
): string {
  const lines: string[] = [
    `# ${title || 'Minefolio AI 对话'}`,
    `*导出时间: ${new Date().toLocaleString()}*`,
    '',
    '---',
    '',
  ]
  for (const m of messages) {
    const roleLabel = m.role === 'user' ? '👤 用户' : '🤖 Minefolio AI 助手'
    lines.push(`### ${roleLabel}`)
    if (m.thinking_content) {
      lines.push('> 💭 **深度思考过程**:\n> ' + m.thinking_content.split('\n').join('\n> '))
      lines.push('')
    }
    lines.push(m.content || '')
    lines.push('')
  }
  return lines.join('\n')
}

/**
 * 触发浏览器下载 Markdown 文件
 */
export function downloadMarkdownFile(filename: string, content: string): void {
  const blob = new Blob([content], { type: 'text/markdown;charset=utf-8' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = filename
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
  URL.revokeObjectURL(url)
}
