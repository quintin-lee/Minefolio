<template>
  <div class="chat-message-content">
    <!-- Inline Workflow Config Card (if staged) -->
    <template v-if="workflowConfig">
      <div
        v-if="isExtracted('wf-config')"
        class="panel-ref-pill"
        @click="$emit('open-panel-item', `msg-${messageId}-wf-config`)"
      >
        <div class="ref-pill-left">
          <Icon icon="ph:sliders-horizontal-bold" class="ref-pill-icon" />
          <span class="ref-pill-label">工作流参数配置</span>
        </div>
        <div class="ref-pill-right">
          <span class="ref-pill-hint">在右侧面板查看</span>
          <Icon icon="ph:arrow-right-bold" class="ref-pill-arrow" />
        </div>
      </div>
      <WorkflowConfigCard
        v-else
        :message-id="messageId"
        :config="workflowConfig"
      />
    </template>

    <!-- Workflow Pipeline Progress Card (if present) -->
    <template v-else-if="workflowData">
      <div
        v-if="isExtracted('wf-progress')"
        class="panel-ref-pill"
        @click="$emit('open-panel-item', `msg-${messageId}-wf-progress`)"
      >
        <div class="ref-pill-left">
          <Icon icon="ph:git-fork-bold" class="ref-pill-icon" />
          <span class="ref-pill-label">工作流执行进度</span>
        </div>
        <div class="ref-pill-right">
          <span class="ref-pill-hint">在右侧面板查看</span>
          <Icon icon="ph:arrow-right-bold" class="ref-pill-arrow" />
        </div>
      </div>
      <WorkflowProgressCard
        v-else
        :workflow-data="workflowData"
      />
    </template>

    <!-- Thinking Mode Collapsible Card -->
    <div v-if="thinkingContent && thinkingContent.trim()" class="thinking-card">
      <div class="thinking-header" @click="toggleThinking">
        <div class="thinking-header-left">
          <Icon
            :icon="isThinkingStreaming ? 'ph:sparkle-fill' : 'ph:brain-bold'"
            class="thinking-icon"
            :class="{ 'spinning-sparkle': isThinkingStreaming }"
          />
          <span class="thinking-status">
            {{ isThinkingStreaming ? '深度思考中...' : '已深度思考' }}
          </span>
          <span v-if="thinkingDuration > 0" class="thinking-duration">
            ({{ thinkingDuration }}s)
          </span>
        </div>
        <div class="thinking-header-right">
          <button
            type="button"
            class="thinking-action-btn"
            :title="thinkingCopied ? '已复制' : '复制思考过程'"
            @click.stop="copyThinkingText"
            aria-label="复制思考过程"
          >
            <Icon :icon="thinkingCopied ? 'ph:check-bold' : 'ph:copy-bold'" />
          </button>
          <Icon
            :icon="isThinkingExpanded ? 'ph:caret-down-bold' : 'ph:caret-right-bold'"
            class="thinking-caret"
          />
        </div>
      </div>
      <div v-if="isThinkingExpanded" class="thinking-content-body">
        <div class="thinking-text-inner">{{ thinkingContent }}</div>
      </div>
    </div>

    <!-- Tool Calls Collapsible Card -->
    <div v-if="toolCalls && toolCalls.length" class="tool-card">
      <div class="tool-card-header" @click="toggleTools">
        <div class="tool-card-left">
          <Icon
            :icon="isToolsStreaming ? 'ph:spinner-gap' : 'ph:wrench-bold'"
            class="tool-card-icon"
            :class="{ 'spinning-sparkle': isToolsStreaming }"
          />
          <span class="tool-card-status">
            {{ isToolsStreaming ? '正在调用工具...' : `已调用 ${toolCalls.length} 个工具` }}
          </span>
        </div>
        <Icon
          :icon="isToolsExpanded ? 'ph:caret-down-bold' : 'ph:caret-right-bold'"
          class="tool-card-caret"
        />
      </div>
      <div v-if="isToolsExpanded" class="tool-card-body">
        <div v-for="(tc, i) in toolCalls" :key="i" class="tool-call-item">
          <div class="tool-call-item-header">
            <Icon icon="ph:cube-bold" class="tool-item-icon" />
            <span class="tool-call-name">{{ tc.name }}</span>
            <span v-if="tc.args && !tc.result" class="tool-item-pending">执行中...</span>
            <span v-else-if="tc.result" class="tool-item-done">完成</span>
          </div>
          <pre v-if="tc.args" class="tool-call-args">{{ tc.args }}</pre>
          <pre v-if="tc.result" class="tool-call-result">{{ tc.result }}</pre>
        </div>
      </div>
    </div>

    <template v-for="seg in segments" :key="seg.id">
      <!-- 若该段已被抽到面板中且非流式状态，呈现紧凑胶囊入口 -->
      <div
        v-if="isExtracted(seg.id)"
        class="panel-ref-pill"
        @click="$emit('open-panel-item', getPanelItemId(seg.id))"
      >
        <div class="ref-pill-left">
          <Icon :icon="getSegmentIcon(seg)" class="ref-pill-icon" />
          <span class="ref-pill-label">{{ getSegmentLabel(seg) }}</span>
        </div>
        <div class="ref-pill-right">
          <span class="ref-pill-hint">在右侧面板查看</span>
          <Icon icon="ph:arrow-right-bold" class="ref-pill-arrow" />
        </div>
      </div>

      <template v-else>
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
        ><span v-html="renderInlineMarkdown(seg.content)"></span></div>

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
    </template>
    <span v-if="showTrailingCursor" class="typing-cursor-inline"></span>
  </div>
