<template>
  <div class="workflow-card-wrapper" :class="`status-${workflowData.status}`">
    <!-- Header -->
    <div class="workflow-header" @click="expanded = !expanded" role="button" :aria-expanded="expanded" aria-label="工作流详情">
      <div class="header-left">
        <div class="wf-icon-badge" aria-hidden="true">
          <Icon icon="ph:git-merge" class="wf-icon" />
        </div>
        <div class="wf-title-group">
          <div class="wf-title">{{ workflowData.title }}</div>
          <div class="wf-subtitle">
            <span v-if="workflowData.status === 'running'" class="status-tag running">
              <span class="pulse-dot"></span>
              执行中 ({{ completedCount }}/{{ workflowData.total_steps }})
              <span v-if="etaText" class="eta-text">· 预计 {{ etaText }}</span>
            </span>
            <span v-else-if="workflowData.status === 'completed'" class="status-tag completed">
              <Icon icon="ph:check-bold" />
              已完成全部 {{ workflowData.total_steps }} 个流水线步骤
              <span v-if="elapsedTime" class="elapsed-text">· 耗时 {{ elapsedTime }}</span>
            </span>
            <span v-else class="status-tag error">
              <Icon icon="ph:warning-circle" />
              执行异常
            </span>
          </div>
        </div>
      </div>
      <div class="header-right">
        <button
          v-if="workflowData.status === 'error'"
          class="retry-btn"
          type="button"
          :title="workflowData.status === 'error' ? '重试此工作流' : ''"
          aria-label="重试工作流"
          @click.stop="retryWorkflow"
        >
          <Icon icon="ph:arrow-counter-clockwise" />
          <span>重试</span>
        </button>
        <button
          v-if="workflowData.status === 'completed'"
          class="trace-btn"
          type="button"
          title="查看追踪日志"
          aria-label="查看AI追踪日志"
          @click.stop="viewTraces"
        >
          <Icon icon="ph:stack" />
          <span>追踪</span>
        </button>
        <button
          class="toggle-btn"
          type="button"
          :title="expanded ? '收起步骤明细' : '展开步骤明细'"
          aria-label="切换步骤明细"
        >
          <Icon :icon="expanded ? 'ph:caret-up-bold' : 'ph:caret-down-bold'" />
        </button>
      </div>
    </div>

    <!-- Steps Timeline (Collapsible) -->
    <div v-show="expanded" class="workflow-steps-timeline">
      <div
        v-for="(step, idx) in workflowData.steps"
        :key="step.step_id || idx"
        class="timeline-step-row"
        :class="`step-${step.status}`"
      >
        <div class="step-indicator">
          <div class="step-icon-box">
            <Icon v-if="step.status === 'completed'" icon="ph:check-circle-fill" class="icon-completed" />
            <span v-else-if="step.status === 'running'" class="step-spinner"></span>
            <span v-else-if="step.status === 'error'" class="icon-error" aria-label="步骤失败">✕</span>
            <span v-else class="step-num">{{ idx + 1 }}</span>
          </div>
          <div v-if="idx < workflowData.steps.length - 1" class="step-line"></div>
        </div>

        <div class="step-content">
          <div class="step-header">
            <span class="step-title">{{ step.title }}</span>
            <span v-if="step.status === 'completed'" class="step-badge-done">已完成</span>
            <span v-else-if="step.status === 'running'" class="step-badge-running">分析处理中...</span>
            <span v-else-if="step.status === 'error'" class="step-badge-error">失败</span>
          </div>
          <div v-if="step.summary" class="step-summary">
            {{ step.summary }}
          </div>
        </div>
      </div>
    </div>

    <!-- Result Summary (when completed) -->
    <div v-if="workflowData.status === 'completed' && workflowResultSummary" class="workflow-result-summary">
      <div class="result-header">
        <Icon icon="ph:check-circle" class="result-icon" />
        <span class="result-title">执行结果摘要</span>
      </div>
      <div class="result-content">
        <div
          v-for="(item, i) in workflowResultSummary"
          :key="i"
          class="result-item"
        >
          <span class="result-item-label">{{ item.label }}:</span>
          <span class="result-item-value">{{ item.value }}</span>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref, watch, onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import { Icon } from '@iconify/vue'
import { ElMessage } from 'element-plus'
import type { WorkflowRunState, WorkflowStepState } from '@/types'
import { useChatStore } from '@/stores/chat'

const props = defineProps<{
  workflowData: WorkflowRunState
}>()

const router = useRouter()
const chatStore = useChatStore()

const expanded = ref(true)
const stepStartTime = ref<number | null>(null)
const elapsedTimeMs = ref(0)
let timerId: ReturnType<typeof setInterval> | null = null

const completedCount = computed(() => {
  return props.workflowData.steps.filter(s => s.status === 'completed').length
})

