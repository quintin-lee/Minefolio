# 智能财务工作流交互优化实现计划 (Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 重构 Minefolio 的 AI 财务工作流交互链路，实现紧凑胶囊条（36px）、输入框 "/" 斜杠指令呼出、对话流内联参数配置卡片（Inline Form）、执行耗时监控与结果业务闭环（调仓/预算联动与智能追问）。

**Architecture:** 
- 前端 Pinia Store (`chat.ts`) 增加工作流配置暂存（Staging）、参数持久化与置顶列表管理；
- 新增 `WorkflowSlashMenu.vue` 浮动指令面板与 `WorkflowConfigCard.vue` 内联参数卡片；
- 重构 `WorkflowBar.vue` 为单行置顶胶囊栏 + 全量抽屉（Drawer）；
- 增强 `WorkflowProgressCard.vue` 支持真实步骤执行耗时、上下文业务操作（一键生成调仓、调整预算）与智能追问气泡。

**Tech Stack:** Vue 3, TypeScript, Pinia, Element Plus, Iconify, Vite, C23 SSE.

---

### Task 1: 扩展类型定义与 Pinia Chat Store（工作流暂存与置顶管理）

**Files:**
- Modify: `frontend/src/types/index.ts:160-175`
- Modify: `frontend/src/api/ai.ts:20-35`
- Modify: `frontend/src/stores/chat.ts:20-40, 460-560`

- [ ] **Step 1: 在 `types/index.ts` 和 `api/ai.ts` 中扩充工作流配置与阶段状态类型**

```typescript
// 在 frontend/src/types/index.ts 中追加:
export interface WorkflowConfigState {
  workflow_id: string
  title: string
  icon: string
  description: string
  initialParams?: Record<string, unknown>
}

// 在 frontend/src/api/ai.ts 的 AiMessage 接口中追加:
export interface AiMessage {
  id: number
  session_id: number
  role: 'user' | 'assistant'
  content: string
  model?: string
  created_at: string
  workflowData?: WorkflowRunState
  workflowConfig?: WorkflowConfigState // 新增：内联配置状态
}
```

- [ ] **Step 2: 在 `stores/chat.ts` 中实现置顶持久化与内联配置暂存方法**