</template>

<script setup lang="ts">
import { computed, ref, watch, defineAsyncComponent, onUnmounted } from 'vue'
import { marked } from 'marked'
import DOMPurify from 'dompurify'
import { Icon } from '@iconify/vue'
import ActionCard from '@/components/ActionCard.vue'
import WorkflowProgressCard from '@/components/WorkflowProgressCard.vue'
import WorkflowConfigCard from '@/components/WorkflowConfigCard.vue'
import type { ProposedAction } from '@/components/ActionCard.vue'
import type { WorkflowRunState, WorkflowConfigState } from '@/types'

// Lazy-load MermaidBlock and CodeBlock to avoid bundling heavy renderers into main chat chunk
const MermaidBlock = defineAsyncComponent(() => import('@/components/MermaidBlock.vue'))
const CodeBlock = defineAsyncComponent(() => import('@/components/CodeBlock.vue'))

const props = defineProps<{
  messageId?: number
  content: string
  thinkingContent?: string
  isStreaming?: boolean
  toolCalls?: { id: string; name: string; args?: string; result?: string }[]
  workflowData?: WorkflowRunState
  workflowConfig?: WorkflowConfigState
  enableBuffer?: boolean
  extractedItemIds?: string[]
}>()

const emit = defineEmits<{
  (e: 'open-panel-item', itemId: string): void
}>()

function isExtracted(segId: string): boolean {
  if (props.isStreaming || !props.extractedItemIds || props.extractedItemIds.length === 0) {
    return false
  }
  return (
    props.extractedItemIds.includes(segId) ||
    (props.messageId !== undefined && props.extractedItemIds.includes(`msg-${props.messageId}-${segId}`))
  )
}

function getPanelItemId(segId: string): string {
  return props.messageId !== undefined ? `msg-${props.messageId}-${segId}` : segId
}

function getSegmentLabel(seg: Segment): string {
  if (seg.type === 'action') {
    return seg.actionData?.action_type === 'daily_expense' ? '日常记账确认' : '账户转账操作'
  }
  if (seg.type === 'mermaid') {
    return '财务分析图表'
  }
  if (seg.type === 'code') {
    return seg.lang ? `${seg.lang.toUpperCase()} 脚本` : '代码片段'
  }
  if (seg.type === 'markdown') {
    return '财务数据表格'
  }
  return '查看分析'
}

