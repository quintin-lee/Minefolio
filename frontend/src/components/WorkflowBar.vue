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

        <div v-else-if="selectedWf.id === 'wf_budget_guard'" class="param-row">
          <label class="param-label">预算月份</label>
          <el-date-picker
            v-model="paramMonth"
            type="month"
            value-format="YYYY-MM"
            placeholder="选择预算月份 (默认当月)"
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

        <div v-else-if="selectedWf.id === 'wf_payday_split'" class="param-group">
          <div class="param-row">
            <label class="param-label">分配比例（总和 100%，留空使用 50/20/20/10）</label>
            <div class="ratio-grid">
              <div class="ratio-item"><span class="ratio-label">生活</span><el-input-number v-model="paramRatioLiving" :min="0" :max="100" :step="5" size="small" style="width: 100%" /></div>
              <div class="ratio-item"><span class="ratio-label">定投</span><el-input-number v-model="paramRatioInvest" :min="0" :max="100" :step="5" size="small" style="width: 100%" /></div>
              <div class="ratio-item"><span class="ratio-label">还贷</span><el-input-number v-model="paramRatioDebt" :min="0" :max="100" :step="5" size="small" style="width: 100%" /></div>
              <div class="ratio-item"><span class="ratio-label">应急</span><el-input-number v-model="paramRatioEmergency" :min="0" :max="100" :step="5" size="small" style="width: 100%" /></div>
            </div>
            <div v-if="ratioSum !== 100 && ratioSum !== 0" class="ratio-hint">当前总和 {{ ratioSum }}%，提交时将自动归一化</div>
          </div>
        </div>

        <div v-else-if="selectedWf.id === 'wf_anomaly_detect'" class="param-row">
          <label class="param-label">回溯天数</label>
          <el-input-number v-model="paramLookback" :min="7" :max="365" :step="10" style="width: 100%" />
          <div class="param-hint">默认 60 天，扫描最近 N 天的异常交易</div>
        </div>

        <div v-else-if="selectedWf.id === 'wf_subscription_audit'" class="param-row">
          <label class="param-label">回溯天数</label>
          <el-input-number v-model="paramLookback" :min="30" :max="365" :step="30" style="width: 100%" />
          <div class="param-hint">默认 180 天，识别周期性订阅</div>
        </div>

        <div v-else-if="selectedWf.id === 'wf_emergency_fund'" class="param-row">
          <label class="param-label">目标覆盖月数</label>
          <el-input-number v-model="paramTargetMonths" :min="1" :max="36" :step="1" style="width: 100%" />
          <div class="param-hint">默认 6 个月，按月均刚性支出测算</div>
        </div>

        <div v-else-if="selectedWf.id === 'wf_debt_payoff'" class="param-row">
          <label class="param-label">每月可用于还贷金额（￥，留空自动按最低还款模拟）</label>
          <el-input-number v-model="paramMonthlyPayment" :min="100" :step="500" style="width: 100%" placeholder="例如 5000" />
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
import { ref, computed, onMounted } from 'vue'
import { Icon } from '@iconify/vue'
import { useChatStore } from '@/stores/chat'
import type { WorkflowDef } from '@/types'

const chatStore = useChatStore()

const dialogVisible = ref(false)
const selectedWf = ref<WorkflowDef | null>(null)
const paramMonth = ref('')
const paramAmount = ref(5000)
const paramRatioLiving = ref<number | undefined>(undefined)
const paramRatioInvest = ref<number | undefined>(undefined)
const paramRatioDebt = ref<number | undefined>(undefined)
const paramRatioEmergency = ref<number | undefined>(undefined)
const paramLookback = ref(60)
const paramTargetMonths = ref(6)
const paramMonthlyPayment = ref<number | undefined>(undefined)

onMounted(async () => {
  if (chatStore.workflows.length === 0) {
    await chatStore.fetchWorkflowsList()
  }
})

const ratioSum = computed(() => {
  const a = paramRatioLiving.value ?? 0
  const b = paramRatioInvest.value ?? 0
  const c = paramRatioDebt.value ?? 0
  const d = paramRatioEmergency.value ?? 0
  const anySet = paramRatioLiving.value !== undefined || paramRatioInvest.value !== undefined || paramRatioDebt.value !== undefined || paramRatioEmergency.value !== undefined
  return anySet ? a + b + c + d : 0
})

const DIRECT_RUN_IDS = new Set(['wf_portfolio_rebalance', 'wf_goal_tracker'])

function handleCardClick(wf: WorkflowDef) {
  if (chatStore.isStreaming) return
  selectedWf.value = wf
  // Prefill lookback defaults per workflow
  if (wf.id === 'wf_anomaly_detect') paramLookback.value = 60
  else if (wf.id === 'wf_subscription_audit') paramLookback.value = 180
  if (DIRECT_RUN_IDS.has(wf.id)) {
    chatStore.runWorkflow(wf.id)
  } else {
    dialogVisible.value = true
  }
}

function confirmRun() {
  if (!selectedWf.value) return
  const params: Record<string, unknown> = {}
  const id = selectedWf.value.id
  if (id === 'wf_monthly_review' && paramMonth.value) {
    params.month = paramMonth.value
  } else if (id === 'wf_budget_guard' && paramMonth.value) {
    params.month = paramMonth.value
  } else if (id === 'wf_expense_decision' && paramAmount.value) {
    params.amount = paramAmount.value
  } else if (id === 'wf_payday_split') {
    if (paramRatioLiving.value !== undefined) params.ratio_living = paramRatioLiving.value
    if (paramRatioInvest.value !== undefined) params.ratio_invest = paramRatioInvest.value
    if (paramRatioDebt.value !== undefined) params.ratio_debt = paramRatioDebt.value
    if (paramRatioEmergency.value !== undefined) params.ratio_emergency = paramRatioEmergency.value
  } else if (id === 'wf_anomaly_detect') {
    if (paramLookback.value) params.lookback_days = paramLookback.value
  } else if (id === 'wf_subscription_audit') {
    if (paramLookback.value) params.lookback_days = paramLookback.value
  } else if (id === 'wf_emergency_fund') {
    if (paramTargetMonths.value) params.target_months = paramTargetMonths.value
  } else if (id === 'wf_debt_payoff') {
    if (paramMonthlyPayment.value) {
      params.monthly_payment = paramMonthlyPayment.value
      params.amount = paramMonthlyPayment.value
    }
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

.ratio-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
  margin-top: 4px;
}

.ratio-item {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.ratio-label {
  font-size: 11px;
  color: #94a3b8;
}

.ratio-hint,
.param-hint {
  font-size: 11px;
  color: #64748b;
  margin-top: 4px;
}

.param-group {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
</style>
