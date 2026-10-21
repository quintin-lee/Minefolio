<template>
  <el-drawer
    v-model="visible"
    direction="rtl"
    :size="drawerSize"
    :with-header="false"
    destroy-on-close
    class="ai-trace-drawer"
  >
    <div v-loading="loading" class="trace-workspace">
      <template v-if="detail">
        <!-- 顶部 Header: 状态与操作 -->
        <header class="drawer-header">
          <div class="header-left">
            <span class="trace-id-badge" @click="copyText(String(detail.id), 'Trace ID')" title="点击复制 ID">
              <Icon icon="ph:hash-bold" class="hash-icon" />
              <span>{{ detail.id }}</span>
              <Icon icon="ph:copy" class="mini-copy-icon" />
            </span>
            <span class="model-pill">
              <Icon icon="ph:cpu-bold" class="pill-icon" />
              <span>{{ detail.model || 'Unknown' }}</span>
              <span class="provider-tag" v-if="detail.provider">({{ detail.provider }})</span>
            </span>
            <el-tag
              :type="detail.status === 'ok' ? 'success' : 'danger'"
              effect="light"
              round
              size="small"
              class="status-tag"
            >
              {{ detail.status === 'ok' ? '200 OK' : 'FAILED' }}
            </el-tag>
            <span class="created-time">{{ formatDateTime(detail.created_at) }}</span>
          </div>

          <div class="header-right">
            <button
              class="header-btn"
              @click="isMaximized = !isMaximized"
              :title="isMaximized ? '还原窗口' : '全屏展开'"
              aria-label="切换全屏"
            >
              <Icon :icon="isMaximized ? 'ph:corners-in-bold' : 'ph:corners-out-bold'" />
              <span>{{ isMaximized ? '还原' : '全屏' }}</span>
            </button>
            <button
              class="header-btn"
              @click="copyText(formattedJson, '原始报文')"
              title="复制完整 JSON 报文"
              aria-label="复制原始 JSON"
            >
              <Icon icon="ph:brackets-curly-bold" />
              <span>复制 JSON</span>
            </button>
            <button
              class="header-btn close-btn"
              @click="visible = false"
              title="关闭详情"
              aria-label="关闭"
            >
              <Icon icon="ph:x-bold" />
            </button>
          </div>
        </header>

        <!-- 顶部 Bento 性能与成本指标带 -->
        <div class="trace-metrics-bar">
          <div class="metric-card">
            <div class="metric-header">
              <Icon icon="ph:timer-bold" class="metric-icon latency" />
              <span class="metric-label">总延迟 / 首 Token</span>
            </div>
            <div class="metric-value-row">
              <span class="metric-value">{{ detail.latency_ms ?? 0 }}<small>ms</small></span>
              <span class="metric-sub" v-if="detail.first_token_ms">TTFT {{ detail.first_token_ms }}ms</span>
            </div>
          </div>

          <div class="metric-card">
            <div class="metric-header">
              <Icon icon="ph:coins-bold" class="metric-icon tokens" />
              <span class="metric-label">Token 消耗总计</span>
            </div>
            <div class="metric-value-row">
              <span class="metric-value">{{ detail.total_tokens?.toLocaleString() ?? 0 }}</span>
              <span class="metric-sub">P:{{ detail.prompt_tokens ?? 0 }} / C:{{ detail.completion_tokens ?? 0 }}</span>
            </div>
          </div>

          <div class="metric-card">
            <div class="metric-header">
              <Icon icon="ph:lightning-bold" class="metric-icon speed" />
              <span class="metric-label">生成速率</span>
            </div>
            <div class="metric-value-row">
              <span class="metric-value">{{ Number(detail.tokens_per_sec || 0).toFixed(1) }}<small>tok/s</small></span>
              <span class="metric-sub">比率 {{ tokenRatio }}</span>
            </div>
          </div>

          <div class="metric-card">
            <div class="metric-header">
              <Icon icon="ph:currency-circle-dollar-bold" class="metric-icon cost" />
              <span class="metric-label">预估调用成本</span>
            </div>
            <div class="metric-value-row">
              <span class="metric-value">{{ formattedCost(detail.cost_usd) }}</span>
              <span class="metric-sub">USD</span>
            </div>
          </div>

          <div class="metric-card">
            <div class="metric-header">
              <Icon icon="ph:sliders-horizontal-bold" class="metric-icon params" />
              <span class="metric-label">采样参数</span>
            </div>
            <div class="metric-value-row">
              <span class="metric-value">T: {{ detail.temperature ?? '1.0' }}</span>
              <span class="metric-sub">Top_P: {{ detail.top_p ?? '1.0' }}</span>
            </div>
          </div>
        </div>

        <!-- 错误横幅 (若调用失败) -->
        <div v-if="detail.error_message" class="trace-error-banner">
          <Icon icon="ph:warning-circle-bold" class="error-banner-icon" />
          <div class="error-banner-content">
            <div class="error-banner-title">调用异常 / 错误信息</div>
            <div class="error-banner-text">{{ detail.error_message }}</div>
          </div>
          <button class="error-copy-btn" @click="copyText(detail.error_message, '错误信息')">
            <Icon icon="ph:copy" />
            <span>复制</span>
          </button>
        </div>

        <!-- 标签页导航 -->
        <div class="trace-tabs-bar">
          <el-tabs v-model="activeTab" class="modern-trace-tabs">
            <el-tab-pane label="对话与输出" name="messages" />
            <el-tab-pane label="原始报文 (Raw JSON)" name="raw" />
            <el-tab-pane label="调用参数与元数据" name="params" />
          </el-tabs>
        </div>

        <!-- 工作区内容 -->
        <main class="drawer-main-content">
          <!-- TAB 1: 对话与输出分栏工作区 -->
          <div v-if="activeTab === 'messages'" class="workspace-split">
            <!-- 左栏: 输入上下文 -->
            <section class="pane prompt-pane">
              <div class="pane-header">
                <div class="pane-title">
                  <Icon icon="ph:arrow-elbow-down-right-bold" class="pane-icon prompt" />
                  <span>输入上下文</span>
                  <span class="count-badge" v-if="parsedMessages.length > 0">({{ parsedMessages.length }})</span>
                </div>
                <button
                  class="pane-action-btn"
                  @click="copyAllPrompts"
                  title="复制所有输入提示词"
                >
                  <Icon icon="ph:copy" />
                  <span>复制输入</span>
                </button>
              </div>

              <div class="pane-body messages-scroll">
                <template v-if="parsedMessages.length > 0">
                  <div
                    v-for="(msg, i) in parsedMessages"
                    :key="i"
                    class="message-card"
                    :class="msg.role"
                  >
                    <div class="card-header">
                      <span class="role-badge" :class="msg.role">
                        {{ msg.role.toUpperCase() }}
                      </span>
                      <span class="seq-tag">#{{ i + 1 }}</span>
                      <button
                        class="card-copy-btn"
                        @click="copyText(msg.content, `${msg.role} 消息`)"
                        title="复制此条消息"
                      >
                        <Icon icon="ph:copy" />
                      </button>
                    </div>
                    <pre class="card-content mono-text">{{ msg.content }}</pre>
                  </div>
                </template>
                <div v-else class="empty-hint">
                  <Icon icon="ph:chat-circle-dots" class="empty-icon" />
                  <span>无结构化输入上下文</span>
                </div>
              </div>
            </section>

            <!-- 右栏: 模型输出 -->
            <section class="pane output-pane">
              <div class="pane-header">
                <div class="pane-title">
                  <Icon icon="ph:sparkle-bold" class="pane-icon output" />
                  <span>模型回复 (Completion)</span>
                </div>
                <div class="pane-actions">
                  <div class="view-toggle">
                    <button
                      class="toggle-btn"
                      :class="{ active: viewMode === 'markdown' }"
                      @click="viewMode = 'markdown'"
                    >
                      <Icon icon="ph:article-bold" />
                      <span>Markdown</span>
                    </button>
                    <button
                      class="toggle-btn"
                      :class="{ active: viewMode === 'raw' }"
                      @click="viewMode = 'raw'"
                    >
                      <Icon icon="ph:code-bold" />
                      <span>纯文本</span>
                    </button>
                  </div>
                  <button
                    class="pane-action-btn primary"
                    @click="copyText(detail.output_content, '模型回复')"
                    title="复制回复正文"
                  >
                    <Icon icon="ph:copy" />
                    <span>复制回复</span>
                  </button>
                </div>
              </div>

              <div class="pane-body output-scroll">
                <div
                  v-if="viewMode === 'markdown'"
                  class="rendered-markdown markdown-body"
                  v-html="renderedOutput"
                ></div>
                <pre
                  v-else
                  class="raw-output mono-text"
                >{{ detail.output_content || '(空输出)' }}</pre>
              </div>
            </section>
          </div>

          <!-- TAB 2: 原始报文 JSON 视图 -->
          <div v-else-if="activeTab === 'raw'" class="pane raw-json-pane">
            <div class="pane-header">
              <div class="pane-title">
                <Icon icon="ph:file-code-bold" class="pane-icon" />
                <span>完整 Trace JSON 报文</span>
              </div>
              <button class="pane-action-btn primary" @click="copyText(formattedJson, '原始 JSON')">
                <Icon icon="ph:copy" />
                <span>复制完整 JSON</span>
              </button>
            </div>
            <div class="pane-body json-scroll">
              <pre class="json-code mono-text">{{ formattedJson }}</pre>
            </div>
          </div>

          <!-- TAB 3: 运行参数与元数据表格 -->
          <div v-else-if="activeTab === 'params'" class="pane params-pane">
            <div class="pane-header">
              <div class="pane-title">
                <Icon icon="ph:table-bold" class="pane-icon" />
                <span>元数据与调用参数明细</span>
              </div>
            </div>
            <div class="pane-body params-scroll">
              <table class="params-table">
                <tbody>
                  <tr><td class="param-key">Trace ID</td><td class="param-val mono-text">{{ detail.id }}</td></tr>
                  <tr><td class="param-key">用户 ID (User ID)</td><td class="param-val mono-text">{{ detail.user_id }}</td></tr>
                  <tr><td class="param-key">会话 ID (Session ID)</td><td class="param-val mono-text">{{ detail.session_id || '0 (临时调用)' }}</td></tr>
                  <tr><td class="param-key">供应商 (Provider)</td><td class="param-val">{{ detail.provider || '—' }}</td></tr>
                  <tr><td class="param-key">模型名称 (Model)</td><td class="param-val mono-text">{{ detail.model || '—' }}</td></tr>
                  <tr><td class="param-key">状态 (Status)</td><td class="param-val">{{ detail.status }}</td></tr>
                  <tr><td class="param-key">调用时间 (Created At)</td><td class="param-val mono-text">{{ formatDateTime(detail.created_at) }}</td></tr>
                  <tr><td class="param-key">总耗时 (Latency)</td><td class="param-val mono-text">{{ detail.latency_ms }} ms</td></tr>
                  <tr><td class="param-key">首字耗时 (First Token TTFT)</td><td class="param-val mono-text">{{ detail.first_token_ms }} ms</td></tr>
                  <tr><td class="param-key">生成速率 (Tokens/sec)</td><td class="param-val mono-text">{{ Number(detail.tokens_per_sec || 0).toFixed(2) }} tok/s</td></tr>
                  <tr><td class="param-key">Prompt Tokens</td><td class="param-val mono-text">{{ detail.prompt_tokens }}</td></tr>
                  <tr><td class="param-key">Completion Tokens</td><td class="param-val mono-text">{{ detail.completion_tokens }}</td></tr>
                  <tr><td class="param-key">Total Tokens</td><td class="param-val mono-text">{{ detail.total_tokens }}</td></tr>
                  <tr><td class="param-key">预估成本 (Estimated Cost)</td><td class="param-val mono-text">{{ formattedCost(detail.cost_usd) }} USD</td></tr>
                  <tr><td class="param-key">采样温度 (Temperature)</td><td class="param-val mono-text">{{ detail.temperature ?? '—' }}</td></tr>
                  <tr><td class="param-key">Top P</td><td class="param-val mono-text">{{ detail.top_p ?? '—' }}</td></tr>
                  <tr><td class="param-key">Max Tokens</td><td class="param-val mono-text">{{ detail.max_tokens ?? '—' }}</td></tr>
                  <tr v-if="detail.metadata"><td class="param-key">扩展元数据 (Metadata)</td><td class="param-val mono-text">{{ detail.metadata }}</td></tr>
                </tbody>
              </table>
            </div>
          </div>
        </main>
      </template>
    </div>
  </el-drawer>
