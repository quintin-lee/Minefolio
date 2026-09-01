<template>
  <div class="chat-message-content">
    <!-- Inline Workflow Config Card (if staged) -->
    <WorkflowConfigCard
      v-if="workflowConfig"
      :message-id="messageId"
      :config="workflowConfig"
    />

    <!-- Workflow Pipeline Progress Card (if present) -->
    <WorkflowProgressCard
      v-else-if="workflowData"
      :workflow-data="workflowData"
    />

    <template v-for="seg in segments" :key="seg.id">
      <!-- Markdown Segment -->
      <div
        v-if="seg.type === 'markdown'"
        class="markdown-part"
        v-html="seg.renderedHtml"
      ></div>

      <!-- Streaming plain-text segment: append-only, no markdown re-parse per frame -->
      <div
        v-else-if="seg.type === 'streaming-text'"
        class="markdown-part streaming-text"
      >{{ seg.content }}<span v-if="showTrailingCursor" class="typing-cursor-inline"></span></div>

      <!-- Action Card Block -->
      <ActionCard
        v-else-if="seg.type === 'action' && seg.actionData"
        :action-data="seg.actionData"
      />

      <!-- Mermaid Diagram Block -->
      <MermaidBlock
        v-else-if="seg.type === 'mermaid'"
        :code="seg.content"
        :is-streaming="isStreaming"
      />

      <!-- Streaming Mermaid Code Block (in progress before closing ```) -->
      <div v-else-if="seg.type === 'streaming-mermaid'" class="streaming-mermaid-placeholder">
        <div class="placeholder-header">
          <div class="header-left">
            <Icon icon="ph:projector-screen-chart" class="placeholder-icon" />
            <span>Mermaid 图表生成中...</span>
          </div>
          <span class="streaming-pulse-dot"></span>
        </div>
        <pre class="streaming-code"><code>{{ seg.content }}</code></pre>
      </div>

      <!-- Generic Code Block with Syntax Highlighting & Copy -->
      <CodeBlock
        v-else-if="seg.type === 'code'"
        :code="seg.content"
        :lang="seg.lang"
      />
    </template>
  </div>
</template>

<script setup lang="ts">
import { computed, ref, watch, defineAsyncComponent, onUnmounted } from 'vue'
import { marked } from 'marked'
import DOMPurify from 'dompurify'
import { Icon } from '@iconify/vue'
import ActionCard from '@/components/ActionCard.vue'
import CodeBlock from '@/components/CodeBlock.vue'
import WorkflowProgressCard from '@/components/WorkflowProgressCard.vue'
import WorkflowConfigCard from '@/components/WorkflowConfigCard.vue'
import type { ProposedAction } from '@/components/ActionCard.vue'
import type { WorkflowRunState, WorkflowConfigState } from '@/types'

// Lazy-load MermaidBlock to avoid 642kb bundle cost when no diagrams are present
const MermaidBlock = defineAsyncComponent(() => import('@/components/MermaidBlock.vue'))

const props = defineProps<{
  messageId?: number
  content: string
  isStreaming?: boolean
  workflowData?: WorkflowRunState
  workflowConfig?: WorkflowConfigState
  enableBuffer?: boolean
}>()

const debouncedContent = ref(props.content || '')
let debounceTimer: number | null = null
let rafId: number | null = null
let prevContent = ''

// LRU cache keyed by content → pre-rendered HTML, avoids repeated marked.parse
// + DOMPurify after streaming completes.
const htmlCache = new Map<string, string>()
const HTML_CACHE_LIMIT = 60

watch(
  () => props.content,
  (v) => {
    const val = v || ''
    if (props.isStreaming && props.enableBuffer !== false) {
      // Streaming: RAF-sync so cursor stays synced with buffer drain
      if (rafId !== null) cancelAnimationFrame(rafId)
      rafId = requestAnimationFrame(() => {
        rafId = null
        // Only update when content actually changed (skip duplicate RAF ticks)
        if (val !== prevContent) {
          debouncedContent.value = val
          prevContent = val
        }
      })
    } else {
      // Non-streaming: debounce + use cached HTML when available
      if (debounceTimer) clearTimeout(debounceTimer)
      debounceTimer = window.setTimeout(() => {
        debounceTimer = null
        prevContent = val
        const cached = htmlCache.get(val)
        debouncedContent.value = cached ?? val
        if (cached) return
        if (htmlCache.size >= HTML_CACHE_LIMIT) {
          const firstKey = htmlCache.keys().next().value as string
          htmlCache.delete(firstKey)
        }
        htmlCache.set(val, val)
      }, 48)
    }
  },
  { immediate: true },
)
watch(
  () => props.isStreaming,
  (streaming) => {
    if (!streaming) {
      if (debounceTimer) {
        clearTimeout(debounceTimer)
        debounceTimer = null
      }
      if (rafId !== null) {
        cancelAnimationFrame(rafId)
        rafId = null
      }
      prevContent = ''
      const cached = htmlCache.get(props.content || '')
      debouncedContent.value = cached ?? (props.content || '')
    }
  },
)

onUnmounted(() => {
  if (debounceTimer) clearTimeout(debounceTimer)
  if (rafId !== null) cancelAnimationFrame(rafId)
})

interface Segment {
  id: string
  type: 'markdown' | 'streaming-text' | 'mermaid' | 'streaming-mermaid' | 'action' | 'code'
  content: string
  lang?: string
  renderedHtml?: string
  actionData?: ProposedAction
}

const showTrailingCursor = computed(() => props.isStreaming && props.enableBuffer !== false)

const mdCache = new Map<string, string>()
const MD_CACHE_LIMIT = 80

function renderMarkdown(text?: string): string {
  if (!text) return ''
  const cached = mdCache.get(text)
  if (cached !== undefined) return cached
  let html: string
  try {
    const rawHtml = marked.parse(text, { async: false, breaks: true }) as string
    html = DOMPurify.sanitize(rawHtml)
  } catch {
    html = DOMPurify.sanitize(text.replace(/</g, '&lt;').replace(/>/g, '&gt;'))
  }
  if (mdCache.size >= MD_CACHE_LIMIT) {
    const firstKey = mdCache.keys().next().value as string
    mdCache.delete(firstKey)
  }
  mdCache.set(text, html)
  return html
}

const segments = computed<Segment[]>(() => {
  const text = debouncedContent.value || ''
  if (!text) return []

  const result: Segment[] = []
  let lastIndex = 0
  const blockRegex = /```([\w\-+#.]+)?\s*\n([\s\S]*?)(?:```|$)/g
  let match: RegExpExecArray | null
  let segIdx = 0

  const streaming = props.isStreaming && props.enableBuffer !== false

  while ((match = blockRegex.exec(text)) !== null) {
    const matchStart = match.index
    const fullMatch = match[0]
    const lang = (match[1] || '').trim().toLowerCase()
    const code = match[2] ?? ''
    const hasClosed = fullMatch.endsWith('```')

    // Check if it's an action card JSON block
    let isAction = false
    let actionObj: ProposedAction | null = null

    if (lang === 'action') {
      try {
        actionObj = JSON.parse(code)
        if (actionObj && (actionObj.action_type === 'daily_expense' || actionObj.action_type === 'transfer')) {
          isAction = true
        }
      } catch {
        // ignore
      }
    } else if (lang === 'json' && (code.includes('"action_type"') || code.includes('"daily_expense"') || code.includes('"transfer"'))) {
      try {
        const parsed = JSON.parse(code)
        if (parsed && (parsed.action_type === 'daily_expense' || parsed.action_type === 'transfer')) {
          actionObj = parsed
          isAction = true
        }
      } catch {
        // ignore
      }
    }

    const isMermaid = lang === 'mermaid'

    // Add markdown segment before this match
    if (matchStart > lastIndex) {
      const markdownBefore = text.slice(lastIndex, matchStart)
      if (markdownBefore.trim()) {
        result.push({
          id: `md-${segIdx++}`,
          type: 'markdown',
          content: markdownBefore.trim(),
          renderedHtml: renderMarkdown(markdownBefore.trim()),
        })
      }
    }

    if (isAction && actionObj) {
      result.push({
        id: `act-${segIdx++}`,
        type: 'action',
        content: code.trim(),
        actionData: actionObj,
      })
    } else if (isMermaid) {
      if (hasClosed) {
        result.push({
          id: `mmd-${segIdx++}`,
          type: 'mermaid',
          content: code.trim(),
        })
      } else {
        if (props.isStreaming) {
          result.push({
            id: `mmd-stream-${segIdx++}`,
            type: 'streaming-mermaid',
            content: code,
          })
        } else {
          result.push({
            id: `mmd-${segIdx++}`,
            type: 'mermaid',
            content: code.trim(),
          })
        }
      }
    } else {
      // Standard code block
      result.push({
        id: `code-${segIdx++}`,
        type: 'code',
        content: hasClosed ? code.trim() : code,
        lang: lang || undefined,
      })
    }

    lastIndex = matchStart + fullMatch.length
  }

  // Trailing text after the last match — the live tail that grows per RAF tick.
  if (lastIndex < text.length) {
    const trailing = text.slice(lastIndex)
    if (trailing.length > 0) {
      if (streaming) {
        // Append-only plain-text: Vue patches text nodes cheaply per character,
        // no marked.parse / DOMPurify / v-html rebuild each frame.
        result.push({
          id: 'stream-tail',
          type: 'streaming-text',
          content: trailing,
        })
      } else {
        if (trailing.trim()) {
          result.push({
            id: `md-${segIdx++}`,
            type: 'markdown',
            content: trailing.trim(),
            renderedHtml: renderMarkdown(trailing.trim()),
          })
        }
      }
    }
  }

  return result.length > 0 ? result : streaming
    ? [{ id: 'stream-tail', type: 'streaming-text', content: text }]
    : [{
        id: 'md-0',
        type: 'markdown',
        content: text,
        renderedHtml: renderMarkdown(text),
      }]
})
</script>

<style scoped>
.chat-message-content {
  display: flex;
  flex-direction: column;
  gap: 8px;
  width: 100%;
}

.markdown-part {
  width: 100%;
  line-height: 1.65;
  word-break: break-word;
}

.streaming-text {
  white-space: pre-wrap;
}

.typing-cursor-inline {
  display: inline-block;
  width: 6px;
  height: 1em;
  background: var(--mf-primary, #00d4ff);
  margin-left: 2px;
  vertical-align: text-bottom;
  animation: cursor-blink 0.8s steps(1) infinite;
}

@keyframes cursor-blink {
  0%, 49% { opacity: 1; }
  50%, 100% { opacity: 0; }
}

.markdown-part :deep(p) {
  margin: 0 0 8px;
}
.markdown-part :deep(p:last-child) {
  margin-bottom: 0;
}
.markdown-part :deep(h1),
.markdown-part :deep(h2),
.markdown-part :deep(h3),
.markdown-part :deep(h4) {
  color: #fff;
  margin: 12px 0 6px;
  font-weight: 600;
}
.markdown-part :deep(ul),
.markdown-part :deep(ol) {
  margin: 6px 0 10px;
  padding-left: 20px;
}
.markdown-part :deep(li) {
  margin-bottom: 4px;
}
.markdown-part :deep(code) {
  font-family: var(--mf-font-mono, monospace);
  background: rgba(0, 212, 255, 0.08);
  color: #38bdf8;
  padding: 2px 6px;
  border-radius: 4px;
  font-size: 12px;
  border: 1px solid rgba(0, 212, 255, 0.15);
}
.markdown-part :deep(pre) {
  background: #020617;
  border: 1px solid var(--mf-border);
  border-radius: 8px;
  padding: 12px 14px;
  overflow-x: auto;
  margin: 10px 0;
}
.markdown-part :deep(pre code) {
  background: transparent;
  padding: 0;
  border: none;
  color: #e2e8f0;
}
.markdown-part :deep(blockquote) {
  margin: 10px 0;
  padding: 8px 14px;
  border-left: 3px solid var(--mf-primary);
  background: rgba(0, 212, 255, 0.04);
  border-radius: 0 6px 6px 0;
  color: var(--mf-text-muted);
}
.markdown-part :deep(table) {
  width: 100%;
  border-collapse: collapse;
  margin: 12px 0;
  font-size: 13px;
}
.markdown-part :deep(th),
.markdown-part :deep(td) {
  border: 1px solid var(--mf-border);
  padding: 8px 12px;
}
.markdown-part :deep(th) {
  background: rgba(0, 212, 255, 0.08);
  font-weight: 600;
  color: var(--mf-primary);
}

/* Streaming Mermaid Placeholder */
.streaming-mermaid-placeholder {
  margin: 10px 0;
  background: rgba(15, 23, 42, 0.7);
  border: 1px dashed var(--mf-border-hover);
  border-radius: var(--mf-radius-md);
  overflow: hidden;
}

.placeholder-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 12px;
  background: rgba(0, 212, 255, 0.06);
  border-bottom: 1px solid rgba(0, 212, 255, 0.1);
  font-size: 12px;
  color: var(--mf-primary);
  font-weight: 500;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 6px;
}

.placeholder-icon {
  font-size: 14px;
}

.streaming-pulse-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--mf-primary);
  box-shadow: 0 0 8px var(--mf-primary);
  animation: pulse-dot 1.2s infinite;
}

@keyframes pulse-dot {
  0%, 100% { opacity: 1; transform: scale(1); }
  50% { opacity: 0.4; transform: scale(0.8); }
}

.streaming-code {
  margin: 0;
  padding: 10px 14px;
  background: #020617;
  color: #38bdf8;
  font-family: var(--mf-font-mono, monospace);
  font-size: 11px;
  line-height: 1.5;
  overflow-x: auto;
  max-height: 160px;
}
</style>
