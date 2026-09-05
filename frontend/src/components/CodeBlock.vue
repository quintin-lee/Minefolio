<template>
  <div class="code-block-wrapper">
    <div class="code-block-header">
      <div class="header-left">
        <span class="lang-badge">{{ displayLang }}</span>
      </div>
      <button class="copy-btn" :class="{ 'is-copied': copied }" @click="copyCode" :title="copied ? '已复制' : '复制代码'">
        <Icon :icon="copied ? 'ph:check' : 'ph:copy'" class="copy-icon" />
        <span class="copy-text">{{ copied ? '已复制' : '复制' }}</span>
      </button>
    </div>
    <div class="code-block-body">
      <pre class="code-pre"><code class="hljs" v-html="highlightedHtml"></code></pre>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref, onUnmounted } from 'vue'
import { Icon } from '@iconify/vue'
import hljs from 'highlight.js'
import 'highlight.js/styles/atom-one-dark.css'

const props = defineProps<{
  code: string
  lang?: string
}>()

const copied = ref(false)
let copyTimer: ReturnType<typeof setTimeout> | null = null

const highlightCache = new Map<string, string>()
const HIGHLIGHT_CACHE_LIMIT = 120

function cacheKey(code: string, lang: string): string {
  return `${lang}::${code.length}::${code.slice(0, 120)}`
}

const displayLang = computed(() => {
  const l = (props.lang || '').trim().toLowerCase()
  if (!l) return 'CODE'
  const map: Record<string, string> = {
    js: 'JAVASCRIPT',
    ts: 'TYPESCRIPT',
    py: 'PYTHON',
    sh: 'BASH',
    bash: 'BASH',
    yml: 'YAML',
    md: 'MARKDOWN',
  }
  return map[l] || l.toUpperCase()
})

const highlightedHtml = computed(() => {
  const rawCode = props.code || ''
  const l = (props.lang || '').trim().toLowerCase()
  const key = cacheKey(rawCode, l)
  const cached = highlightCache.get(key)
  if (cached !== undefined) return cached
  let html: string
  if (l && hljs.getLanguage(l)) {
    try {
      html = hljs.highlight(rawCode, { language: l }).value
    } catch {
      html = ''
    }
    if (!html) {
      try {
        html = hljs.highlightAuto(rawCode).value
      } catch {
        html = rawCode.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      }
    }
  } else {
    try {
      html = hljs.highlightAuto(rawCode).value
    } catch {
      html = rawCode.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    }
  }
  if (highlightCache.size >= HIGHLIGHT_CACHE_LIMIT) {
    const firstKey = highlightCache.keys().next().value as string
    highlightCache.delete(firstKey)
  }
  highlightCache.set(key, html)
  return html
})

async function copyCode() {
  try {
    await navigator.clipboard.writeText(props.code)
    copied.value = true
    if (copyTimer) clearTimeout(copyTimer)
    copyTimer = setTimeout(() => {
      copied.value = false
    }, 2000)
  } catch {
    // fallback or ignore
  }
}

onUnmounted(() => {
  if (copyTimer) clearTimeout(copyTimer)
})
</script>

<style scoped>
.code-block-wrapper {
  margin: 10px 0;
  border-radius: var(--mf-radius-md, 8px);
  background: var(--mf-surface-card);
  border: 1px solid var(--mf-border);
  box-shadow: var(--mf-shadow-sm);
  overflow: hidden;
}

.code-block-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 6px 12px;
  background: var(--mf-surface-hover);
  border-bottom: 1px solid var(--mf-border-subtle);
}

.lang-badge {
  font-family: var(--mf-font-mono, monospace);
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.5px;
  color: var(--mf-primary);
  background: var(--mf-primary-light);
  padding: 2px 8px;
  border-radius: 4px;
  border: 1px solid var(--mf-primary-border);
}

.copy-btn {
  display: flex;
  align-items: center;
  gap: 5px;
  background: transparent;
  border: 1px solid transparent;
  color: var(--mf-text-muted);
  font-size: 12px;
  padding: 3px 8px;
  border-radius: 4px;
  cursor: pointer;
  transition: all 0.2s ease;
}

.copy-btn:hover {
  color: var(--mf-text-main);
  background: var(--mf-primary-light);
  border-color: var(--mf-primary-border);
}

.copy-btn.is-copied {
  color: var(--mf-success);
  background: var(--mf-success-light);
  border-color: var(--mf-success-border);
}

.copy-icon {
  font-size: 14px;
}

.code-block-body {
  position: relative;
}

.code-pre {
  margin: 0;
  padding: 12px 16px;
  background: var(--mf-surface-card);
  overflow-x: auto;
  font-family: var(--mf-font-mono, monospace);
  font-size: 12.5px;
  line-height: 1.6;
}

.code-pre :deep(.hljs) {
  background: transparent;
  padding: 0;
  font-family: inherit;
  color: var(--mf-text-main);
}
</style>
