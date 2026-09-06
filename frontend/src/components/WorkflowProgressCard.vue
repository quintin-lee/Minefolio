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
              <span v-if="formattedElapsed" class="elapsed-text">· 已耗时 {{ formattedElapsed }}</span>
              <span v-if="etaText" class="eta-text">· 预计剩余 {{ etaText }}</span>
            </span>
            <span v-else-if="workflowData.status === 'completed'" class="status-tag completed">
              <Icon icon="ph:check-bold" />
              已完成全部 {{ workflowData.total_steps }} 个流水线步骤
              <span v-if="formattedElapsed" class="elapsed-text">· 总耗时 {{ formattedElapsed }}</span>
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
          title="重试此工作流"
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

    <!-- Contextual Actions Bar & Follow-up Chips (when completed) -->
    <div v-if="workflowData.status === 'completed'" class="workflow-actions-wrapper">
      <div class="action-buttons-row">
        <!-- Workflow specific action buttons -->
        <button
          v-if="workflowData.workflow_id === 'wf_portfolio_rebalance'"
          class="action-pill-btn primary"
          @click="navigateToTransactions"
        >
          <Icon icon="ph:arrows-left-right-bold" />
          <span>跳转记账 / 调仓管理</span>
        </button>

        <button
          v-else-if="workflowData.workflow_id === 'wf_budget_guard' || workflowData.workflow_id === 'wf_expense_decision'"
          class="action-pill-btn primary"
          @click="navigateToCategories"
        >
          <Icon icon="ph:sliders-horizontal-bold" />
          <span>调整分类预算上限</span>
        </button>

        <button
          v-else-if="workflowData.workflow_id === 'wf_subscription_audit'"
          class="action-pill-btn primary"
          @click="navigateToDailyExpenses"
        >
          <Icon icon="ph:receipt-bold" />
          <span>查看日常开支明细</span>
        </button>

        <!-- Common actions -->
        <button class="action-pill-btn" @click="copySummary">
          <Icon icon="ph:copy" />
          <span>复制精炼摘要</span>
        </button>

        <button class="action-pill-btn" @click="exportReport">
          <Icon icon="ph:download-simple" />
          <span>导出分析报告</span>
        </button>
      </div>

      <!-- Follow-up Prompt Chips -->
      <div v-if="followUpPrompts.length > 0" class="follow-up-prompts-row">
        <span class="follow-up-label">
          <Icon icon="ph:sparkle" />
          <span>智能追问：</span>
        </span>
        <button
          v-for="(prompt, idx) in followUpPrompts"
          :key="idx"
          class="follow-up-chip"
          :disabled="chatStore.isStreaming"
          @click="sendFollowUp(prompt)"
        >
          <span>{{ prompt }}</span>
          <Icon icon="ph:arrow-up-right-bold" class="chip-arrow" />
        </button>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref, watch, onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import { Icon } from '@iconify/vue'
import { ElMessage } from 'element-plus'
import type { WorkflowRunState } from '@/types'
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

const formattedElapsed = computed(() => {
  const s = Math.floor(elapsedTimeMs.value / 1000)
  if (s <= 0) return ''
  if (s < 60) return `${s}秒`
  const m = Math.floor(s / 60)
  const rem = s % 60
  return `${m}分${rem}秒`
})

// ETA estimation based on step progress
const etaText = computed(() => {
  if (props.workflowData.status !== 'running') return ''
  const total = props.workflowData.total_steps
  const done = completedCount.value
  if (total === 0 || done === 0) return ''
  const remainingSteps = total - done
  const etaSeconds = remainingSteps * 3
  if (etaSeconds < 60) return `${etaSeconds}秒`
  return `${Math.ceil(etaSeconds / 60)}分钟`
})

// Parse result summary from assistant message content
const workflowResultSummary = computed(() => {
  if (props.workflowData.status !== 'completed') return null
  const msgs = chatStore.messages
  const wfMsg = msgs.find(m => m.workflowData?.workflow_id === props.workflowData.workflow_id)
  if (!wfMsg || !wfMsg.content) return null
  const summary: { label: string; value: string }[] = []
  const lines = wfMsg.content.split('\n').filter(l => l.trim())
  for (const line of lines) {
    const match = line.match(/^[-*]\s*\*\*([^*]+)\*\*[:：]?\s*(.+)$/i)
    if (match) {
      summary.push({ label: match[1]!.trim(), value: match[2]!.trim() })
    }
  }
  return summary.length > 0 ? summary : null
})