const hasError = computed(() =>
  props.workflowData.steps.some(s => s.status === 'error') ||
  props.workflowData.status === 'error'
)

// ETA estimation based on step progress
const etaText = computed(() => {
  if (props.workflowData.status !== 'running') return ''
  const total = props.workflowData.total_steps
  const done = completedCount.value
  if (total === 0 || done === 0) return ''
  // Assume ~3s per step as baseline
  const remainingSteps = total - done
  const etaSeconds = remainingSteps * 3
  if (etaSeconds < 60) return `${etaSeconds}秒`
  return `${Math.ceil(etaSeconds / 60)}分钟`
})

const elapsedTime = computed(() => {
  if (!props.workflowData.steps.length) return ''
  // Just show that it's done
  return ''
})

// Parse result summary from last assistant message content
const workflowResultSummary = computed(() => {
  if (props.workflowData.status !== 'completed') return null
  const msgs = chatStore.messages
  const wfMsg = msgs.find(m => m.workflowData?.workflow_id === props.workflowData.workflow_id)
  if (!wfMsg || !wfMsg.content) return null
  const summary: { label: string; value: string }[] = []
  // Extract key metrics from markdown content
  const lines = wfMsg.content.split('\n').filter(l => l.trim())
  for (const line of lines) {
    const match = line.match(/^[-*]\s*\*\*([^*]+)\*\*[:：]?\s*(.+)$/i)
    if (match) {
      summary.push({ label: match[1]!.trim(), value: match[2]!.trim() })
    }
  }
  return summary.length > 0 ? summary : null
})

function retryWorkflow() {
  if (!props.workflowData.workflow_id) return
  chatStore.runWorkflow(props.workflowData.workflow_id)
  ElMessage.info('正在重新执行工作流...')
}

function viewTraces() {
  router.push({ name: 'ai-traces', query: { workflow_id: props.workflowData.workflow_id } })
}

function startTimer() {
  stepStartTime.value = Date.now()
  timerId = setInterval(() => {
    if (stepStartTime.value !== null) {
      elapsedTimeMs.value = Date.now() - stepStartTime.value
    }
  }, 1000)
}

function stopTimer() {
  if (timerId !== null) {
    clearInterval(timerId)
    timerId = null
  }
}

watch(() => props.workflowData.status, (newStatus) => {
  if (newStatus === 'running') {
    startTimer()
    // Auto-expand during running
    expanded.value = true
  } else if (newStatus === 'completed') {
    stopTimer()
    // Auto-collapse after 3s delay
    setTimeout(() => {
      if (props.workflowData.status === 'completed') {
        expanded.value = false
      }
    }, 3000)
  } else if (newStatus === 'error') {
    stopTimer()
    // Keep expanded on error for visibility
    expanded.value = true
  }
})

onMounted(() => {
  if (props.workflowData.status === 'running') {
    startTimer()
  }
})

onUnmounted(() => {
  stopTimer()
})
</script>

<style scoped>
.workflow-card-wrapper {
  margin: 10px 0;
  border-radius: var(--mf-radius-md, 8px);
  background: rgba(15, 23, 42, 0.75);
  border: 1px solid rgba(0, 212, 255, 0.18);
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.35);
  overflow: hidden;
  backdrop-filter: blur(8px);
  transition: border-color 0.2s ease;
}

.workflow-card-wrapper.status-running {
  border-color: rgba(0, 212, 255, 0.4);
  box-shadow: 0 0 16px rgba(0, 212, 255, 0.12);
}

.workflow-card-wrapper.status-error {
  border-color: rgba(248, 113, 113, 0.4);
  box-shadow: 0 0 16px rgba(248, 113, 113, 0.12);
}

.workflow-card-wrapper.status-completed {
  border-color: rgba(52, 211, 153, 0.3);
}

.workflow-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 14px;
  background: rgba(0, 212, 255, 0.05);
  border-bottom: 1px solid rgba(0, 212, 255, 0.1);
  cursor: pointer;
  user-select: none;
}

.workflow-header:hover {
  background: rgba(0, 212, 255, 0.08);
}

.header-left {
  display: flex;
  align-items: center;
  gap: 10px;
}