</template>

<script setup lang="ts">
import { ref, watch, computed, onMounted, onUnmounted } from 'vue'
import { Icon } from '@iconify/vue'
import { ElMessage } from 'element-plus'
import { marked } from 'marked'
import DOMPurify from 'dompurify'
import { getTrace } from '@/api/ai-trace'
import type { AiTraceDetail } from '@/api/ai-trace'

const props = defineProps<{ modelValue: boolean; traceId: number }>()
const emit = defineEmits<{ 'update:modelValue': [v: boolean] }>()

const visible = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})

const loading = ref(false)
const detail = ref<AiTraceDetail | null>(null)
const isMaximized = ref(false)
const isMobile = ref(false)
const activeTab = ref('messages')
const viewMode = ref<'markdown' | 'raw'>('markdown')

function checkMobile() {
  if (typeof window !== 'undefined') {
    isMobile.value = window.innerWidth < 768
  }
}

onMounted(() => {
  checkMobile()
  window.addEventListener('resize', checkMobile)
})

onUnmounted(() => {
  window.removeEventListener('resize', checkMobile)
})

const drawerSize = computed(() => {
  if (isMobile.value || isMaximized.value) return '100%'
  return '75%'
})

const parsedMessages = computed(() => {
  if (!detail.value) return []
  const list: { role: string; content: string }[] = []
  let hasSystemInMessages = false

  if (detail.value.input_messages) {
    try {
      const parsed = JSON.parse(detail.value.input_messages)
      if (Array.isArray(parsed)) {
        for (const item of parsed) {
          if (item && typeof item === 'object') {
            const role = String(item.role || 'user').toLowerCase()
            const content = typeof item.content === 'string' ? item.content : JSON.stringify(item.content, null, 2)
            if (role === 'system') hasSystemInMessages = true
            list.push({ role, content })
          }
        }
      }
    } catch {
      list.push({ role: 'user', content: detail.value.input_messages })
    }
  }

  if (detail.value.system_prompt && !hasSystemInMessages) {
    list.unshift({ role: 'system', content: detail.value.system_prompt })
  }

  return list
})

