<template>
  <div class="workflow-config-card">
    <div class="card-header">
      <div class="header-left">
        <div class="wf-icon-box">
          <Icon :icon="config.icon || 'ph:sparkle'" />
        </div>
        <div class="title-group">
          <div class="wf-title">配置工作流：{{ config.title }}</div>
          <div class="wf-desc">{{ config.description }}</div>
        </div>
      </div>
      <button class="close-btn" title="取消配置" @click="cancel">
        <Icon icon="ph:x-bold" />
      </button>
    </div>

    <!-- Parameter Controls -->
    <div class="card-body">
      <!-- 1. 月份选择 (wf_monthly_review, wf_budget_guard) -->
      <div v-if="config.workflow_id === 'wf_monthly_review' || config.workflow_id === 'wf_budget_guard'" class="param-row">
        <label class="param-label">复盘/诊断月份</label>
        <div class="date-row">
          <el-date-picker
            v-model="paramMonth"
            type="month"
            value-format="YYYY-MM"
            placeholder="选择月份 (默认当月)"
            size="small"
            style="flex: 1;"
          />
          <el-button size="small" @click="paramMonth = currentMonthStr">当月</el-button>
          <el-button size="small" @click="paramMonth = prevMonthStr">上月</el-button>
        </div>
      </div>

      <!-- 2. 大额支出金额 (wf_expense_decision) -->
      <div v-else-if="config.workflow_id === 'wf_expense_decision'" class="param-row">
        <label class="param-label">拟支出金额 (￥)</label>
        <el-input-number v-model="paramAmount" :min="1" :step="1000" size="small" style="width: 100%;" />
        <div class="quick-chips">
          <span class="chip-btn" @click="paramAmount = 2000">￥2,000</span>
          <span class="chip-btn" @click="paramAmount = 5000">￥5,000</span>
          <span class="chip-btn" @click="paramAmount = 10000">￥10,000</span>
          <span class="chip-btn" @click="paramAmount = 20000">￥20,000</span>
        </div>
      </div>

      <!-- 3. 发薪日分流 (wf_payday_split) -->
      <div v-else-if="config.workflow_id === 'wf_payday_split'" class="param-group">
        <div class="label-with-hint">
          <span class="param-label">分配比例设定（生活 / 定投 / 还贷 / 应急）</span>
          <span class="sum-hint" :class="{ ok: ratioSum === 100 }">当前合计: {{ ratioSum }}%</span>
        </div>
        <div class="preset-chips">
          <button
            v-for="p in presets"
            :key="p.label"
            class="preset-chip"
            :class="{ active: isPreset(p) }"
            @click="applyPreset(p)"
          >
            {{ p.label }} ({{ p.living }}/{{ p.invest }}/{{ p.debt }}/{{ p.emergency }})
          </button>
        </div>
        <div class="ratio-inputs">
          <div class="ratio-col"><span class="r-label">生活</span><el-input-number v-model="rLiving" :min="0" :max="100" :step="5" size="small" style="width:100%" /></div>
          <div class="ratio-col"><span class="r-label">定投</span><el-input-number v-model="rInvest" :min="0" :max="100" :step="5" size="small" style="width:100%" /></div>
          <div class="ratio-col"><span class="r-label">还贷</span><el-input-number v-model="rDebt" :min="0" :max="100" :step="5" size="small" style="width:100%" /></div>
          <div class="ratio-col"><span class="r-label">应急</span><el-input-number v-model="rEmergency" :min="0" :max="100" :step="5" size="small" style="width:100%" /></div>
        </div>
      </div>

      <!-- 4. 回溯天数 (wf_anomaly_detect, wf_subscription_audit) -->
      <div v-else-if="config.workflow_id === 'wf_anomaly_detect' || config.workflow_id === 'wf_subscription_audit'" class="param-row">
        <label class="param-label">分析回溯天数</label>
        <el-slider v-model="paramLookback" :min="14" :max="365" :step="7" show-input size="small" />
      </div>

      <!-- 5. 目标覆盖月数 (wf_emergency_fund) -->
      <div v-else-if="config.workflow_id === 'wf_emergency_fund'" class="param-row">
        <label class="param-label">目标储备覆盖月数</label>
        <el-input-number v-model="paramTargetMonths" :min="1" :max="36" size="small" style="width: 100%" />
      </div>

      <!-- 6. 现金流预测周期 (wf_cashflow_forecast) -->
      <div v-else-if="config.workflow_id === 'wf_cashflow_forecast'" class="param-row">
        <label class="param-label">预测未来月数</label>
        <el-input-number v-model="paramHorizon" :min="3" :max="12" size="small" style="width: 100%" />
      </div>
    </div>

    <!-- Actions -->
    <div class="card-footer">
      <el-button size="small" @click="cancel">取消</el-button>
      <el-button size="small" type="primary" :loading="chatStore.isStreaming" @click="submit">
        <Icon icon="ph:lightning-bold" style="margin-right: 4px;" />
        <span>立即启动流水线</span>
      </el-button>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { Icon } from '@iconify/vue'
import { useChatStore } from '@/stores/chat'
import type { WorkflowConfigState } from '@/types'

const props = defineProps<{
  messageId?: number
  config: WorkflowConfigState
}>()

const chatStore = useChatStore()

