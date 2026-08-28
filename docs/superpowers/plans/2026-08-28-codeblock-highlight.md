# Chat CodeBlock Syntax Highlight Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create a componentized `CodeBlock.vue` with `highlight.js` syntax highlighting, language badge, and one-click copy button, and integrate it into `ChatMessageContent.vue`.

**Architecture:** Install `highlight.js`, create `CodeBlock.vue` to handle syntax highlighting and clipboard operations with dark theme styling, and enhance regex in `ChatMessageContent.vue` to segment generic code blocks into `<CodeBlock />`.

**Tech Stack:** Vue 3, TypeScript, `highlight.js`, `@iconify/vue`, DOMPurify.

---

### Task 1: Install `highlight.js` Dependency

**Files:**
- Modify: `frontend/package.json`

- [ ] **Step 1: Install `highlight.js`**

Run: `npm --prefix frontend install highlight.js`

- [ ] **Step 2: Commit**

```bash
git add frontend/package.json frontend/package-lock.json
git commit -m "build(frontend): 📦 add highlight.js dependency"
```

---

### Task 2: Create `CodeBlock.vue` Component

**Files:**
- Create: `frontend/src/components/CodeBlock.vue`

- [ ] **Step 1: Write `CodeBlock.vue` component with header, copy action, and syntax highlighting**

```vue
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
  if (l && hljs.getLanguage(l)) {
    try {
      return hljs.highlight(rawCode, { language: l }).value
    } catch {
      // fallback
    }
  }
  try {
    return hljs.highlightAuto(rawCode).value
  } catch {
    return rawCode.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
  }
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
  background: #020617;
  border: 1px solid rgba(0, 212, 255, 0.15);
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.4);
  overflow: hidden;
}

.code-block-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 6px 12px;
  background: rgba(15, 23, 42, 0.85);
  border-bottom: 1px solid rgba(0, 212, 255, 0.1);
}

.lang-badge {
  font-family: var(--mf-font-mono, monospace);
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.5px;
  color: var(--mf-primary, #00d4ff);
  background: rgba(0, 212, 255, 0.08);
  padding: 2px 8px;
  border-radius: 4px;
  border: 1px solid rgba(0, 212, 255, 0.2);
}

.copy-btn {
  display: flex;
  align-items: center;
  gap: 5px;
  background: transparent;
  border: 1px solid transparent;
  color: #94a3b8;
  font-size: 12px;
  padding: 3px 8px;
  border-radius: 4px;
  cursor: pointer;
  transition: all 0.2s ease;
}

.copy-btn:hover {
  color: #e2e8f0;
  background: rgba(255, 255, 255, 0.06);
  border-color: rgba(255, 255, 255, 0.1);
}

.copy-btn.is-copied {
  color: #34d399;
  background: rgba(52, 211, 153, 0.1);
  border-color: rgba(52, 211, 153, 0.25);
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
  background: #020617;
  overflow-x: auto;
  font-family: var(--mf-font-mono, monospace);
  font-size: 12.5px;
  line-height: 1.6;
}

.code-pre :deep(.hljs) {
  background: transparent;
  padding: 0;
  font-family: inherit;
  color: #e2e8f0;
}
</style>
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/components/CodeBlock.vue
git commit -m "feat(chat): ✨ create CodeBlock component with syntax highlighting and copy tool"
```

---

### Task 3: Integrate `CodeBlock` in `ChatMessageContent.vue`

**Files:**
- Modify: `frontend/src/components/ChatMessageContent.vue`

- [ ] **Step 1: Update `ChatMessageContent.vue` to segment generic code blocks into `type: 'code'`**

Update `ChatMessageContent.vue` to import and render `CodeBlock.vue` for all markdown code blocks while preserving `mermaid` and `action` specializations.

- [ ] **Step 2: Run build and tests**

Run: `npm --prefix frontend run build && npm --prefix frontend test`
Expected: 0 build errors, all tests PASS.

- [ ] **Step 3: Commit**

```bash
git add frontend/src/components/ChatMessageContent.vue
git commit -m "refactor(chat): ♻️ render code blocks via CodeBlock component in ChatMessageContent"
```
