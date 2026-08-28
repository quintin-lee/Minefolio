<template>
  <div class="workflow-card-wrapper" :class="`status-${workflowData.status}`">
    <!-- Header -->
    <div class="workflow-header" @click="expanded = !expanded">
      <div class="header-left">
        <div class="wf-icon-badge">
          <Icon icon="ph:git-merge" class="wf-icon" />
        </div>
        <div class="wf-title-group">
          <div class="wf-title">{{ workflowData.title }}</div>
          <div class="wf-subtitle">
            <span v-if="workflowData.status === 'running'" class="status-tag running">
              <span class="pulse-dot"></span>
              执行中 ({{ completedCount }}/{{ workflowData.total_steps }})
            </span>
            <span v-else-if="workflowData.status === 'completed'" class="status-tag completed">
              <Icon icon="ph:check-bold" />
              已完成全部 {{ workflowData.total_steps }} 个流水线步骤
            </span>
            <span v-else class="status-tag error">
              <Icon icon="ph:warning-circle" />
              执行异常
            </span>
          </div>
        </div>
      </div>
      <div class="header-right">
        <button class="toggle-btn" type="button" :title="expanded ? '收起步骤明细' : '展开步骤明细'">
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
            <span v-else-if="step.status === 'error'" class="icon-error">✕</span>
            <span v-else class="step-num">{{ idx + 1 }}</span>
          </div>
          <div v-if="idx < workflowData.steps.length - 1" class="step-line"></div>
        </div>

        <div class="step-content">
          <div class="step-header">
            <span class="step-title">{{ step.title }}</span>
            <span v-if="step.status === 'completed'" class="step-badge-done">已完成</span>
            <span v-else-if="step.status === 'running'" class="step-badge-running">分析处理中...</span>
          </div>
          <div v-if="step.summary" class="step-summary">
            {{ step.summary }}
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue'
import { Icon } from '@iconify/vue'
import type { WorkflowRunState } from '@/types'

const props = defineProps<{
  workflowData: WorkflowRunState
}>()

const expanded = ref(true)

const completedCount = computed(() => {
  return props.workflowData.steps.filter(s => s.status === 'completed').length
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

.toggle-btn {
  background: transparent;
  border: none;
  color: #94a3b8;
  font-size: 14px;
  cursor: pointer;
  padding: 4px;
  display: flex;
  align-items: center;
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

.icon-completed {
  font-size: 14px;
  color: #34d399;
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

.step-badge-done {
  font-size: 11px;
  color: #34d399;
  font-weight: 500;
}

.step-badge-running {
  font-size: 11px;
  color: #38bdf8;
  font-weight: 500;
}

.step-summary {
  font-size: 11.5px;
  color: #94a3b8;
  background: rgba(0, 0, 0, 0.25);
  padding: 4px 8px;
  border-radius: 4px;
  margin-top: 4px;
  border-left: 2px solid var(--mf-primary, #00d4ff);
}
</style>