// Contextual follow-up suggestions
const followUpPrompts = computed(() => {
  const id = props.workflowData.workflow_id
  if (id === 'wf_monthly_review') {
    return [
      '分析本月支出占比最高的三个品类及异常交易',
      '对比上月收支并给出下月刚性开支削减建议',
    ]
  } else if (id === 'wf_portfolio_rebalance') {
    return [
      '测算分批调仓执行对当前资金流的影响',
      '查看各类资产的历史年化收益与波动率',
    ]
  } else if (id === 'wf_budget_guard' || id === 'wf_expense_decision') {
    return [
      '如果进行这笔支出，未来3个月现金流安全吗？',
      '帮我测算若采用分期方式还款的总利息成本',
    ]
  } else if (id === 'wf_payday_split') {
    return [
      '按激进型（生活40/定投30/还贷20/应急10）重新测算',
      '查看我当前的应急金账户余额是否满足刚性需求',
    ]
  } else if (id === 'wf_cashflow_forecast') {
    return [
      '若发生一笔 10000 元的突发支出，现金流会在哪个月见底？',
      '推荐合理的流动资金收益增强理财配比',
    ]
  }
  return [
    '结合我的历史资产给出针对性的优化行动项',
    '将上述分析结论生成一份执行备忘录',
  ]
})

function retryWorkflow() {
  if (!props.workflowData.workflow_id) return
  chatStore.runWorkflow(props.workflowData.workflow_id)
  ElMessage.info('正在重新执行工作流...')
}

function viewTraces() {
  router.push({ name: 'ai-traces', query: { workflow_id: props.workflowData.workflow_id } })
}

function navigateToTransactions() {
  router.push({ path: '/transactions' })
}

function navigateToCategories() {
  router.push({ path: '/categories' })
}

function navigateToDailyExpenses() {
  router.push({ path: '/daily-expenses' })
}

function copySummary() {
  if (workflowResultSummary.value && workflowResultSummary.value.length > 0) {
    const text = workflowResultSummary.value.map(i => `${i.label}: ${i.value}`).join('\n')
    navigator.clipboard.writeText(text)
    ElMessage.success('已复制执行结果摘要到剪贴板')
  } else {
    const msgs = chatStore.messages
    const wfMsg = msgs.find(m => m.workflowData?.workflow_id === props.workflowData.workflow_id)
    if (wfMsg?.content) {
      navigator.clipboard.writeText(wfMsg.content)
      ElMessage.success('已复制分析报告全文')
    }
  }
}

function exportReport() {
  const msgs = chatStore.messages
  const wfMsg = msgs.find(m => m.workflowData?.workflow_id === props.workflowData.workflow_id)
  const content = wfMsg?.content || `${props.workflowData.title} 分析报告`
  const blob = new Blob([content], { type: 'text/markdown;charset=utf-8' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `${props.workflowData.title}_${new Date().toISOString().slice(0, 10)}.md`
  a.click()
  URL.revokeObjectURL(url)
  ElMessage.success('报告导出成功')
}

function sendFollowUp(prompt: string) {
  chatStore.sendMessage(prompt)
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
    expanded.value = true
  } else if (newStatus === 'completed') {
    stopTimer()
    setTimeout(() => {
      if (props.workflowData.status === 'completed') {
        expanded.value = false
      }
    }, 2000)
  } else if (newStatus === 'error') {
    stopTimer()
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
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border);
  box-shadow: var(--mf-shadow-sm);
  overflow: hidden;
  backdrop-filter: blur(8px);
  transition: border-color 0.2s ease;
}

.workflow-card-wrapper.status-running {
  border-color: var(--mf-primary-border);
  box-shadow: var(--mf-shadow-glow);
}
.workflow-card-wrapper.status-error {
  border-color: var(--mf-danger-border);
  box-shadow: 0 0 16px var(--mf-danger-light);
}
.workflow-card-wrapper.status-completed {
  border-color: var(--mf-success-border);
}

.workflow-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 14px;
  background: var(--mf-surface-hover);
  border-bottom: 1px solid var(--mf-border);
  cursor: pointer;
  user-select: none;
}
.workflow-header:hover {
  background: var(--mf-primary-light);
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
  background: linear-gradient(135deg, var(--mf-primary-light), rgba(99, 102, 241, 0.12));
  color: var(--mf-primary);
  font-size: 18px;
  border: 1px solid var(--mf-primary-border);
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
  color: var(--mf-info);
}
.status-tag.completed {
  color: var(--mf-success);
}
.status-tag.error {
  color: var(--mf-danger);
}

.eta-text, .elapsed-text {
  color: var(--mf-text-muted);
  font-size: 10.5px;
  margin-left: 2px;
}

