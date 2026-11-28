<template>
  <div class="action-card" :class="[cardStatus, actionData.action_type]">
    <!-- Header -->
    <div class="card-header">
      <div class="header-badge">
        <Icon :icon="headerIcon" class="badge-icon" />
        <span class="badge-text">{{ headerTitle }}</span>
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

    <!-- Form Content -->
    <div class="card-body">
      <!-- Daily Expense Form -->
      <template v-if="actionData.action_type === 'daily_expense'">
        <div class="form-grid">
          <div class="form-item amount-item">
            <label>金额</label>
            <el-input-number
              v-model="form.amount"
              :min="0.01"
              :step="10"
              :precision="2"
              :disabled="isReadonly"
              class="full-width"
              placeholder="金额"
            />
          </div>

          <div class="form-item">
            <label>类型</label>
            <el-select v-model="form.type" :disabled="isReadonly" class="full-width">
              <el-option label="支出" value="expense" />
              <el-option label="收入" value="income" />
            </el-select>
          </div>

          <div class="form-item">
            <label>分类</label>
            <el-select
              v-model="form.category_id"
              :disabled="isReadonly"
              filterable
              placeholder="选择分类"
              class="full-width"
            >
              <el-option
                v-for="cat in availableCategories"
                :key="cat.id"
                :label="cat.name"
                :value="cat.id"
              />
            </el-select>
          </div>

          <div class="form-item">
            <label>账户</label>
            <el-select
              v-model="form.asset_id"
              :disabled="isReadonly"
              filterable
              placeholder="选择账户"
              class="full-width"
            >
              <el-option
                v-for="asset in availableAssets"
                :key="asset.id"
                :label="`${assetLabel(asset)} (¥${Number(asset.current_value).toFixed(2)})`"
                :value="asset.id"
              />
            </el-select>
          </div>

          <div class="form-item">
            <label>日期</label>
            <el-date-picker
              v-model="form.date"
              type="date"
              value-format="YYYY-MM-DD"
              :disabled="isReadonly"
              class="full-width"
              placeholder="日期"
            />
          </div>

          <div class="form-item full-row">
            <label>备注</label>
            <el-input
              v-model="form.note"
              :disabled="isReadonly"
              placeholder="备注信息 (可选)"
              maxlength="100"
            />
          </div>
        </div>
      </template>

      <!-- Transfer Form -->
      <template v-else-if="actionData.action_type === 'transfer'">
        <div class="form-grid">
          <div class="form-item amount-item full-row">
            <label>转账金额</label>
            <el-input-number
              v-model="form.amount"
              :min="0.01"
              :step="100"
              :precision="2"
              :disabled="isReadonly"
              class="full-width"
            />
          </div>

          <div class="form-item">
            <label>转出账户</label>
            <el-select
              v-model="form.from_asset_id"
              :disabled="isReadonly"
              filterable
              placeholder="转出账户"
              class="full-width"
            >
              <el-option
                v-for="asset in availableAssets"
                :key="asset.id"
                :label="`${assetLabel(asset)} (¥${Number(asset.current_value).toFixed(2)})`"
                :value="asset.id"
              />
            </el-select>
          </div>

          <div class="form-item">
            <label>转入账户</label>
            <el-select
              v-model="form.to_asset_id"
              :disabled="isReadonly"
              filterable
              placeholder="转入账户"
              class="full-width"
            >
              <el-option
                v-for="asset in availableAssets"
                :key="asset.id"
                :label="`${assetLabel(asset)} (¥${Number(asset.current_value).toFixed(2)})`"
                :value="asset.id"
              />
            </el-select>
          </div>

          <div class="form-item">
            <label>转账日期</label>
            <el-date-picker
              v-model="form.date"
              type="date"
              value-format="YYYY-MM-DD"
              :disabled="isReadonly"
              class="full-width"
            />
          </div>

          <div class="form-item full-row">
            <label>转账备注</label>
            <el-input
              v-model="form.note"
              :disabled="isReadonly"
              placeholder="转账备注 (如还信用卡、理财等)"
              maxlength="100"
            />
          </div>
        </div>
      </template>

      <!-- Create Category Summary -->
      <template v-else-if="actionData.action_type === 'create_category'">
        <div class="form-grid">
          <div class="form-item">
            <label>分类名称</label>
            <div class="readonly-field">{{ actionData.data.name || '—' }}</div>
          </div>
          <div class="form-item">
            <label>分类类型</label>
            <div class="readonly-field">{{ categoryTypeLabel }}</div>
          </div>
          <div class="form-item" v-if="actionData.data.currency">
            <label>默认币种</label>
            <div class="readonly-field">{{ actionData.data.currency }}</div>
          </div>
          <div class="form-item" v-if="actionData.data.parent_id">
            <label>父分类 ID</label>
            <div class="readonly-field">#{{ actionData.data.parent_id }}</div>
          </div>
        </div>
      </template>

      <!-- Create Tag Summary -->
      <template v-else-if="actionData.action_type === 'create_tag'">
        <div class="form-grid">
          <div class="form-item">
            <label>标签名称</label>
            <div class="readonly-field">{{ actionData.data.name || '—' }}</div>
          </div>
          <div class="form-item">
            <label>标签颜色</label>
            <div class="readonly-field tag-color-row">
              <span class="color-dot" :style="{ backgroundColor: actionData.data.color || '#3b82f6' }" />
              <span>{{ actionData.data.color || '#3b82f6' }}</span>
            </div>
          </div>
        </div>
      </template>

      <!-- Savings Goal Summary -->
      <template v-else-if="actionData.action_type === 'savings_goal'">
        <div class="form-grid">
          <div class="form-item">
            <label>目标名称</label>
            <div class="readonly-field">{{ actionData.data.name || '—' }}</div>
          </div>
          <div class="form-item">
            <label>目标金额</label>
            <div class="readonly-field">¥{{ Number(actionData.data.target_amount || 0).toFixed(2) }}</div>
          </div>
          <div class="form-item">
            <label>当前已攒</label>
            <div class="readonly-field">¥{{ Number(actionData.data.current_amount || 0).toFixed(2) }}</div>
          </div>
          <div class="form-item">
            <label>截止日期</label>
            <div class="readonly-field">{{ actionData.data.deadline || '未指定' }}</div>
          </div>
          <div class="form-item full-row" v-if="actionData.data.note">
            <label>备注</label>
            <div class="readonly-field">{{ actionData.data.note }}</div>
          </div>
        </div>
      </template>

      <!-- Investment Transaction Summary -->
      <template v-else-if="actionData.action_type === 'transaction'">
        <div class="form-grid">
          <div class="form-item">
            <label>交易类型</label>
            <div class="readonly-field">{{ actionData.data.transaction_type || '—' }}</div>
          </div>
          <div class="form-item">
            <label>交易金额</label>
            <div class="readonly-field">¥{{ Number(actionData.data.amount || 0).toFixed(2) }}</div>
          </div>
          <div class="form-item">
            <label>标的资产</label>
            <div class="readonly-field">{{ actionData.data.asset_name || `#${actionData.data.asset_id || 0}` }}</div>
          </div>
          <div class="form-item" v-if="actionData.data.linked_asset_id">
            <label>关联资金账户</label>
            <div class="readonly-field">{{ actionData.data.linked_asset_name || `#${actionData.data.linked_asset_id}` }}</div>
          </div>
          <div class="form-item" v-if="Number(actionData.data.quantity || 0) > 0">
            <label>成交份额 / 单价</label>
            <div class="readonly-field">{{ actionData.data.quantity }} × ¥{{ Number(actionData.data.price || 0).toFixed(4) }}</div>
          </div>
          <div class="form-item">
            <label>交易日期</label>
            <div class="readonly-field">{{ actionData.data.date || '—' }}</div>
          </div>
          <div class="form-item full-row" v-if="actionData.data.note">
            <label>备注</label>
            <div class="readonly-field">{{ actionData.data.note }}</div>
          </div>
        </div>
      </template>
    </div>

    <!-- Footer Actions -->
    <div class="card-footer" v-if="cardStatus === 'pending'">
      <el-button size="small" @click="handleCancel">取消</el-button>
      <el-tooltip
        v-if="!draftToken"
        content="该卡片缺少服务端签发的确认令牌（须由 AI 通过 propose 工具发起），为防伪造已禁止写入"
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
        <span>{{ confirmBtnLabel }}</span>
      </el-button>
    </div>

    <!-- Success Message -->
    <div class="card-success-banner" v-if="cardStatus === 'executed'">
      <Icon icon="ph:check-circle" class="success-icon" />
      <span>{{ successBannerText }}</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { Icon } from '@iconify/vue'