```typescript
// 置顶工作流持久化 Key 与默认列表
const PINNED_STORAGE_KEY = 'minefolio_pinned_workflows'
const DEFAULT_PINNED_IDS = ['wf_monthly_review', 'wf_portfolio_rebalance', 'wf_payday_split', 'wf_budget_guard']

// State
const pinnedWorkflowIds = ref<string[]>(
  JSON.parse(localStorage.getItem(PINNED_STORAGE_KEY) || 'null') || DEFAULT_PINNED_IDS
)

function togglePinWorkflow(id: string) {
  const idx = pinnedWorkflowIds.value.indexOf(id)
  if (idx >= 0) {
    pinnedWorkflowIds.value.splice(idx, 1)
  } else {
    pinnedWorkflowIds.value.push(id)
  }
  localStorage.setItem(PINNED_STORAGE_KEY, JSON.stringify(pinnedWorkflowIds.value))
}

// 暂存需参数的工作流到对话流（插入内联配置卡片）
async function stageWorkflow(workflowId: string, initialParams?: Record<string, unknown>) {
  if (isStreaming.value) return
  const targetWf = workflows.value.find(w => w.id === workflowId)
  if (!targetWf) return
  const sid = currentSessionId.value
  if (!sid) await createNewSession()

  const configMsg: AiMessage = reactive({
    id: Date.now(),
    session_id: currentSessionId.value!,
    role: 'assistant',
    content: '',
    workflowConfig: {
      workflow_id: targetWf.id,
      title: targetWf.title,
      icon: targetWf.icon || 'ph:sparkle',
      description: targetWf.description,
      initialParams,
    },
    created_at: new Date().toISOString(),
  })
  messages.value.push(configMsg)
}

function cancelStagedWorkflow(messageId: number) {
  const idx = messages.value.findIndex(m => m.id === messageId)
  if (idx !== -1) {
    messages.value.splice(idx, 1)
  }
}

// 从内联卡片启动工作流：就地转为 WorkflowProgressCard 并开始流式
async function startStagedWorkflow(messageId: number, params?: Record<string, unknown>) {
  const msg = messages.value.find(m => m.id === messageId)
  if (!msg || !msg.workflowConfig || isStreaming.value) return
  const workflowId = msg.workflowConfig.workflow_id
  const targetWf = workflows.value.find(w => w.id === workflowId)
  const wfTitle = targetWf ? targetWf.title : msg.workflowConfig.title

  // 1. 初始化步骤并就地替换为 workflowData
  const initialSteps: WorkflowStepState[] = targetWf
    ? targetWf.steps.map((st, idx) => ({
        step_index: idx,
        step_id: st.step_id,
        title: st.title,
        status: 'pending' as const,
      }))
    : []

  const wfState: WorkflowRunState = reactive({
    workflow_id: workflowId,
    title: wfTitle,
    total_steps: targetWf ? targetWf.step_count : 4,
    status: 'running',
    steps: initialSteps,
  })

  delete msg.workflowConfig
  msg.workflowData = wfState
  activeWorkflow.value = wfState

  // 2. 触发流式
  abortCurrentStream()
  activeAbortController = new AbortController()
  const signal = activeAbortController.signal
  enableTypewriterBuffer.value = true
  isStreaming.value = true
  const writer = new SmoothStreamWriter(msg)

  try {
    for await (const chunk of runWorkflowStream(
      {
        workflow_id: workflowId,
        session_id: currentSessionId.value ?? undefined,
        params,
      },
      signal
    )) {
      if (chunk.type === 'workflow_start') {
        if (chunk.title) wfState.title = chunk.title
        if (chunk.total_steps) wfState.total_steps = chunk.total_steps
      } else if (chunk.type === 'step_start' && typeof chunk.step_index === 'number') {
        const idx = chunk.step_index
        while (wfState.steps.length <= idx) {
          wfState.steps.push({
            step_index: wfState.steps.length,
            step_id: chunk.step_id || `step_${wfState.steps.length}`,
            title: chunk.title || `步骤 ${wfState.steps.length + 1}`,
            status: 'pending',
          })
        }
        const curStep = wfState.steps[idx]
        if (curStep) {
          curStep.status = 'running'
          if (chunk.title) curStep.title = chunk.title
        }
      } else if (chunk.type === 'step_complete' && typeof chunk.step_index === 'number') {
        const idx = chunk.step_index
        const curStep = wfState.steps[idx]
        if (curStep) {
          curStep.status = 'completed'
          if (chunk.summary) curStep.summary = chunk.summary
        }
      } else if (chunk.type === 'delta' && chunk.text) {
        writer.append(chunk.text)
      } else if (chunk.type === 'workflow_complete') {
        wfState.status = 'completed'
        wfState.steps.forEach(s => {
          if (s.status === 'running' || s.status === 'pending') s.status = 'completed'
        })
      } else if (chunk.type === 'error') {
        wfState.status = 'error'
      }
    }
  } catch (err: any) {
    if (err?.name !== 'AbortError') {
      wfState.status = 'error'
    }
  } finally {
    writer.flush()
    isStreaming.value = false
    activeAbortController = null
  }
}
```

- [ ] **Step 3: 运行构建命令验证类型系统与 Store 改动**

Run: `npm --prefix frontend run build`
Expected: 编译通过无类型错误

- [ ] **Step 4: Commit**

```bash
git add frontend/src/types/index.ts frontend/src/api/ai.ts frontend/src/stores/chat.ts
git commit -m "feat(chat): ✨ add workflow staging and pinned preference to chat store"
```

---

### Task 2: 创建 `WorkflowSlashMenu.vue`（斜杠指令浮动菜单）

**Files:**
- Create: `frontend/src/components/WorkflowSlashMenu.vue`

- [ ] **Step 1: 实现 `WorkflowSlashMenu.vue` 模板与组件逻辑**