const renderedOutput = computed(() => {
  const content = detail.value?.output_content
  if (!content) return '<p class="text-muted">(空回复)</p>'
  try {
    const rawHtml = marked.parse(content, { async: false, breaks: true }) as string
    return DOMPurify.sanitize(rawHtml)
  } catch {
    return `<pre>${content}</pre>`
  }
})

const formattedJson = computed(() => {
  if (!detail.value) return ''
  return JSON.stringify(detail.value, null, 2)
})

const tokenRatio = computed(() => {
  if (!detail.value) return '—'
  const p = detail.value.prompt_tokens || 0
  const c = detail.value.completion_tokens || 0
  if (p === 0 && c === 0) return '—'
  return `${p}:${c}`
})

function formatDateTime(s?: string) {
  return s ? s.replace('T', ' ').slice(0, 19) : '—'
}

function formattedCost(val?: number) {
  if (val === undefined || val === null || isNaN(val) || val === 0) return '$0.0000'
  if (val < 0.0001) return '< $0.0001'
  return `$${val.toFixed(4)}`
}

async function copyText(text: string, label = '内容') {
  if (!text) return
  try {
    if (navigator?.clipboard?.writeText) {
      await navigator.clipboard.writeText(text)
    } else {
      const ta = document.createElement('textarea')
      ta.value = text
      document.body.appendChild(ta)
      ta.select()
      document.execCommand('copy')
      document.body.removeChild(ta)
    }
    ElMessage.success(`已复制${label}`)
  } catch {
    ElMessage.error('复制失败')
  }
}