import { assetsApi } from '@/api/assets'
import { confirmAction as confirmAiAction } from '@/api/ai'
import { useCategoryStore } from '@/stores/category'
import { useChatStore } from '@/stores/chat'
import { localToday, assetLabel } from '@/utils/format'
import type { ActionType } from '@/utils/normalize-action'
import type { Asset, Category } from '@/types'

export interface ProposedAction {
  action_type: ActionType
  status?: 'proposed' | 'pending' | 'executed' | 'cancelled'
  data: {
    type?: 'expense' | 'income' | 'asset' | 'transaction'
    amount?: number
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
    name?: string
    color?: string
    parent_id?: number
    asset_type?: string
    currency?: string
    icon?: string
    ledger_id?: number
    goal_id?: number
    target_amount?: number
    current_amount?: number
    deadline?: string
    transaction_type?: string
    linked_asset_id?: number
    linked_asset_name?: string
    quantity?: number
    price?: number
    draft_args_json?: string
    /** 服务端 propose 工具签发的 HMAC 令牌（300s TTL）。缺失 = 模型凭空捏造的卡片，不可落账 */
    draft_token?: string
  }
}

const props = defineProps<{
  actionData: ProposedAction
}>()

const emit = defineEmits<{
  (e: 'executed', action: ProposedAction): void
  (e: 'cancelled'): void
}>()