```vue
<template>
  <div v-if="visible" class="workflow-slash-menu" role="menu" aria-label="快捷工作流选择">
    <div class="menu-header">
      <Icon icon="ph:lightning-bold" class="header-icon" />
      <span>快捷工作流指令 (按 ↑↓ 导航，Enter 确认，Esc 退出)</span>
    </div>
    <div class="menu-list" ref="listRef">
      <div
        v-for="(wf, idx) in filteredWorkflows"
        :key="wf.id"
        class="menu-item"
        :class="{ active: selectedIndex === idx }"
        @click="selectItem(wf)"
        @mouseenter="selectedIndex = idx"
      >
        <div class="item-icon">
          <Icon :icon="wf.icon || 'ph:git-merge'" />
        </div>
        <div class="item-info">
          <div class="item-title">{{ wf.title }}</div>
          <div class="item-desc">{{ wf.description }}</div>
        </div>
        <span class="item-tag">{{ isDirect(wf.id) ? '快捷启动' : '参数配置' }}</span>
      </div>
      <div v-if="filteredWorkflows.length === 0" class="menu-empty">
        未找到匹配的工作流
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, nextTick } from 'vue'
import { Icon } from '@iconify/vue'
import { useChatStore } from '@/stores/chat'
import type { WorkflowDef } from '@/types'

const props = defineProps<{
  visible: boolean
  query: string
}>()

const emit = defineEmits<{
  (e: 'select', wf: WorkflowDef): void
  (e: 'close'): void
}>()

const chatStore = useChatStore()
const selectedIndex = ref(0)
const listRef = ref<HTMLElement | null>(null)

const DIRECT_RUN_IDS = new Set(['wf_portfolio_rebalance', 'wf_goal_tracker', 'wf_bill_calendar', 'wf_health_score'])
function isDirect(id: string) {
  return DIRECT_RUN_IDS.has(id)
}

const filteredWorkflows = computed(() => {
  const q = props.query.trim().toLowerCase()
  if (!q) return chatStore.workflows
  return chatStore.workflows.filter(w =>
    w.title.toLowerCase().includes(q) ||
    w.description.toLowerCase().includes(q) ||
    w.id.toLowerCase().includes(q)
  )
})

watch(() => props.query, () => {
  selectedIndex.value = 0
})

function selectItem(wf: WorkflowDef) {
  emit('select', wf)
}

function handleKeyDown(e: KeyboardEvent): boolean {
  if (!props.visible) return false
  if (e.key === 'ArrowDown') {
    e.preventDefault()
    selectedIndex.value = (selectedIndex.value + 1) % Math.max(1, filteredWorkflows.value.length)
    scrollToActive()
    return true
  } else if (e.key === 'ArrowUp') {
    e.preventDefault()
    selectedIndex.value = (selectedIndex.value - 1 + filteredWorkflows.value.length) % Math.max(1, filteredWorkflows.value.length)
    scrollToActive()
    return true
  } else if (e.key === 'Enter') {
    e.preventDefault()
    if (filteredWorkflows.value[selectedIndex.value]) {
      selectItem(filteredWorkflows.value[selectedIndex.value]!)
    }
    return true
  } else if (e.key === 'Escape') {
    e.preventDefault()
    emit('close')
    return true
  }
  return false
}

function scrollToActive() {
  nextTick(() => {
    const el = listRef.value?.querySelector('.menu-item.active') as HTMLElement | null
    if (el && listRef.value) {
      el.scrollIntoView({ block: 'nearest' })
    }
  })
}

defineExpose({ handleKeyDown })
</script>

<style scoped>
.workflow-slash-menu {
  position: absolute;
  bottom: calc(100% + 8px);
  left: 0;
  width: 380px;
  max-width: 90vw;
  background: rgba(15, 23, 42, 0.95);
  backdrop-filter: blur(12px);
  border: 1px solid rgba(0, 212, 255, 0.25);
  border-radius: 8px;
  box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.5), 0 0 15px rgba(0, 212, 255, 0.1);
  overflow: hidden;
  z-index: 100;
  animation: slideUp 0.15s ease-out;
}

@keyframes slideUp {
  from { opacity: 0; transform: translateY(6px); }
  to { opacity: 1; transform: translateY(0); }
}

.menu-header {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 8px 12px;
  font-size: 11px;
  color: var(--mf-primary, #00d4ff);
  background: rgba(0, 212, 255, 0.08);
  border-bottom: 1px solid rgba(0, 212, 255, 0.12);
  font-weight: 500;
}

.menu-list {
  max-height: 240px;
  overflow-y: auto;
  padding: 4px;
}

.menu-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 10px;
  border-radius: 6px;
  cursor: pointer;
  transition: all 0.12s ease;
}

.menu-item:hover, .menu-item.active {
  background: rgba(0, 212, 255, 0.15);
}

.item-icon {
  width: 26px;
  height: 26px;
  border-radius: 6px;
  background: rgba(0, 212, 255, 0.12);
  color: var(--mf-primary, #00d4ff);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 14px;
  flex-shrink: 0;
}

.item-info {
  flex: 1;
  min-width: 0;
}

.item-title {
  font-size: 12.5px;
  font-weight: 600;
  color: #f1f5f9;
}

.item-desc {
  font-size: 11px;
  color: #94a3b8;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.item-tag {
  font-size: 10px;
  padding: 1px 6px;
  border-radius: 8px;
  background: rgba(255, 255, 255, 0.06);
  color: #94a3b8;
  flex-shrink: 0;
}

.menu-empty {
  padding: 16px;
  text-align: center;
  font-size: 12px;
  color: #64748b;
}
</style>
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/components/WorkflowSlashMenu.vue
git commit -m "feat(chat): ✨ add WorkflowSlashMenu component for slash command auto-complete"
```