function copyAllPrompts() {
  if (!parsedMessages.value.length) return
  const text = parsedMessages.value
    .map((m, idx) => `[${m.role.toUpperCase()} #${idx + 1}]\n${m.content}`)
    .join('\n\n---\n\n')
  copyText(text, '全部输入提示词')
}

watch(
  () => props.traceId,
  async (id) => {
    if (!id) return
    loading.value = true
    detail.value = null
    try {
      const raw = (await getTrace(id)) as unknown
      const res = (raw && typeof raw === 'object' && 'data' in raw
        ? (raw as { data: AiTraceDetail }).data
        : raw) as AiTraceDetail
      detail.value = res || null
    } catch (e) {
      console.error('[AiTraceDetail] load failed:', e)
    } finally {
      loading.value = false
    }
  },
  { immediate: true }
)
</script>

<style scoped>
/* 抽屉样式与容器 */
:deep(.el-drawer__body) {
  padding: 0 !important;
  display: flex;
  flex-direction: column;
  height: 100%;
  overflow: hidden;
  background: var(--mf-background);
}

.trace-workspace {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
  color: var(--mf-text-main);
  background: var(--mf-background);
}

/* 顶部 Header */
.drawer-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 20px;
  background: var(--mf-surface);
  border-bottom: 1px solid var(--mf-border);
  flex-shrink: 0;
  gap: 12px;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}