const categoryStore = useCategoryStore()
const chatStore = useChatStore()
const availableAssets = ref<Asset[]>([])
const loading = ref(false)
const cardStatus = ref<'pending' | 'executing' | 'executed' | 'cancelled'>(
  props.actionData.status === 'executed' ? 'executed' : (props.actionData.status === 'cancelled' ? 'cancelled' : 'pending')
)

watch(
  () => props.actionData.status,
  (newStatus) => {
    if (newStatus === 'executed') {
      cardStatus.value = 'executed'
    } else if (newStatus === 'cancelled') {
      cardStatus.value = 'cancelled'
    } else if (newStatus === 'pending') {
      cardStatus.value = 'pending'
    }
  }
)

const form = reactive({
  type: props.actionData.data.type || 'expense',
  amount: Number(props.actionData.data.amount) || 0,
  category_id: Number(props.actionData.data.category_id) || undefined,
  asset_id: Number(props.actionData.data.asset_id) || undefined,
  from_asset_id: Number(props.actionData.data.from_asset_id) || undefined,
  to_asset_id: Number(props.actionData.data.to_asset_id) || undefined,
  date: props.actionData.data.date || localToday(),
  fee: Number(props.actionData.data.fee) || 0,
  note: props.actionData.data.note || '',
})

/** 服务端签发的确认令牌：缺失 = 卡片并非由 propose 工具产生，落账按钮禁用 */
const draftToken = computed(() => props.actionData.data.draft_token || '')

const isReadonly = computed(() => cardStatus.value !== 'pending')