---

### Task 3: 创建 `WorkflowConfigCard.vue`（对话流内联参数配置卡片）

**Files:**
- Create: `frontend/src/components/WorkflowConfigCard.vue`

- [ ] **Step 1: 实现 `WorkflowConfigCard.vue` 参数配置交互与启动**

```vue
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
  messageId: number
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
  chatStore.cancelStagedWorkflow(props.messageId)
}

function submit() {
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
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/components/WorkflowConfigCard.vue
git commit -m "feat(chat): ✨ add WorkflowConfigCard component for inline parameter staging"
```

---

### Task 4: 重构 `WorkflowBar.vue`（36px 紧凑胶囊栏 + 全量抽屉面板）

**Files:**
- Modify: `frontend/src/components/WorkflowBar.vue`

- [ ] **Step 1: 重构 `WorkflowBar.vue` 模板与组件逻辑**

```vue
<template>
  <div class="workflow-bar-compact">
    <!-- Single-Row Pill Bar (36px) -->
    <div class="pill-bar-track">
      <div class="pill-tag">
        <Icon icon="ph:lightning-bold" class="tag-icon" />
        <span>快捷工作流</span>
      </div>

      <div class="pills-container">
        <button
          v-for="wf in pinnedWorkflows"
          :key="wf.id"
          class="wf-pill"
          :class="{ 'is-disabled': chatStore.isStreaming }"
          :title="wf.description"
          @click="triggerWorkflow(wf)"
        >
          <Icon :icon="wf.icon || 'ph:git-merge'" class="pill-icon" />
          <span class="pill-title">{{ wf.title }}</span>
        </button>
      </div>

      <button class="all-drawer-btn" @click="drawerVisible = true">
        <Icon icon="ph:squares-four" />
        <span>全部 ({{ chatStore.workflows.length }})</span>
        <Icon icon="ph:caret-right" />
      </button>
    </div>

    <!-- All Workflows Drawer -->
    <el-drawer
      v-model="drawerVisible"
      title="智能财务分析工作流库"
      size="440px"
      direction="rtl"
      append-to-body
      class="workflow-library-drawer"
    >
      <div class="drawer-inner">
        <!-- Search & Filter -->
        <div class="drawer-search-row">
          <el-input
            v-model="searchQuery"
            placeholder="搜索工作流..."
            clearable
            prefix-icon="Search"
            size="small"
          />
        </div>

        <!-- Workflow Grid -->
        <div class="drawer-cards-grid">
          <div
            v-for="wf in drawerFilteredWorkflows"
            :key="wf.id"
            class="library-card"
            :class="{ 'is-disabled': chatStore.isStreaming }"
            @click="triggerFromDrawer(wf)"
          >
            <div class="card-top">
              <div class="card-icon-box">
                <Icon :icon="wf.icon || 'ph:git-merge'" />
              </div>
              <div class="card-meta">
                <div class="card-title">{{ wf.title }}</div>
                <div class="card-desc">{{ wf.description }}</div>
              </div>
              <button
                class="pin-action-btn"
                :class="{ pinned: isPinned(wf.id) }"
                :title="isPinned(wf.id) ? '取消快捷置顶' : '固定到常用栏'"
                @click.stop="chatStore.togglePinWorkflow(wf.id)"
              >
                <Icon :icon="isPinned(wf.id) ? 'ph:star-fill' : 'ph:star'" />
              </button>
            </div>
            <div class="card-bottom">
              <span class="steps-tag">{{ wf.step_count || wf.steps?.length || 4 }} 步流水线</span>
              <span class="mode-tag">{{ isDirect(wf.id) ? '一键执行' : '参数配置' }}</span>
            </div>
          </div>
        </div>
      </div>
    </el-drawer>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { Icon } from '@iconify/vue'
import { useChatStore } from '@/stores/chat'
import type { WorkflowDef } from '@/types'

const chatStore = useChatStore()
const drawerVisible = ref(false)
const searchQuery = ref('')

const DIRECT_RUN_IDS = new Set(['wf_portfolio_rebalance', 'wf_goal_tracker', 'wf_bill_calendar', 'wf_health_score'])
function isDirect(id: string) {
  return DIRECT_RUN_IDS.has(id)
}

function isPinned(id: string) {
  return chatStore.pinnedWorkflowIds.includes(id)
}

const pinnedWorkflows = computed(() => {
  return chatStore.workflows.filter(w => chatStore.pinnedWorkflowIds.includes(w.id))
})

const drawerFilteredWorkflows = computed(() => {
  const q = searchQuery.value.trim().toLowerCase()
  if (!q) return chatStore.workflows
  return chatStore.workflows.filter(w =>
    w.title.toLowerCase().includes(q) ||
    w.description.toLowerCase().includes(q)
  )
})

function triggerWorkflow(wf: WorkflowDef) {
  if (chatStore.isStreaming) return
  if (isDirect(wf.id)) {
    chatStore.runWorkflow(wf.id)
  } else {
    chatStore.stageWorkflow(wf.id)
  }
}

function triggerFromDrawer(wf: WorkflowDef) {
  drawerVisible.value = false
  triggerWorkflow(wf)
}

onMounted(async () => {
  if (chatStore.workflows.length === 0) {
    await chatStore.fetchWorkflowsList()
  }
})
</script>

<style scoped>
.workflow-bar-compact {
  margin-bottom: 8px;
}

.pill-bar-track {
  display: flex;
  align-items: center;
  gap: 8px;
  background: rgba(15, 23, 42, 0.7);
  border: 1px solid rgba(0, 212, 255, 0.15);
  border-radius: var(--mf-radius-md, 8px);
  padding: 4px 8px;
  height: 36px;
  backdrop-filter: blur(8px);
}

.pill-tag {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 11.5px;
  font-weight: 600;
  color: var(--mf-primary, #00d4ff);
  flex-shrink: 0;
}

.tag-icon {
  font-size: 13px;
}

.pills-container {
  display: flex;
  align-items: center;
  gap: 6px;
  overflow-x: auto;
  scrollbar-width: none;
  flex: 1;
}

.pills-container::-webkit-scrollbar {
  display: none;
}

.wf-pill {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 3px 10px;
  background: rgba(2, 6, 23, 0.6);
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 12px;
  font-size: 11.5px;
  color: #cbd5e1;
  cursor: pointer;
  white-space: nowrap;
  transition: all 0.15s ease;
}

.wf-pill:hover:not(.is-disabled) {
  background: rgba(0, 212, 255, 0.12);
  border-color: rgba(0, 212, 255, 0.35);
  color: #ffffff;
  transform: translateY(-1px);
}

.wf-pill.is-disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.pill-icon {
  font-size: 12px;
  color: var(--mf-primary, #00d4ff);
}

.all-drawer-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 3px 8px;
  background: transparent;
  border: none;
  font-size: 11px;
  color: #94a3b8;
  cursor: pointer;
  flex-shrink: 0;
  transition: color 0.15s ease;
}

.all-drawer-btn:hover {
  color: var(--mf-primary, #00d4ff);
}

/* Drawer styles */
.drawer-inner {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.drawer-cards-grid {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.library-card {
  background: rgba(2, 6, 23, 0.7);
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 8px;
  padding: 10px 12px;
  cursor: pointer;
  transition: all 0.15s ease;
}

.library-card:hover:not(.is-disabled) {
  border-color: rgba(0, 212, 255, 0.35);
  background: rgba(0, 212, 255, 0.05);
}

.card-top {
  display: flex;
  align-items: flex-start;
  gap: 10px;
}

.card-icon-box {
  width: 28px;
  height: 28px;
  border-radius: 6px;
  background: rgba(0, 212, 255, 0.1);
  color: var(--mf-primary, #00d4ff);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 16px;
  flex-shrink: 0;
}

.card-meta {
  flex: 1;
  min-width: 0;
}

.card-title {
  font-size: 12.5px;
  font-weight: 600;
  color: #f1f5f9;
}

.card-desc {
  font-size: 11px;
  color: #94a3b8;
  margin-top: 2px;
  line-height: 1.4;
}

.pin-action-btn {
  background: transparent;
  border: none;
  color: #64748b;
  font-size: 15px;
  cursor: pointer;
  padding: 2px;
  transition: color 0.15s ease;
}

.pin-action-btn:hover, .pin-action-btn.pinned {
  color: #f59e0b;
}

.card-bottom {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-top: 8px;
  padding-top: 6px;
  border-top: 1px solid rgba(255, 255, 255, 0.05);
  font-size: 10.5px;
  color: #64748b;
}

.steps-tag {
  color: #94a3b8;
}

.mode-tag {
  color: var(--mf-primary, #00d4ff);
}
</style>
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/components/WorkflowBar.vue
git commit -m "refactor(chat): 🎨 compact 36px workflow pill bar with all-workflows drawer"
```