.wf-icon-badge {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 32px;
  height: 32px;
  border-radius: 8px;
  background: linear-gradient(135deg, rgba(0, 212, 255, 0.2), rgba(99, 102, 241, 0.2));
  color: var(--mf-primary, #00d4ff);
  font-size: 18px;
  border: 1px solid rgba(0, 212, 255, 0.25);
}

.wf-title-group {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.wf-title {
  font-size: 13.5px;
  font-weight: 600;
  color: #f1f5f9;
  letter-spacing: 0.2px;
}

.wf-subtitle {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 4px;
}

.status-tag {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-size: 11px;
  font-weight: 500;
}

.status-tag.running {
  color: #38bdf8;
}

.status-tag.completed {
  color: #34d399;
}

.status-tag.error {
  color: #f87171;
}

.eta-text {
  color: #94a3b8;
  font-size: 10px;
}

.elapsed-text {
  color: #64748b;
  font-size: 10px;
  margin-left: 4px;
}

.pulse-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #38bdf8;
  box-shadow: 0 0 6px #38bdf8;
  animation: pulse 1.2s infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 1; transform: scale(1); }
  50% { opacity: 0.3; transform: scale(0.8); }
}

.header-right {
  display: flex;
  align-items: center;
  gap: 4px;
}

.toggle-btn,
.retry-btn,
.trace-btn {
  background: transparent;
  border: none;
  color: #94a3b8;
  font-size: 14px;
  cursor: pointer;
  padding: 4px;
  display: flex;
  align-items: center;
  gap: 4px;
  transition: color 0.15s ease;
}

.retry-btn {
  color: #f87171;
}

.retry-btn:hover {
  color: #ef4444;
}

.trace-btn {
  color: #818cf8;
}

.trace-btn:hover {
  color: #6366f1;
}

.retry-btn span,
.trace-btn span {
  font-size: 11px;
}

.workflow-steps-timeline {
  padding: 12px 16px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.timeline-step-row {
  display: flex;
  gap: 12px;
  position: relative;
}

.step-indicator {
  display: flex;
  flex-direction: column;
  align-items: center;
  width: 20px;
}

.step-icon-box {
  width: 20px;
  height: 20px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 11px;
  font-weight: 600;
  background: #1e293b;
  color: #64748b;
  border: 1px solid #334155;
  z-index: 1;
}

.step-completed .step-icon-box {
  background: rgba(52, 211, 153, 0.15);
  border-color: rgba(52, 211, 153, 0.4);
  color: #34d399;
}

.step-running .step-icon-box {
  background: rgba(56, 189, 248, 0.15);
  border-color: rgba(56, 189, 248, 0.5);
  color: #38bdf8;
}

.step-error .step-icon-box {
  background: rgba(248, 113, 113, 0.15);
  border-color: rgba(248, 113, 113, 0.4);
  color: #f87171;
}

.icon-completed {
  font-size: 14px;
  color: #34d399;
}

.icon-error {
  font-size: 12px;
  color: #f87171;
}

.step-spinner {
  width: 10px;
  height: 10px;
  border: 2px solid rgba(56, 189, 248, 0.2);
  border-top-color: #38bdf8;
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
}

@keyframes spin {
  to { transform: rotate(360deg); }
}

.step-line {
  flex: 1;
  width: 2px;
  background: #334155;
  margin: 4px 0;
}

.step-completed .step-line {
  background: rgba(52, 211, 153, 0.3);
}

.step-content {
  flex: 1;
  padding-bottom: 6px;
}

.step-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  font-size: 12.5px;
}

.step-title {
  font-weight: 500;
  color: #cbd5e1;
}

.step-running .step-title {
  color: #38bdf8;
  font-weight: 600;
}

.step-error .step-title {
  color: #f87171;
}

.step-badge-done {
  font-size: 10px;
  color: #34d399;
  background: rgba(52, 211, 153, 0.1);
  padding: 1px 6px;
  border-radius: 8px;
}

.step-badge-running {
  font-size: 10px;
  color: #38bdf8;
  background: rgba(56, 189, 248, 0.1);
  padding: 1px 6px;
  border-radius: 8px;
}

.step-badge-error {
  font-size: 10px;
  color: #f87171;
  background: rgba(248, 113, 113, 0.1);
  padding: 1px 6px;
  border-radius: 8px;
}

.step-summary {
  font-size: 11px;
  color: #94a3b8;
  margin-top: 4px;
  line-height: 1.4;
}

/* Result Summary */
.workflow-result-summary {
  border-top: 1px solid rgba(52, 211, 153, 0.2);
  padding: 12px 16px;
  background: rgba(52, 211, 153, 0.05);
}

.result-header {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-bottom: 8px;
}

.result-icon {
  color: #34d399;
  font-size: 16px;
}

.result-title {
  font-size: 12px;
  font-weight: 600;
  color: #34d399;
}

.result-content {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.result-item {
  display: flex;
  gap: 8px;
  font-size: 12px;
  padding: 4px 0;
  border-bottom: 1px solid rgba(255, 255, 255, 0.05);
}

.result-item:last-child {
  border-bottom: none;
}

.result-item-label {
  color: #94a3b8;
  flex-shrink: 0;
  min-width: 80px;
}

.result-item-value {
  color: #e2e8f0;
  word-break: break-word;
}
</style>