const headerTitle = computed(() => {
  switch (props.actionData.action_type) {
    case 'daily_expense':
      return form.type === 'income' ? '拟记日常收入' : '拟记日常支出'
    case 'transfer':
      return '拟记账户转账'
    case 'create_category':
      return '拟新建分类'
    case 'create_tag':
      return '拟新建标签'
    case 'savings_goal':
      return Number(props.actionData.data.goal_id || 0) > 0 ? '拟更新储蓄目标' : '拟新建储蓄目标'
    case 'transaction':
      return '拟录入资产交易'
    default:
      return '待确认操作'
  }
})

const headerIcon = computed(() => {
  switch (props.actionData.action_type) {
    case 'daily_expense':
      return form.type === 'income' ? 'ph:arrow-fat-down-bold' : 'ph:shopping-bag-bold'
    case 'transfer':
      return 'ph:arrows-left-right-bold'
    case 'create_category':
      return 'ph:folder-plus-bold'
    case 'create_tag':
      return 'ph:tag-bold'
    case 'savings_goal':
      return 'ph:target-bold'
    case 'transaction':
      return 'ph:chart-line-up-bold'
    default:
      return 'ph:check-circle-bold'
  }
})

const categoryTypeLabel = computed(() => {
  const t = String(props.actionData.data.type || 'expense')
  if (t === 'expense') return '支出'
  if (t === 'income') return '收入'
  if (t === 'asset') return '资产'
  if (t === 'transaction') return '交易'
  return t
})

const confirmBtnLabel = computed(() => {
  switch (props.actionData.action_type) {
    case 'daily_expense':
    case 'transfer':
      return '确认记账入库'
    case 'create_category':
      return '确认创建分类'
    case 'create_tag':
      return '确认创建标签'
    case 'savings_goal':
      return '确认保存目标'
    case 'transaction':
      return '确认提交交易'
    default:
      return '确认执行'
  }
})

const successBannerText = computed(() => {
  switch (props.actionData.action_type) {
    case 'create_category':
      return '分类已创建并生效！'
    case 'create_tag':
      return '标签已创建并生效！'
    case 'savings_goal':
      return '储蓄目标已保存！'
    case 'transaction':
      return '交易已入库，资产持仓与余额已同步更新！'
    default:
      return '已成功写入账单，关联资产账户余额已实时联动更新！'
  }
})

const availableCategories = computed<Category[]>(() => {
  if (form.type === 'expense') {
    return categoryStore.expenseCategories.length > 0 ? categoryStore.expenseCategories : categoryStore.allNodes
  }
  if (form.type === 'income') {
    return categoryStore.incomeCategories.length > 0 ? categoryStore.incomeCategories : categoryStore.allNodes
  }
  return categoryStore.allNodes
})

onMounted(async () => {
  try {
    if (!categoryStore.loaded && categoryStore.allNodes.length === 0) {
      await categoryStore.loadCategories()
    }
    const res = await assetsApi.list({ page_size: 100 })
    if (res && res.list) {
      availableAssets.value = res.list
    }

    // Auto-match default asset if none selected
    if (props.actionData.action_type === 'daily_expense' && !form.asset_id && availableAssets.value.length > 0) {
      form.asset_id = availableAssets.value[0]!.id
    }
    if (props.actionData.action_type === 'transfer') {
      if (!form.from_asset_id && availableAssets.value.length > 0) {
        form.from_asset_id = availableAssets.value[0]!.id
      }
      if (!form.to_asset_id && availableAssets.value.length > 1) {
        form.to_asset_id = availableAssets.value[1]!.id
      }
    }

    // Auto-match default category if none selected
    if (props.actionData.action_type === 'daily_expense' && !form.category_id && availableCategories.value.length > 0) {
      form.category_id = availableCategories.value[0]!.id
    }
  } catch {
    // ignore
  }
})