---

### Task 5: 更新 `ChatMessageContent.vue` 支持内联配置卡片渲染

**Files:**
- Modify: `frontend/src/components/ChatMessageContent.vue`

- [ ] **Step 1: 在 `ChatMessageContent.vue` 中集成 `WorkflowConfigCard`**

```vue
<template>
  <div class="message-content-body">
    <!-- Inline Workflow Config Card (if staged) -->
    <WorkflowConfigCard
      v-if="workflowConfig"
      :message-id="messageId"
      :config="workflowConfig"
    />

    <!-- Workflow Pipeline Progress Card (if present) -->
    <WorkflowProgressCard
      v-else-if="workflowData"
      :workflow-data="workflowData"
    />

    <!-- Markdown Rendered Content -->
    <div
      v-if="content"
      class="markdown-body"
      v-html="renderedHtml"
    ></div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import WorkflowProgressCard from '@/components/WorkflowProgressCard.vue'
import WorkflowConfigCard from '@/components/WorkflowConfigCard.vue'
import type { WorkflowRunState, WorkflowConfigState } from '@/types'

const props = defineProps<{
  messageId: number
  content: string
  workflowData?: WorkflowRunState
  workflowConfig?: WorkflowConfigState
}>()
// ... 保持原有 markdown 渲染逻辑 ...
</script>
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/components/ChatMessageContent.vue
git commit -m "feat(chat): ✨ support inline WorkflowConfigCard in ChatMessageContent"
```