.trace-id-badge {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 3px 8px;
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border);
  border-radius: 6px;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.15s ease;
}
.trace-id-badge:hover {
  border-color: var(--mf-primary);
  color: var(--mf-primary);
}
.hash-icon {
  font-size: 12px;
  color: var(--mf-text-muted);
}
.mini-copy-icon {
  font-size: 12px;
  opacity: 0.6;
}

.model-pill {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 3px 10px;
  background: var(--mf-surface-muted);
  border-radius: 6px;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-size: 12px;
  color: var(--mf-text-main);
}
.pill-icon {
  font-size: 13px;
  color: var(--mf-primary);
}
.provider-tag {
  color: var(--mf-text-muted);
  font-size: 11px;
}

.status-tag {
  font-weight: 600;
  font-size: 11px;
}

.created-time {
  font-size: 12px;
  color: var(--mf-text-muted);
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 8px;
}

.header-btn {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 6px 10px;
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border);
  border-radius: 6px;
  font-size: 12px;
  color: var(--mf-text-main);
  cursor: pointer;
  transition: all 0.15s ease;
}
.header-btn:hover {
  background: var(--mf-surface);
  border-color: var(--mf-primary);
  color: var(--mf-primary);
}
.header-btn.close-btn {
  padding: 6px 8px;
}
.header-btn.close-btn:hover {
  border-color: var(--mf-danger, #ef4444);
  color: var(--mf-danger, #ef4444);
}

/* 顶部 Bento 性能指标横带 */
.trace-metrics-bar {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
  gap: 10px;
  padding: 12px 20px;
  background: var(--mf-surface);
  border-bottom: 1px solid var(--mf-border);
  flex-shrink: 0;
}

.metric-card {
  display: flex;
  flex-direction: column;
  gap: 4px;
  padding: 10px 12px;
  background: var(--mf-background);
  border: 1px solid var(--mf-border);
  border-radius: 8px;
  transition: border-color 0.15s;
}
.metric-card:hover {
  border-color: rgba(var(--mf-primary-rgb, 59, 130, 246), 0.4);
}

.metric-header {
  display: flex;
  align-items: center;
  gap: 6px;
}
.metric-icon {
  font-size: 14px;
}
.metric-icon.latency { color: #f59e0b; }
.metric-icon.tokens { color: #3b82f6; }
.metric-icon.speed { color: #10b981; }
.metric-icon.cost { color: #ec4899; }
.metric-icon.params { color: #8b5cf6; }

.metric-label {
  font-size: 11px;
  color: var(--mf-text-muted);
  font-weight: 500;
}

.metric-value-row {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 6px;
}

.metric-value {
  font-size: 15px;
  font-weight: 700;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  color: var(--mf-text-main);
}
.metric-value small {
  font-size: 11px;
  font-weight: 500;
  color: var(--mf-text-muted);
  margin-left: 2px;
}

.metric-sub {
  font-size: 11px;
  color: var(--mf-text-muted);
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
}

/* 错误横幅 */
.trace-error-banner {
  display: flex;
  align-items: center;
  gap: 12px;
  margin: 12px 20px 0;
  padding: 10px 14px;
  background: rgba(239, 68, 68, 0.08);
  border: 1px solid rgba(239, 68, 68, 0.3);
  border-radius: 8px;
  color: var(--mf-danger, #ef4444);
  flex-shrink: 0;
}
.error-banner-icon {
  font-size: 20px;
  flex-shrink: 0;
}
.error-banner-content {
  flex: 1;
  min-width: 0;
}
.error-banner-title {
  font-size: 12px;
  font-weight: 700;
}
.error-banner-text {
  font-size: 12px;
  white-space: pre-wrap;
  word-break: break-all;
  margin-top: 2px;
}
.error-copy-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 4px 8px;
  background: transparent;
  border: 1px solid rgba(239, 68, 68, 0.4);
  border-radius: 6px;
  color: var(--mf-danger, #ef4444);
  font-size: 11px;
  cursor: pointer;
}
.error-copy-btn:hover {
  background: rgba(239, 68, 68, 0.15);
}

/* 标签页栏 */
.trace-tabs-bar {
  padding: 0 20px;
  background: var(--mf-surface);
  border-bottom: 1px solid var(--mf-border);
  flex-shrink: 0;
}
.modern-trace-tabs :deep(.el-tabs__header) {
  margin: 0;
}
.modern-trace-tabs :deep(.el-tabs__nav-wrap::after) {
  display: none;
}
.modern-trace-tabs :deep(.el-tabs__item) {
  font-size: 13px;
  font-weight: 500;
  padding: 0 16px;
  height: 42px;
  line-height: 42px;
}

/* 核心工作区 */
.drawer-main-content {
  flex: 1;
  min-height: 0;
  overflow: hidden;
  display: flex;
  flex-direction: column;
  padding: 16px 20px;
}

/* 左右分栏工作区 */
.workspace-split {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
  height: 100%;
  min-height: 0;
}

@media (max-width: 992px) {
  .workspace-split {
    grid-template-columns: 1fr;
    height: auto;
    overflow-y: auto;
  }
}

/* 通用分栏 Pane 结构 */
.pane {
  display: flex;
  flex-direction: column;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 10px;
  overflow: hidden;
  height: 100%;
  min-height: 0;
}

.pane-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 14px;
  background: var(--mf-surface-muted);
  border-bottom: 1px solid var(--mf-border);
  flex-shrink: 0;
  gap: 8px;
}

.pane-title {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  font-weight: 600;
  color: var(--mf-text-main);
}
.pane-icon {
  font-size: 15px;
}
.pane-icon.prompt { color: #8b5cf6; }
.pane-icon.output { color: #10b981; }

.count-badge {
  font-size: 11px;
  color: var(--mf-text-muted);
  font-weight: 400;
}

.pane-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}

.pane-action-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 4px 8px;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 6px;
  font-size: 11px;
  color: var(--mf-text-main);
  cursor: pointer;
  transition: all 0.15s ease;
}
.pane-action-btn:hover {
  border-color: var(--mf-primary);
  color: var(--mf-primary);
}
.pane-action-btn.primary {
  background: var(--mf-primary-light, rgba(59, 130, 246, 0.1));
  border-color: var(--mf-primary);
  color: var(--mf-primary);
}

.view-toggle {
  display: inline-flex;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 6px;
  padding: 2px;
  gap: 2px;
}
.toggle-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 2px 8px;
  border: none;
  background: transparent;
  color: var(--mf-text-muted);
  font-size: 11px;
  border-radius: 4px;
  cursor: pointer;
  transition: all 0.15s ease;
}
.toggle-btn.active {
  background: var(--mf-primary);
  color: #fff;
  font-weight: 500;
}

.pane-body {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  padding: 14px;
}

/* 消息流与卡片 */
.messages-scroll {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.message-card {
  border-radius: 8px;
  padding: 12px;
  background: var(--mf-background);
  border: 1px solid var(--mf-border);
  transition: border-color 0.15s;
}
.message-card:hover {
  border-color: rgba(var(--mf-primary-rgb, 59, 130, 246), 0.3);
}

.message-card.system {
  border-left: 3px solid #8b5cf6;
  background: rgba(139, 92, 246, 0.04);
}
.message-card.user {
  border-left: 3px solid #3b82f6;
  background: rgba(59, 130, 246, 0.04);
}
.message-card.assistant {
  border-left: 3px solid #10b981;
  background: rgba(16, 185, 129, 0.04);
}
.message-card.tool {
  border-left: 3px solid #f59e0b;
  background: rgba(245, 158, 11, 0.04);
}

.card-header {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-bottom: 8px;
}

.role-badge {
  font-size: 10px;
  font-weight: 700;
  padding: 2px 6px;
  border-radius: 4px;
  letter-spacing: 0.05em;
}
.role-badge.system { background: rgba(139, 92, 246, 0.15); color: #8b5cf6; }
.role-badge.user { background: rgba(59, 130, 246, 0.15); color: #3b82f6; }
.role-badge.assistant { background: rgba(16, 185, 129, 0.15); color: #10b981; }
.role-badge.tool { background: rgba(245, 158, 11, 0.15); color: #f59e0b; }

.seq-tag {
  font-size: 11px;
  color: var(--mf-text-placeholder);
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
}

.card-copy-btn {
  margin-left: auto;
  background: transparent;
  border: none;
  color: var(--mf-text-muted);
  cursor: pointer;
  padding: 2px 4px;
  border-radius: 4px;
  display: inline-flex;
  align-items: center;
}
.card-copy-btn:hover {
  color: var(--mf-primary);
  background: var(--mf-surface-muted);
}

.card-content {
  margin: 0;
  font-size: 12.5px;
  line-height: 1.6;
  white-space: pre-wrap;
  word-break: break-all;
  color: var(--mf-text-main);
}

.mono-text {
  font-family: 'JetBrains Mono', 'Fira Code', 'Cascadia Code', monospace;
}

/* 模型回复区渲染 */
.output-scroll {
  background: var(--mf-background);
}

.rendered-markdown {
  font-size: 13.5px;
  line-height: 1.65;
  color: var(--mf-text-main);
}
.rendered-markdown :deep(h1),
.rendered-markdown :deep(h2),
.rendered-markdown :deep(h3) {
  margin-top: 14px;
  margin-bottom: 8px;
  color: var(--mf-text-main);
}
.rendered-markdown :deep(p) {
  margin-bottom: 10px;
}
.rendered-markdown :deep(table) {
  width: 100%;
  border-collapse: collapse;
  margin: 10px 0;
  font-size: 12.5px;
}
.rendered-markdown :deep(th),
.rendered-markdown :deep(td) {
  padding: 6px 10px;
  border: 1px solid var(--mf-border);
}
.rendered-markdown :deep(th) {
  background: var(--mf-surface-muted);
  font-weight: 600;
}
.rendered-markdown :deep(pre) {
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  padding: 10px;
  border-radius: 6px;
  overflow-x: auto;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-size: 12px;
}
.rendered-markdown :deep(code) {
  background: var(--mf-surface-muted);
  padding: 2px 5px;
  border-radius: 4px;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-size: 12px;
}

.raw-output {
  margin: 0;
  font-size: 13px;
  line-height: 1.6;
  white-space: pre-wrap;
  word-break: break-all;
  color: var(--mf-text-main);
}

.empty-hint {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 40px 0;
  color: var(--mf-text-muted);
  font-size: 13px;
  gap: 8px;
}
.empty-icon {
  font-size: 28px;
}

/* 原始 JSON 视图 */
.raw-json-pane,
.params-pane {
  height: 100%;
}
.json-scroll {
  background: var(--mf-background);
}
.json-code {
  margin: 0;
  font-size: 12px;
  line-height: 1.5;
  color: var(--mf-text-main);
}

/* 运行参数表 */
.params-scroll {
  background: var(--mf-background);
}
.params-table {
  width: 100%;
  border-collapse: collapse;
}
.params-table td {
  padding: 10px 14px;
  border-bottom: 1px solid var(--mf-border);
  font-size: 13px;
}
.param-key {
  width: 220px;
  color: var(--mf-text-muted);
  font-weight: 500;
}
.param-val {
  color: var(--mf-text-main);
}
</style>