function getSegmentIcon(seg: Segment): string {
  if (seg.type === 'action') {
    return seg.actionData?.action_type === 'daily_expense' ? 'ph:receipt-bold' : 'ph:arrows-left-right-bold'
  }
  if (seg.type === 'mermaid') {
    return 'ph:chart-pie-slice-bold'
  }
  if (seg.type === 'code') {
    return 'ph:code-bold'
  }
  if (seg.type === 'markdown') {
    return 'ph:table-bold'
  }
  return 'ph:arrow-square-out-bold'
}

const debouncedContent = ref(props.content || '')
let debounceTimer: number | null = null
watch(
  () => props.content,
  (v) => {
    const val = v || ''
    if (props.isStreaming && props.enableBuffer !== false) {
      // 流式状态：SmoothStreamWriter 已经由 requestAnimationFrame 严格按帧平滑吐字。
      debouncedContent.value = val
    } else {
      // 非流式：启用短防抖，避免高频更新下的重复重排
      if (debounceTimer) clearTimeout(debounceTimer)
      debounceTimer = window.setTimeout(() => {
        debounceTimer = null
        debouncedContent.value = val
      }, 40)
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
      debouncedContent.value = props.content || ''
    }
  },
)

// Thinking Mode State & Timing
const manualExpanded = ref<boolean | null>(null)
const thinkingDuration = ref(0)
let thinkingTimer: number | null = null
const thinkingCopied = ref(false)
let copyThinkingTimer: number | null = null

const isThinkingStreaming = computed(() => {
  return !!props.isStreaming && !props.content && !!props.thinkingContent
})

const isThinkingExpanded = computed({
  get() {
    if (manualExpanded.value !== null) {
      return manualExpanded.value
    }
    // 思考内容输出时与输出后默认保持展开，不折叠
    return true
  },
  set(val: boolean) {
    manualExpanded.value = val
  },
})

function toggleThinking() {
  isThinkingExpanded.value = !isThinkingExpanded.value
}
// Tool Calls Card State
const toolsManualExpanded = ref<boolean | null>(null)
const isToolsStreaming = computed(() => {
  return !!props.isStreaming && !!props.toolCalls?.some(tc => !tc.result)
})
const isToolsExpanded = computed({
  get() {
    if (toolsManualExpanded.value !== null) {
      return toolsManualExpanded.value
    }
    return isToolsStreaming.value
  },
  set(val: boolean) {
    toolsManualExpanded.value = val
  },
})

function toggleTools() {
  isToolsExpanded.value = !isToolsExpanded.value
}

watch(
  () => props.messageId,
  () => {
    manualExpanded.value = null
    toolsManualExpanded.value = null
    thinkingDuration.value = 0
  },
)

watch(
  isThinkingStreaming,
  (streaming) => {
    if (streaming) {
      if (!thinkingTimer) {
        thinkingDuration.value = 1
        thinkingTimer = window.setInterval(() => {
          thinkingDuration.value += 1
        }, 1000)
      }
    } else {
      if (thinkingTimer) {
        clearInterval(thinkingTimer)
        thinkingTimer = null
      }
    }
  },
  { immediate: true },
)

async function copyThinkingText() {
  if (!props.thinkingContent) return
  try {
    await navigator.clipboard.writeText(props.thinkingContent)
    thinkingCopied.value = true
    if (copyThinkingTimer) clearTimeout(copyThinkingTimer)
    copyThinkingTimer = window.setTimeout(() => {
      thinkingCopied.value = false
    }, 2000)
  } catch {
    // fallback or ignore
  }
}

