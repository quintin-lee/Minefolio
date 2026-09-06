<template>
  <el-dialog v-model="visible" title="Trace 详情" width="800px" destroy-on-close>
    <div v-loading="loading" class="trace-detail">
      <template v-if="detail">
        <div class="detail-section">
          <h4>基本信息</h4>
          <div class="info-grid">
            <div class="info-item"><span class="label">ID</span><span class="value mono">{{ detail.id }}</span></div>
            <div class="info-item"><span class="label">供应商</span><span class="value">{{ detail.provider }}</span></div>
            <div class="info-item"><span class="label">模型</span><span class="value mono">{{ detail.model }}</span></div>
            <div class="info-item"><span class="label">状态</span>
              <el-tag :type="detail.status === 'ok' ? 'success' : 'danger'" effect="light" round size="small">
                {{ detail.status === 'ok' ? '成功' : '失败' }}
              </el-tag>
            </div>
            <div class="info-item"><span class="label">时间</span><span class="value mono">{{ formatDateTime(detail.created_at) }}</span></div>
            <div class="info-item"><span class="label">延迟</span><span class="value mono">{{ detail.latency_ms }}ms</span></div>
            <div class="info-item"><span class="label">首 Token</span><span class="value mono">{{ detail.first_token_ms }}ms</span></div>
            <div class="info-item"><span class="label">tok/s</span><span class="value mono">{{ Number(detail.tokens_per_sec || 0).toFixed(1) }}</span></div>
            <div class="info-item"><span class="label">Prompt Token</span><span class="value mono">{{ detail.prompt_tokens }}</span></div>
            <div class="info-item"><span class="label">Completion Token</span><span class="value mono">{{ detail.completion_tokens }}</span></div>
            <div class="info-item"><span class="label">Total Token</span><span class="value mono">{{ detail.total_tokens }}</span></div>
            <div class="info-item"><span class="label">Temperature</span><span class="value mono">{{ detail.temperature }}</span></div>
          </div>
        </div>

        <div class="detail-section" v-if="detail.error_message">
          <h4>错误信息</h4>
          <pre class="code-block error">{{ detail.error_message }}</pre>
        </div>

        <div class="detail-section">
          <h4>输入消息</h4>
          <div class="messages-container">
            <div v-for="(msg, i) in parsedMessages" :key="i" class="message-item" :class="msg.role">
              <div class="message-role">{{ msg.role === 'system' ? 'System' : msg.role === 'user' ? 'User' : 'Assistant' }}</div>
              <pre class="message-content">{{ msg.content }}</pre>
            </div>
          </div>
        </div>

        <div class="detail-section">
          <h4>输出内容</h4>
          <pre class="code-block output">{{ detail.output_content || '(empty)' }}</pre>
        </div>
      </template>
    </div>
  </el-dialog>
</template>

<script setup lang="ts">
import { ref, watch, computed } from 'vue'
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

const parsedMessages = computed(() => {
  if (!detail.value?.input_messages) return []
  try {
    return JSON.parse(detail.value.input_messages) as { role: string; content: string }[]
  } catch {
    return []
  }
})

function formatDateTime(s: string) { return s ? s.replace('T', ' ').slice(0, 19) : '—' }

watch(() => props.traceId, async (id) => {
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
})
</script>

<style scoped>
.trace-detail {
  min-height: 200px;
}

.detail-section {
  margin-bottom: 24px;
}

.detail-section h4 {
  font-size: 14px;
  font-weight: 600;
  color: var(--mf-text-main);
  margin-bottom: 12px;
  padding-bottom: 8px;
  border-bottom: 1px solid var(--mf-border);
}

.info-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 12px;
}

.info-item {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.info-item .label {
  font-size: 12px;
  color: var(--mf-text-muted);
}

.info-item .value {
  font-size: 14px;
  color: var(--mf-text-main);
}

.mono {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
}

.messages-container {
  display: flex;
  flex-direction: column;
  gap: 12px;
  max-height: 400px;
  overflow-y: auto;
}

.message-item {
  border-radius: 8px;
  padding: 12px;
  background: var(--mf-surface-muted);
}

.message-item.system {
  background: rgba(139, 92, 246, 0.1);
  border-left: 3px solid #8b5cf6;
}

.message-item.user {
  background: rgba(59, 130, 246, 0.1);
  border-left: 3px solid #3b82f6;
}

.message-item.assistant {
  background: rgba(16, 185, 129, 0.1);
  border-left: 3px solid #10b981;
}

.message-role {
  font-size: 12px;
  font-weight: 600;
  color: var(--mf-text-muted);
  margin-bottom: 6px;
  text-transform: uppercase;
}

.message-content {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-size: 13px;
  color: var(--mf-text-main);
  white-space: pre-wrap;
  word-break: break-all;
  margin: 0;
}

.code-block {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-size: 13px;
  background: var(--mf-surface);
  border-radius: 8px;
  padding: 12px;
  white-space: pre-wrap;
  word-break: break-all;
  max-height: 300px;
  overflow-y: auto;
  margin: 0;
  color: var(--mf-text-main);
}

.code-block.error {
  border-left: 3px solid var(--mf-danger);
  color: var(--mf-danger);
}

.code-block.output {
  border-left: 3px solid #10b981;
}
</style>