.pulse-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--mf-info);
  box-shadow: 0 0 6px var(--mf-info);
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
  background: var(--mf-surface-muted);
  color: var(--mf-text-muted);
  border: 1px solid var(--mf-border);
  z-index: 1;
}

.step-completed .step-icon-box {
  background: var(--mf-success-light);
  border-color: var(--mf-success-border);
  color: var(--mf-success);
}
.step-running .step-icon-box {
  background: var(--mf-info-light);
  border-color: var(--mf-info-border);
  color: var(--mf-info);
}
.step-error .step-icon-box {
  background: var(--mf-danger-light);
  border-color: var(--mf-danger-border);
  color: var(--mf-danger);
}

.step-error .step-icon-box {
  background: rgba(248, 113, 113, 0.15);
  border-color: rgba(248, 113, 113, 0.4);
  color: #f87171;
}

.icon-completed {
  font-size: 14px;
  color: var(--mf-success);
}
.icon-error {
  font-size: 12px;
  color: var(--mf-danger);
}

.step-spinner {
  width: 10px;
  height: 10px;
  border: 2px solid var(--mf-info-light);
  border-top-color: var(--mf-info);
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
}

@keyframes spin {
  to { transform: rotate(360deg); }
}

.step-line {
  flex: 1;
  width: 2px;
  background: var(--mf-border);
  margin: 4px 0;
}

.step-completed .step-line {
  background: var(--mf-success-light);
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
  color: var(--mf-text-regular);
}
.step-running .step-title {
  color: var(--mf-info);
  font-weight: 600;
}
.step-error .step-title {
  color: var(--mf-danger);
}


.step-badge-done {
  font-size: 10px;
  color: var(--mf-success);
  background: var(--mf-success-light);
  padding: 1px 6px;
  border-radius: 8px;
}
.step-badge-running {
  font-size: 10px;
  color: var(--mf-info);
  background: var(--mf-info-light);
  padding: 1px 6px;
  border-radius: 8px;
}
.step-badge-error {
  font-size: 10px;
  color: var(--mf-danger);
  background: var(--mf-danger-light);
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
  padding: 10px 14px;
  background: rgba(52, 211, 153, 0.05);
}

.result-header {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-bottom: 6px;
}

.result-icon {
  color: #34d399;
  font-size: 15px;
}

.result-title {
  font-size: 12px;
  font-weight: 600;
  color: #34d399;
}

.result-content {
  display: flex;
  flex-direction: column;
  gap: 3px;
}

.result-item {
  display: flex;
  gap: 8px;
  font-size: 11.5px;
  padding: 3px 0;
  border-bottom: 1px solid rgba(255, 255, 255, 0.04);
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

/* Actions Wrapper */
.workflow-actions-wrapper {
  padding: 10px 14px;
  background: rgba(2, 6, 23, 0.6);
  border-top: 1px solid rgba(255, 255, 255, 0.06);
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.action-buttons-row {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.action-pill-btn {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 4px 10px;
  background: rgba(15, 23, 42, 0.8);
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 12px;
  font-size: 11px;
  color: #cbd5e1;
  cursor: pointer;
  transition: all 0.15s ease;
}

.action-pill-btn:hover {
  background: var(--mf-primary-light);
  border-color: var(--mf-primary-border);
  color: var(--mf-text-main);
}
.action-pill-btn.primary {
  background: var(--mf-primary-light);
  border-color: var(--mf-primary-border);
  color: var(--mf-primary);
  font-weight: 500;
}
.action-pill-btn.primary:hover {
  background: var(--mf-primary);
  border-color: var(--mf-primary);
  color: var(--mf-text-main);
}

/* Follow-up Prompts */
.follow-up-prompts-row {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 6px;
}

.follow-up-label {
  display: inline-flex;
  align-items: center;
  gap: 3px;
  font-size: 11px;
  color: var(--mf-primary);
  font-weight: 500;
  flex-shrink: 0;
}

.follow-up-chip {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 3px 9px;
  background: rgba(30, 41, 59, 0.6);
  border: 1px solid rgba(56, 189, 248, 0.2);
  border-radius: 12px;
  font-size: 11px;
  color: #94a3b8;
  cursor: pointer;
  transition: all 0.15s ease;
}

.follow-up-chip:hover:not(:disabled) {
  background: rgba(56, 189, 248, 0.15);
  border-color: rgba(56, 189, 248, 0.4);
  color: #e2e8f0;
  transform: translateY(-1px);
}

.follow-up-chip:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.chip-arrow {
  font-size: 10px;
  color: var(--mf-primary);
}
</style>