const now = new Date()
const currentMonthStr = `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}`
const prevDate = new Date(now.getFullYear(), now.getMonth() - 1, 1)
const prevMonthStr = `${prevDate.getFullYear()}-${String(prevDate.getMonth() + 1).padStart(2, '0')}`

const paramMonth = ref(currentMonthStr)
const paramAmount = ref(5000)
const paramLookback = ref(props.config.workflow_id === 'wf_subscription_audit' ? 180 : 60)
const paramTargetMonths = ref(6)
const paramHorizon = ref(6)

// Payday ratios
const rLiving = ref(50)
const rInvest = ref(20)
const rDebt = ref(20)
const rEmergency = ref(10)

const presets = [
  { label: '稳健型', living: 50, invest: 20, debt: 20, emergency: 10 },
  { label: '激进型', living: 40, invest: 30, debt: 20, emergency: 10 },
  { label: '保守型', living: 60, invest: 10, debt: 20, emergency: 10 },
  { label: '均衡型', living: 35, invest: 25, debt: 25, emergency: 15 },
]

function isPreset(p: typeof presets[0]) {
  return rLiving.value === p.living && rInvest.value === p.invest && rDebt.value === p.debt && rEmergency.value === p.emergency
}

function applyPreset(p: typeof presets[0]) {
  rLiving.value = p.living
  rInvest.value = p.invest
  rDebt.value = p.debt
  rEmergency.value = p.emergency
}

const ratioSum = computed(() => rLiving.value + rInvest.value + rDebt.value + rEmergency.value)

function cancel() {
  if (props.messageId !== undefined) {
    chatStore.cancelStagedWorkflow(props.messageId)
  }
}

function submit() {
  if (props.messageId === undefined) return
  const params: Record<string, unknown> = {}
  const id = props.config.workflow_id

  if (id === 'wf_monthly_review' || id === 'wf_budget_guard') {
    params.month = paramMonth.value || currentMonthStr
  } else if (id === 'wf_expense_decision') {
    params.amount = paramAmount.value
  } else if (id === 'wf_payday_split') {
    params.ratio_living = rLiving.value
    params.ratio_invest = rInvest.value
    params.ratio_debt = rDebt.value
    params.ratio_emergency = rEmergency.value
  } else if (id === 'wf_anomaly_detect' || id === 'wf_subscription_audit') {
    params.lookback_days = paramLookback.value
  } else if (id === 'wf_emergency_fund') {
    params.target_months = paramTargetMonths.value
  } else if (id === 'wf_cashflow_forecast') {
    params.horizon = paramHorizon.value
  }

  chatStore.startStagedWorkflow(props.messageId, params)
}
</script>

<style scoped>
.workflow-config-card {
  margin: 10px 0;
  border-radius: var(--mf-radius-md, 8px);
  background: rgba(15, 23, 42, 0.85);
  border: 1px solid rgba(0, 212, 255, 0.25);
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.4);
  padding: 12px 16px;
  animation: fadeIn 0.2s ease-out;
}

@keyframes fadeIn {
  from { opacity: 0; transform: translateY(4px); }
  to { opacity: 1; transform: translateY(0); }
}

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding-bottom: 10px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
}

.header-left {
  display: flex;
  align-items: center;
  gap: 10px;
}

.wf-icon-box {
  width: 32px;
  height: 32px;
  border-radius: 8px;
  background: rgba(0, 212, 255, 0.15);
  color: var(--mf-primary, #00d4ff);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 18px;
  border: 1px solid rgba(0, 212, 255, 0.3);
}

.title-group .wf-title {
  font-size: 13px;
  font-weight: 600;
  color: #f1f5f9;
}

.title-group .wf-desc {
  font-size: 11px;
  color: #94a3b8;
}

.close-btn {
  background: transparent;
  border: none;
  color: #64748b;
  font-size: 14px;
  cursor: pointer;
}

.close-btn:hover {
  color: #ef4444;
}

.card-body {
  padding: 12px 0;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.param-label {
  font-size: 12px;
  color: #cbd5e1;
  font-weight: 500;
  margin-bottom: 4px;
  display: block;
}

.date-row {
  display: flex;
  gap: 6px;
}

.quick-chips, .preset-chips {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
  margin-top: 6px;
}

.chip-btn, .preset-chip {
  padding: 2px 8px;
  background: rgba(2, 6, 23, 0.7);
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 10px;
  font-size: 11px;
  color: #94a3b8;
  cursor: pointer;
  transition: all 0.15s ease;
}

.chip-btn:hover, .preset-chip:hover {
  border-color: rgba(0, 212, 255, 0.3);
  color: #e2e8f0;
}

.preset-chip.active {
  background: rgba(0, 212, 255, 0.15);
  border-color: rgba(0, 212, 255, 0.4);
  color: var(--mf-primary, #00d4ff);
}

.label-with-hint {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.sum-hint {
  font-size: 11px;
  color: #f87171;
}

.sum-hint.ok {
  color: #34d399;
}

.ratio-inputs {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 8px;
  margin-top: 6px;
}

.ratio-col {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.r-label {
  font-size: 10.5px;
  color: #94a3b8;
}

.card-footer {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  border-top: 1px solid rgba(255, 255, 255, 0.05);
  padding-top: 10px;
}
</style>
