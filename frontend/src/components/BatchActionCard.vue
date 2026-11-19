<template>
  <div class="batch-action-card" :class="cardStatus">
    <!-- Header -->
    <div class="card-header">
      <div class="header-badge">
        <Icon icon="ph:stack-bold" class="badge-icon" />
        <span class="badge-text">批量拟录入交易（{{ actions.length }} 条）</span>
      </div>
      <div class="status-indicator">
        <span v-if="cardStatus === 'pending'" class="status-tag pending">待确认</span>
        <span v-else-if="cardStatus === 'executing'" class="status-tag executing">入库中...</span>
        <span v-else-if="cardStatus === 'executed'" class="status-tag executed">
          <Icon icon="ph:check-circle-fill" /> 已入库
        </span>
        <span v-else-if="cardStatus === 'cancelled'" class="status-tag cancelled">已取消</span>
      </div>
    </div>

    <!-- Action List -->
    <div class="card-body">
      <div class="action-list">
        <div
          v-for="(action, index) in actions"
          :key="index"
          class="action-row"
        >
          <div class="action-index">{{ index + 1 }}</div>
          <div class="action-info">
            <el-tag
              :type="action.action_type === 'transfer' ? 'warning' : (action.data.type === 'income' ? 'success' : 'danger')"
              size="small"
              class="action-type-tag"
            >
              {{ action.action_type === 'transfer' ? '转账' : (action.data.type === 'income' ? '收入' : '支出') }}
            </el-tag>
            <span class="action-desc">{{ getActionDesc(action) }}</span>
          </div>
          <div class="action-amount" :class="action.action_type === 'daily_expense' && action.data.type === 'income' ? 'amount-income' : 'amount-expense'">
            {{ action.action_type === 'daily_expense' && action.data.type === 'income' ? '+' : '-' }}¥{{ Number(action.data.amount).toFixed(2) }}
          </div>
        </div>
      </div>

      <!-- Total Summary -->
      <div class="total-row">
        <span class="total-label">合计（净支出）</span>
        <span class="total-amount">¥{{ netTotal.toFixed(2) }}</span>
      </div>

      <div v-if="actions[0]?.data.date" class="date-hint">
        <Icon icon="ph:calendar-blank" class="hint-icon" />
        <span>{{ actions[0]?.data.date || '' }}</span>
      </div>
    </div>

    <!-- Footer Actions -->
    <div class="card-footer" v-if="cardStatus === 'pending'">
      <el-button size="small" @click="handleCancel">取消</el-button>
      <el-tooltip
        v-if="!draft_token"
        content="该批量卡片缺少服务端签发的确认令牌（须由 AI 通过 propose_batch_actions 工具发起），为防伪造已禁止写入"
      >
        <el-button type="info" size="small" disabled>
          <Icon icon="ph:shield-warning-bold" class="btn-icon" />
          <span>写入已禁用</span>
        </el-button>
      </el-tooltip>
      <el-button
        v-else
        type="primary"
        size="small"
        :loading="loading"
        @click="handleConfirm"
        class="confirm-btn"
      >
        <Icon icon="ph:lightning-fill" class="btn-icon" />
        <span>确认批量入账</span>
      </el-button>
    </div>

    <!-- Success Banner -->
    <div class="card-success-banner" v-if="cardStatus === 'executed'">
      <Icon icon="ph:check-circle" class="success-icon" />
      <span>已成功批量写入账单，关联资产账户余额已实时联动更新！</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { ElMessage } from 'element-plus'
import { Icon } from '@iconify/vue'
import { confirmBatchActions } from '@/api/ai'

export interface BatchProposedAction {
  action_type: 'daily_expense' | 'transfer'
  data: {
    type?: 'expense' | 'income'
    amount: number
    category_id?: number
    category_name?: string
    asset_id?: number
    asset_name?: string
    date?: string
    note?: string
    from_asset_id?: number
    from_asset_name?: string
    to_asset_id?: number
    to_asset_name?: string
    fee?: number
  }
}

const props = withDefaults(
  defineProps<{
    draft_token: string
    actions: BatchProposedAction[]
    status?: 'pending' | 'executing' | 'executed' | 'cancelled'
  }>(),
  {
    status: 'pending',
  }
)

const emit = defineEmits<{
  (e: 'executed'): void
  (e: 'cancelled'): void
}>()

const loading = ref(false)
const cardStatus = ref<'pending' | 'executing' | 'executed' | 'cancelled'>(props.status || 'pending')

watch(
  () => props.status,
  (newStatus) => {
    if (newStatus) {
      cardStatus.value = newStatus
    }
  }
)

function getActionDesc(action: BatchProposedAction): string {
  if (action.action_type === 'transfer') {
    const from = action.data.from_asset_name || `账户${action.data.from_asset_id ?? ''}`
    const to = action.data.to_asset_name || `账户${action.data.to_asset_id ?? ''}`
    return `${from} → ${to}`
  }
  const cat = action.data.category_name || ''
  const asset = action.data.asset_name || ''
  const note = action.data.note ? ` · ${action.data.note}` : ''
  return [cat, asset].filter(Boolean).join(' / ') + note
}

const netTotal = computed(() => {
  return props.actions.reduce((sum, action) => {
    const amount = Number(action.data.amount) || 0
    if (action.action_type === 'daily_expense' && action.data.type === 'income') {
      return sum - amount
    }
    return sum + amount
  }, 0)
})

