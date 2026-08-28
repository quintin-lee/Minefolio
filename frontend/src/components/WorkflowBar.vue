<template>
  <div class="workflow-bar">
    <div class="bar-header">
      <div class="header-tag">
        <Icon icon="ph:sparkle" class="sparkle-icon" />
        <span>智能财务工作流</span>
      </div>
      <span class="header-hint">一键启动自动化多步深度分析流水线</span>
    </div>

    <!-- Workflow Cards Carousel/Grid -->
    <div class="workflow-cards-container">
      <div
        v-for="wf in chatStore.workflows"
        :key="wf.id"
        class="workflow-chip-card"
        :class="{ 'is-disabled': chatStore.isStreaming }"
        @click="handleCardClick(wf)"
      >
        <div class="chip-icon-box">
          <Icon :icon="wf.icon || 'ph:git-merge'" />
        </div>
        <div class="chip-info">
          <div class="chip-title">{{ wf.title }}</div>
          <div class="chip-desc">{{ wf.description }}</div>
        </div>
        <div class="chip-action">
          <Icon icon="ph:play-circle-fill" class="play-icon" />
        </div>
      </div>
    </div>

    <!-- Parameter Dialog -->
    <el-dialog
      v-model="dialogVisible"
      :title="`启动工作流：${selectedWf?.title}`"
      width="420px"
      append-to-body
      class="workflow-params-dialog"
    >
      <div v-if="selectedWf" class="dialog-content">
        <p class="dialog-desc">{{ selectedWf.description }}</p>

        <!-- Workflow specific parameter inputs -->
        <div v-if="selectedWf.id === 'wf_monthly_review'" class="param-row">
          <label class="param-label">复盘月份</label>
          <el-date-picker
            v-model="paramMonth"
            type="month"
            value-format="YYYY-MM"
            placeholder="选择复盘月份 (默认当月)"
            style="width: 100%"
          />
        </div>

        <div v-else-if="selectedWf.id === 'wf_expense_decision'" class="param-row">
          <label class="param-label">拟支出金额 (￥)</label>
          <el-input-number
            v-model="paramAmount"
            :min="1"
            :step="1000"
            style="width: 100%"
            placeholder="输入计划支出金额"
          />
        </div>

        <div class="steps-preview">
          <div class="preview-title">流水线步骤预览：</div>
          <div class="preview-steps-list">
            <div v-for="(st, idx) in selectedWf.steps" :key="st.step_id" class="preview-step-item">
              <span class="step-dot">{{ idx + 1 }}</span>
              <span class="step-text">{{ st.title }}</span>
            </div>
          </div>
        </div>
      </div>

      <template #footer>
        <div class="dialog-footer">
          <el-button @click="dialogVisible = false">取消</el-button>
          <el-button type="primary" :loading="chatStore.isStreaming" @click="confirmRun">
            <Icon icon="ph:lightning-bold" style="margin-right: 4px;" />
            立即启动流水线
          </el-button>
        </div>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { Icon } from '@iconify/vue'
import { useChatStore } from '@/stores/chat'
import type { WorkflowDef } from '@/types'

const chatStore = useChatStore()

const dialogVisible = ref(false)
const selectedWf = ref<WorkflowDef | null>(null)
const paramMonth = ref('')
const paramAmount = ref(5000)

onMounted(async () => {
  if (chatStore.workflows.length === 0) {
    await chatStore.fetchWorkflowsList()
  }
})

function handleCardClick(wf: WorkflowDef) {
  if (chatStore.isStreaming) return
  selectedWf.value = wf
  if (wf.id === 'wf_portfolio_rebalance') {
    // Portfolio rebalance doesn't require extra parameters, run directly
    chatStore.runWorkflow(wf.id)
  } else {
    // Open parameter dialog
    dialogVisible.value = true
  }
}

function confirmRun() {
  if (!selectedWf.value) return
  const params: Record<string, unknown> = {}
  if (selectedWf.value.id === 'wf_monthly_review' && paramMonth.value) {
    params.month = paramMonth.value
  } else if (selectedWf.value.id === 'wf_expense_decision' && paramAmount.value) {
    params.amount = paramAmount.value
  }
  dialogVisible.value = false
  chatStore.runWorkflow(selectedWf.value.id, params)
}
</script>

<style scoped>
.workflow-bar {
  margin-bottom: 12px;
  background: rgba(15, 23, 42, 0.6);
  border: 1px solid rgba(0, 212, 255, 0.12);
  border-radius: var(--mf-radius-md, 8px);
  padding: 10px 14px;
}

.bar-header {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}

.header-tag {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 12px;
  font-weight: 600;
  color: var(--mf-primary, #00d4ff);
  letter-spacing: 0.2px;
}

.sparkle-icon {
  font-size: 14px;
}

.header-hint {
  font-size: 11.5px;
  color: #64748b;
}

.workflow-cards-container {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 8px;
}

.workflow-chip-card {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 8px 12px;
  background: rgba(2, 6, 23, 0.7);
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 6px;
  cursor: pointer;
  transition: all 0.2s ease;
}

.workflow-chip-card:hover:not(.is-disabled) {
  background: rgba(0, 212, 255, 0.06);
  border-color: rgba(0, 212, 255, 0.3);
  transform: translateY(-1px);
}

.workflow-chip-card.is-disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.chip-icon-box {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 28px;
  height: 28px;
  border-radius: 6px;
  background: rgba(0, 212, 255, 0.1);
  color: var(--mf-primary, #00d4ff);
  font-size: 16px;
  flex-shrink: 0;
}

.chip-info {
  flex: 1;
  min-width: 0;
}

.chip-title {
  font-size: 12.5px;
  font-weight: 600;
  color: #e2e8f0;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.chip-desc {
  font-size: 11px;
  color: #94a3b8;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.chip-action {
  color: #64748b;
  font-size: 18px;
  display: flex;
  align-items: center;
  transition: color 0.2s ease;
}

.workflow-chip-card:hover:not(.is-disabled) .chip-action {
  color: var(--mf-primary, #00d4ff);
}

.dialog-desc {
  font-size: 13px;
  color: #94a3b8;
  margin-bottom: 14px;
  line-height: 1.5;
}

.param-row {
  margin-bottom: 14px;
}

.param-label {
  display: block;
  font-size: 12px;
  font-weight: 500;
  color: #cbd5e1;
  margin-bottom: 6px;
}

.steps-preview {
  margin-top: 14px;
  background: rgba(15, 23, 42, 0.6);
  padding: 10px 12px;
  border-radius: 6px;
  border: 1px solid rgba(255, 255, 255, 0.05);
}

.preview-title {
  font-size: 11.5px;
  font-weight: 600;
  color: #cbd5e1;
  margin-bottom: 6px;
}

.preview-steps-list {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.preview-step-item {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: #94a3b8;
}

.step-dot {
  width: 16px;
  height: 16px;
  border-radius: 50%;
  background: rgba(0, 212, 255, 0.15);
  color: var(--mf-primary, #00d4ff);
  font-size: 10px;
  font-weight: 600;
  display: flex;
  align-items: center;
  justify-content: center;
}
</style>
