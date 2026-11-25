import { marked } from 'marked'
import DOMPurify from 'dompurify'

let hookRegistered = false

function ensureSafeLinkHook(): void {
  if (hookRegistered) return
  hookRegistered = true
  DOMPurify.addHook('afterSanitizeAttributes', (node) => {
    if (node.tagName === 'A') {
      const href = node.getAttribute('href') || ''
      const trimmed = href.trim()
      if (/^(javascript|vbscript|data):/i.test(trimmed)) {
        node.removeAttribute('href')
      } else if (trimmed) {
        node.setAttribute('target', '_blank')
        node.setAttribute('rel', 'noopener noreferrer')
      }
    }
  })
}

/**
 * Render Markdown text into sanitized HTML with safe external link attributes.
 */
export function renderSafeMarkdown(text?: string): string {
  if (!text) return ''
  ensureSafeLinkHook()
  try {
    const rawHtml = marked.parse(text, { async: false, breaks: true }) as string
    return DOMPurify.sanitize(rawHtml)
  } catch {
    const escaped = text
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;')
    return DOMPurify.sanitize(`<pre>${escaped}</pre>`)
  }
}