async function handleConfirm() {
  if (!props.draft_token) {
    ElMessage.error('该批量卡片缺少服务端签发的确认令牌，已阻止写入')
    return
  }

  loading.value = true
  cardStatus.value = 'executing'

  try {
    const payload = {
      draft_token: props.draft_token,
      actions: props.actions.map(action => ({
        action_type: action.action_type,
        amount: Number(action.data.amount),
        type: action.data.type,
        date: action.data.date,
        category_id: action.data.category_id,
        asset_id: action.data.asset_id,
        from_asset_id: action.data.from_asset_id,
        to_asset_id: action.data.to_asset_id,
        note: action.data.note,
      })),
    }

    const res = (await confirmBatchActions(payload)) as unknown as {
      success?: boolean
      error?: string
      message?: string
    }

    if (res?.error) {
      if (
        res.error.includes('已被使用') ||
        res.error.toLowerCase().includes('replay') ||
        res.error.includes('consumed')
      ) {
        cardStatus.value = 'executed'
        ElMessage.info('该批量操作此前已记账入库')
        emit('executed')
        return
      }
      throw new Error(res.message || res.error)
    }

    cardStatus.value = 'executed'
    ElMessage.success(`批量记账成功！共入库 ${props.actions.length} 条`)
    emit('executed')
  } catch (err: unknown) {
    const e = err as { message?: string; response?: { data?: { message?: string } } }
    const errMsg = e?.message || e?.response?.data?.message || '批量操作失败，请重试'
    if (errMsg.includes('已被使用') || errMsg.toLowerCase().includes('replay')) {
      cardStatus.value = 'executed'
      ElMessage.info('该批量操作此前已记账入库')
      emit('executed')
    } else {
      cardStatus.value = 'pending'
      ElMessage.error(errMsg)
    }
  } finally {
    loading.value = false
  }
}

function handleCancel() {
  cardStatus.value = 'cancelled'
  emit('cancelled')
}
</script>

<style scoped>
.batch-action-card {
  margin: 14px 0;
  border-radius: 14px;
  border: 1px solid var(--mf-border);
  background: var(--mf-surface);
  backdrop-filter: blur(16px);
  box-shadow: var(--mf-shadow-md);
  overflow: hidden;
  transition: all 0.25s ease;
}

.batch-action-card.pending {
  border-color: var(--mf-border-hover);
  box-shadow: var(--mf-shadow-md), 0 0 16px rgba(0, 212, 255, 0.06);
}

.batch-action-card.executed {
  border-color: rgba(16, 185, 129, 0.4);
  background: rgba(16, 185, 129, 0.04);
}

.batch-action-card.cancelled {
  opacity: 0.7;
  border-color: var(--mf-border);
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 10px 16px;
  background: var(--mf-surface-muted);
  border-bottom: 1px solid var(--mf-border);
}

.header-badge {
  display: flex;
  align-items: center;
  gap: 8px;
  font-weight: 600;
  font-size: 13.5px;
  color: var(--mf-text-main);
}

.badge-icon {
  font-size: 17px;
  color: var(--mf-primary);
}

.status-tag {
  font-size: 11px;
  padding: 2px 10px;
  border-radius: 20px;
  font-weight: 600;
}

.status-tag.pending {
  background: var(--mf-primary-light);
  color: var(--mf-primary);
  border: 1px solid var(--mf-primary-border);
}

.status-tag.executing {
  background: var(--mf-warning-light);
  color: var(--mf-warning);
  border: 1px solid var(--mf-warning-border);
}

.status-tag.executed {
  display: flex;
  align-items: center;
  gap: 4px;
  background: var(--mf-success-light);
  color: var(--mf-success);
  border: 1px solid var(--mf-success-border);
}

.status-tag.cancelled {
  background: var(--mf-surface-muted);
  color: var(--mf-text-muted);
  border: 1px solid var(--mf-border);
}

.card-body {
  padding: 12px 16px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.action-list {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.action-row {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 7px 10px;
  border-radius: 8px;
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border);
}

.action-index {
  font-size: 11px;
  font-weight: 700;
  color: var(--mf-text-muted);
  min-width: 16px;
  text-align: center;
}

.action-info {
  display: flex;
  align-items: center;
  gap: 8px;
  flex: 1;
  min-width: 0;
}

.action-type-tag {
  flex-shrink: 0;
}

.action-desc {
  font-size: 12.5px;
  color: var(--mf-text-main);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.action-amount {
  font-size: 13px;
  font-weight: 600;
  flex-shrink: 0;
}

.amount-expense {
  color: #ef4444;
}

.amount-income {
  color: #10b981;
}

.total-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 8px 10px;
  border-radius: 8px;
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border-hover);
  font-weight: 600;
}

.total-label {
  font-size: 12.5px;
  color: var(--mf-text-muted);
}

.total-amount {
  font-size: 14px;
  color: var(--mf-text-main);
}

.date-hint {
  display: flex;
  align-items: center;
  gap: 5px;
  font-size: 11.5px;
  color: var(--mf-text-muted);
}

.hint-icon {
  font-size: 13px;
}

.card-footer {
  display: flex;
  justify-content: flex-end;
  align-items: center;
  gap: 10px;
  padding: 12px 16px;
  background: var(--mf-surface-muted);
  border-top: 1px solid var(--mf-border);
}

.confirm-btn {
  display: flex;
  align-items: center;
  gap: 6px;
  background: linear-gradient(135deg, var(--mf-primary), #6366f1) !important;
  border: none !important;
  font-weight: 600;
  box-shadow: 0 2px 8px rgba(99, 102, 241, 0.25);
}

.confirm-btn:hover {
  opacity: 0.92;
}

.btn-icon {
  font-size: 14px;
}

.card-success-banner {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 16px;
  background: rgba(16, 185, 129, 0.1);
  color: #10b981;
  font-size: 12.5px;
  font-weight: 500;
  border-top: 1px solid rgba(16, 185, 129, 0.2);
}

.success-icon {
  font-size: 18px;
  color: #10b981;
}
</style>