onUnmounted(() => {
  if (debounceTimer) clearTimeout(debounceTimer)
  if (thinkingTimer) clearInterval(thinkingTimer)
  if (copyThinkingTimer) clearTimeout(copyThinkingTimer)
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

/**
 * 流式尾段轻量行内渲染：只处理粗体/斜体/行内代码/删除线，不跑完整 marked 解析。
 * 目的是消除流式期间 **粗体**、`代码` 等语法以原始符号裸露、段落结束才跳变的问题。
 * 输出已 HTML 转义，天然免疫注入，无需 DOMPurify 二次过滤。
 */
function renderInlineMarkdown(text: string): string {
  const esc = text
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
  return esc
    .replace(/`([^`\n]+)`/g, '<code>$1</code>')
    .replace(/\*\*([^*\n]+)\*\*/g, '<strong>$1</strong>')
    .replace(/(^|[^*])\*([^*\n]+)\*/g, '$1<em>$2</em>')
    .replace(/~~([^~\n]+)~~/g, '<del>$1</del>')
}

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
        // 如果尾部积累了多个完整段落（以双换行 \n\n 隔开），将已完成段落提前作为 Markdown 渲染，
        // 仅将正在流式输入的最后一个活跃段落保持为 streaming-text，避免整体在结束瞬间产生突兀的样式跳跃！
        const lastDoubleNewline = trailing.lastIndexOf('\n\n')
        if (lastDoubleNewline !== -1) {
          const completedPart = trailing.slice(0, lastDoubleNewline).trim()
          const activeTail = trailing.slice(lastDoubleNewline + 2)
          if (completedPart) {
            result.push({
              id: `md-${segIdx++}`,
              type: 'markdown',
              content: completedPart,
              renderedHtml: renderMarkdown(completedPart),
            })
          }
          result.push({
            id: 'stream-tail',
            type: 'streaming-text',
            content: activeTail,
          })
        } else {
          result.push({
            id: 'stream-tail',
            type: 'streaming-text',
            content: trailing,
          })
        }
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
}

[data-theme="light"] .chat-message-content .workflow-config-card,
[data-theme="light"] .chat-message-content .workflow-card-wrapper {
  color: var(--mf-text-main);
  border: 1px solid #e5e7eb;
  background: #ffffff;
}
[data-theme="light"] .chat-message-content .wf-title,
[data-theme="light"] .chat-message-content .wf-subtitle,
[data-theme="light"] .chat-message-content .result-title,
[data-theme="light"] .chat-message-content .result-item,
[data-theme="light"] .chat-message-content .step-content,
[data-theme="light"] .chat-message-content .step-title,
[data-theme="light"] .chat-message-content .step-desc,
[data-theme="light"] .chat-message-content .param-label {
  color: var(--mf-text-main);
}
[data-theme="light"] .chat-message-content .status-tag.running,
[data-theme="light"] .chat-message-content .status-tag.completed,
[data-theme="light"] .chat-message-content .status-tag.error {
  color: var(--mf-text-main);
}
[data-theme="light"] .chat-message-content .el-slider__runway,
[data-theme="light"] .chat-message-content .el-input-number {
  color: var(--mf-text-main);
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
  width: 5px;
  height: 1.1em;
  background: var(--mf-primary, #00d4ff);
  margin-left: 3px;
  vertical-align: -2px;
  border-radius: 2px;
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
  color: var(--mf-text-main);
  margin: 14px 0 8px;
  font-weight: 600;
  letter-spacing: -0.2px;
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
  background: var(--mf-primary-light);
  color: var(--mf-primary);
  padding: 2px 6px;
  border-radius: 4px;
  font-size: 12px;
  border: 1px solid var(--mf-primary-border);
}
.markdown-part :deep(pre) {
  background: var(--mf-surface-card);
  border: 1px solid var(--mf-border);
  border-radius: 10px;
  padding: 12px 16px;
  overflow-x: auto;
  margin: 12px 0;
}
.markdown-part :deep(pre code) {
  background: transparent;
  padding: 0;
  border: none;
  color: var(--mf-text-main);
}
.markdown-part :deep(blockquote) {
  margin: 12px 0;
  padding: 10px 16px;
  border-left: 3px solid var(--mf-primary);
  background: var(--mf-primary-light);
  border-radius: 0 8px 8px 0;
  color: var(--mf-text-regular);
  font-size: 13.5px;
  line-height: 1.6;
}
.markdown-part :deep(table) {
  width: 100%;
  border-collapse: separate;
  border-spacing: 0;
  margin: 14px 0;
  font-size: 13px;
  border-radius: 10px;
  overflow: hidden;
  border: 1px solid var(--mf-border);
  box-shadow: var(--mf-shadow-sm);
}
.markdown-part :deep(th),
.markdown-part :deep(td) {
  border-right: 1px solid var(--mf-border);
  border-bottom: 1px solid var(--mf-border);
  padding: 9px 14px;
  text-align: left;
}
.markdown-part :deep(th:last-child),
.markdown-part :deep(td:last-child) {
  border-right: none;
}
.markdown-part :deep(tr:last-child td) {
  border-bottom: none;
}
.markdown-part :deep(th) {
  background: var(--mf-surface-muted);
  font-weight: 600;
  color: var(--mf-text-main);
  letter-spacing: 0.02em;
}
.markdown-part :deep(tbody tr:nth-child(even)) {
  background: rgba(125, 125, 125, 0.03);
}
.markdown-part :deep(tbody tr:hover) {
  background: var(--mf-surface-hover);
}

/* Streaming Mermaid Placeholder */
.streaming-mermaid-placeholder {
  margin: 10px 0;
  background: var(--mf-surface-muted);
  border: 1px dashed var(--mf-border-hover);
  border-radius: var(--mf-radius-md);
  overflow: hidden;
}

.placeholder-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 12px;
  background: var(--mf-primary-light);
  border-bottom: 1px solid var(--mf-primary-border);
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
  background: var(--mf-surface);
  color: var(--mf-text-main);
  font-family: var(--mf-font-mono, monospace);
  font-size: 11px;
  line-height: 1.5;
  overflow-x: auto;
  max-height: 160px;
}

/* 面板引用紧凑胶囊 */
.panel-ref-pill {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 8px 14px;
  margin: 4px 0;
  background: var(--mf-surface-card);
  border: 1px solid var(--mf-primary-border);
  border-radius: 12px;
  cursor: pointer;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.04);
  transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
}

.panel-ref-pill:hover {
  background: var(--mf-primary-light);
  border-color: var(--mf-primary);
  transform: translateY(-1px);
  box-shadow: 0 4px 14px rgba(0, 212, 255, 0.12);
}

.ref-pill-left {
  display: flex;
  align-items: center;
  gap: 8px;
}

.ref-pill-icon {
  font-size: 16px;
  color: var(--mf-primary);
}

.ref-pill-label {
  font-size: 13px;
  font-weight: 600;
  color: var(--mf-text-main);
}

.ref-pill-right {
  display: flex;
  align-items: center;
  gap: 4px;
  color: var(--mf-primary);
  font-size: 11.5px;
  font-weight: 500;
}

.ref-pill-arrow {
  font-size: 13px;
  transition: transform 0.15s;
}

.panel-ref-pill:hover .ref-pill-arrow {
  transform: translateX(3px);
}

/* 思考模式折叠卡片 */
.thinking-card {
  margin: 4px 0 12px 0;
  background: var(--mf-surface-muted, rgba(248, 250, 252, 0.6));
  border: 1px solid var(--mf-border-subtle, #e2e8f0);
  border-left: 3px solid var(--mf-primary);
  border-radius: var(--mf-radius-md, 8px);
  overflow: hidden;
  transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
}

.thinking-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 12px;
  cursor: pointer;
  user-select: none;
  background: transparent;
  transition: background 0.15s ease;
}

.thinking-header:hover {
  background: var(--mf-surface-hover, rgba(0, 0, 0, 0.03));
}

.thinking-header-left {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12.5px;
  font-weight: 500;
  color: var(--mf-text-secondary);
}

.thinking-icon {
  font-size: 15px;
  color: var(--mf-primary);
  flex-shrink: 0;
}

.spinning-sparkle {
  animation: sparkle-rotate 2s linear infinite;
}

@keyframes sparkle-rotate {
  0% { transform: rotate(0deg) scale(0.9); }
  50% { transform: rotate(180deg) scale(1.1); }
  100% { transform: rotate(360deg) scale(0.9); }
}

.thinking-status {
  color: var(--mf-text-main);
  font-weight: 500;
}

.thinking-duration {
  font-size: 11.5px;
  color: var(--mf-text-muted);
}

.thinking-header-right {
  display: flex;
  align-items: center;
  gap: 6px;
  color: var(--mf-text-secondary);
}

.thinking-action-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 24px;
  height: 24px;
  padding: 0;
  border: none;
  background: transparent;
  color: var(--mf-text-secondary);
  border-radius: 4px;
  cursor: pointer;
  font-size: 13px;
  transition: all 0.15s ease;
}

.thinking-action-btn:hover {
  background: var(--mf-surface-hover, rgba(0, 0, 0, 0.05));
  color: var(--mf-primary);
}

.thinking-caret {
  font-size: 14px;
  color: var(--mf-text-secondary);
  transition: transform 0.2s ease;
}

.thinking-content-body {
  padding: 10px 14px 12px 14px;
  border-top: 1px dashed var(--mf-border-subtle, #e2e8f0);
  font-size: 12.5px;
  line-height: 1.6;
  color: var(--mf-text-secondary);
  background: var(--mf-surface, #ffffff);
  max-height: 400px;
  overflow-y: auto;
}

.thinking-text-inner {
  white-space: pre-wrap;
  word-break: break-word;
  font-family: var(--mf-font-family, inherit);
}

/* 工具调用折叠卡片 */
.tool-card {
  margin: 4px 0 12px 0;
  background: var(--mf-surface-muted, rgba(248, 250, 252, 0.6));
  border: 1px solid var(--mf-border-subtle, #e2e8f0);
  border-left: 3px solid var(--mf-success, #16a34a);
  border-radius: var(--mf-radius-md, 8px);
  overflow: hidden;
}

.tool-card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 12px;
  cursor: pointer;
  user-select: none;
  transition: background 0.15s ease;
}

.tool-card-header:hover {
  background: var(--mf-surface-hover, rgba(0, 0, 0, 0.03));
}

.tool-card-left {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12.5px;
  font-weight: 500;
  color: var(--mf-text-secondary);
}

.tool-card-icon {
  font-size: 15px;
  color: var(--mf-success, #16a34a);
  flex-shrink: 0;
}

.tool-card-status {
  color: var(--mf-text-main);
  font-weight: 500;
}

.tool-card-caret {
  font-size: 14px;
  color: var(--mf-text-secondary);
}

.tool-card-body {
  padding: 10px 14px 12px;
  border-top: 1px dashed var(--mf-border-subtle, #e2e8f0);
  display: flex;
  flex-direction: column;
  gap: 10px;
  max-height: 400px;
  overflow-y: auto;
  background: var(--mf-surface, #ffffff);
}

.tool-call-item {
  border: 1px solid var(--mf-border-subtle, #e2e8f0);
  border-radius: 6px;
  padding: 8px 10px;
  background: var(--mf-surface-muted, rgba(248, 250, 252, 0.4));
}

.tool-call-item-header {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12.5px;
}

.tool-item-icon {
  font-size: 13px;
  color: var(--mf-success, #16a34a);
}

.tool-call-name {
  font-family: var(--font-mono, monospace);
  color: var(--mf-text-main);
  font-weight: 500;
}

.tool-item-pending {
  font-size: 11.5px;
  color: var(--mf-text-muted);
  margin-left: auto;
}

.tool-item-done {
  font-size: 11.5px;
  color: var(--mf-success, #16a34a);
  margin-left: auto;
}

.tool-call-args,
.tool-call-result {
  margin: 6px 0 0;
  padding: 8px;
  font-size: 11.5px;
  line-height: 1.5;
  font-family: var(--font-mono, monospace);
  white-space: pre-wrap;
  word-break: break-word;
  color: var(--mf-text-secondary);
  background: rgba(0, 0, 0, 0.03);
  border-radius: 4px;
  max-height: 160px;
  overflow-y: auto;
}

.tool-call-result {
  color: var(--mf-text-main);
}
</style>