---

### Task 6: 增强 `WorkflowProgressCard.vue`（耗时监控、业务行动按钮与智能追问）

**Files:**
- Modify: `frontend/src/components/WorkflowProgressCard.vue`

- [ ] **Step 1: 增强 `WorkflowProgressCard.vue` 业务行动条与追问气泡**

在模板底部和脚本中增加：
1. 真实累计耗时计数器（格式化为 `mm:ss`）；
2. 专属行动按钮（`wf_portfolio_rebalance` 提供【一键生成调仓单】、`wf_budget_guard` 提供【快捷调整预算】、通用【复制摘要】与【导出 Markdown 报告】）；
3. 动态智能追问气泡（点击直接调用 `chatStore.sendMessage`）。

- [ ] **Step 2: Commit**

```bash
git add frontend/src/components/WorkflowProgressCard.vue
git commit -m "feat(chat): ✨ enhance WorkflowProgressCard with action buttons and follow-up chips"
```

---

### Task 7: 在 `Chat.vue` 中集成斜杠指令监听与业务动作联动

**Files:**
- Modify: `frontend/src/views/Chat.vue`

- [ ] **Step 1: 集成 `WorkflowSlashMenu` 并在输入框按键中分发**

在 `Chat.vue` 中：
1. 引入 `WorkflowSlashMenu.vue`；
2. 监听 textarea 的 `@input` 和 `@keydown`，当检测到 `/` 且处于行首或空格后时打开 `WorkflowSlashMenu`；
3. 将键盘事件优先委托给 `WorkflowSlashMenu.handleKeyDown`；
4. 用户选中后，若无需参数直接 `runWorkflow`，需参数调用 `stageWorkflow`。

- [ ] **Step 2: 运行完整构建与单元验证**

Run: `npm --prefix frontend run build`
Expected: 0 错误，打包生成静态资产成功

- [ ] **Step 3: Commit**

```bash
git add frontend/src/views/Chat.vue
git commit -m "feat(chat): ✨ integrate slash command and compact workflow layout in Chat.vue"
```

---

### Task 8: 全链路端到端构建与功能验证

**Files:**
- Verification: `backend/tests/test_link.sh`
- Verification: `npm --prefix frontend run build`

- [ ] **Step 1: 运行前端构建与类型校验**

Run: `npm --prefix frontend run build`
Expected: PASS with 0 TypeScript/Vite errors.

- [ ] **Step 2: 运行后端集成测试**

Run: `cd backend && ./tests/test_link.sh`
Expected: All 33 test cases PASS.

- [ ] **Step 3: 最终汇总与文档记录**

Commit final changes if any.