async function handleConfirm() {
  if (loading.value) return
  if (
    (props.actionData.action_type === 'daily_expense' ||
      props.actionData.action_type === 'transfer' ||
      props.actionData.action_type === 'transaction') &&
    form.amount <= 0
  ) {
    ElMessage.warning('请输入有效的金额')
    return
  }

  loading.value = true
  cardStatus.value = 'executing'

  try {
    if (!draftToken.value) {
      // 无 HMAC 令牌 = 卡片并非由 propose 工具产生（模型凭空捏造），拒绝落账
      cardStatus.value = 'pending'
      loading.value = false
      ElMessage.error('该卡片缺少服务端签发的确认令牌，已阻止写入。请让 AI 重新发起 propose')
      return
    }
    if (props.actionData.action_type === 'daily_expense') {
      if (!form.asset_id) {
        ElMessage.warning('请选择扣款或收款账户')
        cardStatus.value = 'pending'
        loading.value = false
        return
      }
      if (!form.category_id) {
        ElMessage.warning('请选择分类')
        cardStatus.value = 'pending'
        loading.value = false
        return
      }

      const res = (await confirmAiAction({
        action_type: 'daily_expense',
        draft_token: draftToken.value,
        session_id: chatStore.currentSessionId ?? undefined,
        amount: form.amount,
        type: form.type,
        date: form.date,
        category_id: form.category_id,
        asset_id: form.asset_id,
        note: form.note || '',
      })) as unknown as { success?: boolean; error?: string; message?: string }
      if (res?.error) {
        if (res.error.includes('已被使用') || res.error.toLowerCase().includes('replay') || res.error.includes('consumed')) {
          cardStatus.value = 'executed'
          ElMessage.info('该操作此前已记账入库')
          emit('executed', { ...props.actionData, status: 'executed' })
          return
        }
        throw new Error(res.message || res.error)
      }
    } else if (props.actionData.action_type === 'transfer') {
      if (!form.from_asset_id || !form.to_asset_id) {
        ElMessage.warning('请选择转出与转入账户')
        cardStatus.value = 'pending'
        loading.value = false
        return
      }
      if (form.from_asset_id === form.to_asset_id) {
        ElMessage.warning('转出与转入账户不能相同')
        cardStatus.value = 'pending'
        loading.value = false
        return
      }

      const res = (await confirmAiAction({
        action_type: 'transfer',
        draft_token: draftToken.value,
        session_id: chatStore.currentSessionId ?? undefined,
        amount: form.amount,
        from_asset_id: form.from_asset_id,
        to_asset_id: form.to_asset_id,
        date: form.date,
        note: form.note || '转账',
      })) as unknown as { success?: boolean; error?: string; message?: string }
      if (res?.error) {
        if (res.error.includes('已被使用') || res.error.toLowerCase().includes('replay') || res.error.includes('consumed')) {
          cardStatus.value = 'executed'
          ElMessage.info('该操作此前已转账入库')
          emit('executed', { ...props.actionData, status: 'executed' })
          return
        }
        throw new Error(res.message || res.error)
      }
    } else {
      // 通用提案确认（create_category / create_tag / savings_goal / transaction）：
      // 优先从 draft_args_json 还原规范化参数以保证 HMAC 哈希严格一致
      let canonicalArgs: Record<string, unknown> = {}
      if (props.actionData.data.draft_args_json) {
        try {
          canonicalArgs = JSON.parse(props.actionData.data.draft_args_json)
        } catch {
          canonicalArgs = {}
        }
      }
      if (Object.keys(canonicalArgs).length === 0) {
        const d = props.actionData.data
        if (props.actionData.action_type === 'create_category') {
          canonicalArgs = {
            name: d.name || '',
            type: d.type || 'expense',
            parent_id: Number(d.parent_id || 0),
            asset_type: d.asset_type || 'cash',
            currency: d.currency || 'CNY',
            icon: d.icon || '',
            ledger_id: Number(d.ledger_id || 0),
          }
        } else if (props.actionData.action_type === 'create_tag') {
          canonicalArgs = {
            name: d.name || '',
            color: d.color || '#3b82f6',
          }
        } else if (props.actionData.action_type === 'savings_goal') {
          canonicalArgs = {
            goal_id: Number(d.goal_id || 0),
            name: d.name || '',
            target_amount: Number(d.target_amount || 0),
            current_amount: Number(d.current_amount || 0),
            deadline: d.deadline || '',
            note: d.note || '',
          }
        } else if (props.actionData.action_type === 'transaction') {
          canonicalArgs = {
            transaction_type: d.transaction_type || '',
            amount: Number(d.amount || 0),
            asset_id: Number(d.asset_id || 0),
            linked_asset_id: Number(d.linked_asset_id || 0),
            quantity: Number(d.quantity || 0),
            price: Number(d.price || 0),
            fee: Number(d.fee || 0),
            date: d.date || '',
            note: d.note || '',
          }
        }
      }

      const res = (await confirmAiAction({
        ...canonicalArgs,
        action_type: props.actionData.action_type,
        draft_token: draftToken.value,
        session_id: chatStore.currentSessionId ?? undefined,
      })) as unknown as { success?: boolean; error?: string; message?: string }
      if (res?.error) {
        if (res.error.includes('已被使用') || res.error.toLowerCase().includes('replay') || res.error.includes('consumed')) {
          cardStatus.value = 'executed'
          ElMessage.info('该操作此前已执行入库')
          emit('executed', { ...props.actionData, status: 'executed' })
          return
        }
        throw new Error(res.message || res.error)
      }
    }

    categoryStore.invalidate()
    cardStatus.value = 'executed'
    ElMessage.success(successBannerText.value)
    emit('executed', { ...props.actionData, status: 'executed' })
  } catch (err: any) {
    const errMsg = err?.message || err?.response?.data?.message || '操作失败，请重试'
    if (errMsg.includes('已被使用') || errMsg.toLowerCase().includes('replay')) {
      cardStatus.value = 'executed'
      ElMessage.info('该操作此前已记账入库')
      emit('executed', { ...props.actionData, status: 'executed' })
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
.action-card {
  margin: 14px 0;
  border-radius: 14px;
  border: 1px solid var(--mf-border);
  background: var(--mf-surface);
  backdrop-filter: blur(16px);
  box-shadow: var(--mf-shadow-md);
  overflow: hidden;
  transition: all 0.25s ease;
}

.action-card.pending {
  border-color: var(--mf-border-hover);
  box-shadow: var(--mf-shadow-md), 0 0 16px rgba(0, 212, 255, 0.06);
}

.action-card.executed {
  border-color: rgba(16, 185, 129, 0.4);
  background: rgba(16, 185, 129, 0.04);
}

.action-card.cancelled {
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
  padding: 14px 16px;
}

.form-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 12px;
}

.form-item {
  display: flex;
  flex-direction: column;
  gap: 5px;
}

.form-item label {
  font-size: 11.5px;
  color: var(--mf-text-muted);
  font-weight: 500;
}

.form-item.full-row {
  grid-column: span 2;
}

.full-width {
  width: 100% !important;
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
  color: #ffffff !important;
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

.readonly-field {
  min-height: 32px;
  display: flex;
  align-items: center;
  padding: 4px 10px;
  border-radius: 8px;
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border);
  font-size: 13px;
  color: var(--mf-text-main);
}

.tag-color-row {
  gap: 8px;
}

.color-dot {
  width: 12px;
  height: 12px;
  border-radius: 50%;
  flex-shrink: 0;
}

@media (max-width: 640px) {
  .form-grid {
    grid-template-columns: 1fr;
  }
  .form-item.full-row {
    grid-column: span 1;
  }
}
</style>
