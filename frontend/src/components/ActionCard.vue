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
                :label="`${asset.name} (¥${Number(asset.current_value).toFixed(2)})`"
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
                :label="`${asset.name} (¥${Number(asset.current_value).toFixed(2)})`"
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
                :label="`${asset.name} (¥${Number(asset.current_value).toFixed(2)})`"
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

          <div class="form-item">
            <label>手续费</label>
            <el-input-number
              v-model="form.fee"
              :min="0"
              :precision="2"
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
    </div>

    <!-- Footer Actions -->
    <div class="card-footer" v-if="cardStatus === 'pending'">
      <el-button size="small" @click="handleCancel">取消</el-button>
      <el-button
        type="primary"
        size="small"
        :loading="loading"
        @click="handleConfirm"
        class="confirm-btn"
      >
        <Icon icon="ph:lightning-fill" class="btn-icon" />
        <span>确认记账入库</span>
      </el-button>
    </div>

    <!-- Success Message -->
    <div class="card-success-banner" v-if="cardStatus === 'executed'">
      <Icon icon="ph:check-circle" class="success-icon" />
      <span>已成功写入账单，关联资产账户余额已实时联动更新！</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import { Icon } from '@iconify/vue'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { transactionsApi } from '@/api/transactions'
import { assetsApi } from '@/api/assets'
import { useCategoryStore } from '@/stores/category'
import type { Asset, Category } from '@/types'

export interface ProposedAction {
  action_type: 'daily_expense' | 'transfer'
  status?: 'proposed' | 'pending' | 'executed' | 'cancelled'
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

const props = defineProps<{
  actionData: ProposedAction
}>()

const emit = defineEmits<{
  (e: 'executed', action: ProposedAction): void
  (e: 'cancelled'): void
}>()

const categoryStore = useCategoryStore()
const availableAssets = ref<Asset[]>([])
const loading = ref(false)
const cardStatus = ref<'pending' | 'executing' | 'executed' | 'cancelled'>(
  props.actionData.status === 'executed' ? 'executed' : (props.actionData.status === 'cancelled' ? 'cancelled' : 'pending')
)

const form = reactive({
  type: props.actionData.data.type || 'expense',
  amount: Number(props.actionData.data.amount) || 0,
  category_id: Number(props.actionData.data.category_id) || undefined,
  asset_id: Number(props.actionData.data.asset_id) || undefined,
  from_asset_id: Number(props.actionData.data.from_asset_id) || undefined,
  to_asset_id: Number(props.actionData.data.to_asset_id) || undefined,
  date: props.actionData.data.date || new Date().toISOString().slice(0, 10),
  fee: Number(props.actionData.data.fee) || 0,
  note: props.actionData.data.note || '',
})

const isReadonly = computed(() => cardStatus.value !== 'pending')

const headerTitle = computed(() => {
  if (props.actionData.action_type === 'daily_expense') {
    return form.type === 'income' ? '拟记日常收入' : '拟记日常支出'
  }
  return '拟记账户转账'
})

const headerIcon = computed(() => {
  if (props.actionData.action_type === 'daily_expense') {
    return form.type === 'income' ? 'ph:arrow-fat-down-bold' : 'ph:shopping-bag-bold'
  }
  return 'ph:arrows-left-right-bold'
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
  if (form.amount <= 0) {
    ElMessage.warning('请输入有效的金额')
    return
  }

  loading.value = true
  cardStatus.value = 'executing'

  try {
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

      await dailyExpensesApi.create({
        expense_type: form.type,
        amount: form.amount,
        category_id: form.category_id,
        asset_id: form.asset_id,
        date: form.date,
        currency: 'CNY',
        note: form.note || '',
      })
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

      await transactionsApi.create({
        asset_id: form.from_asset_id,
        linked_asset_id: form.to_asset_id,
        category_id: 1, // Default transfer category
        transaction_type: 'transfer_out',
        amount: form.amount,
        fee: form.fee || 0,
        currency: 'CNY',
        transaction_date: form.date,
        note: form.note || '转账',
      })
    }

    categoryStore.invalidate()
    cardStatus.value = 'executed'
    ElMessage.success('记账成功！')
    emit('executed', props.actionData)
  } catch (err: any) {
    cardStatus.value = 'pending'
    ElMessage.error(err?.response?.data?.message || '操作失败，请重试')
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
  margin: 12px 0;
  border-radius: 12px;
  border: 1px solid var(--el-border-color-lighter);
  background: var(--el-bg-color-overlay);
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.05);
  overflow: hidden;
  transition: all 0.25s ease;
}

.action-card.pending {
  border-color: var(--el-color-primary-light-5);
}

.action-card.executed {
  border-color: var(--el-color-success-light-5);
  background: var(--el-color-success-light-9);
}

.action-card.cancelled {
  opacity: 0.7;
  border-color: var(--el-border-color-lighter);
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 10px 14px;
  background: var(--el-fill-color-light);
  border-bottom: 1px solid var(--el-border-color-extra-light);
}

.header-badge {
  display: flex;
  align-items: center;
  gap: 6px;
  font-weight: 600;
  font-size: 13px;
  color: var(--el-text-color-primary);
}

.badge-icon {
  font-size: 16px;
  color: var(--el-color-primary);
}

.status-tag {
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 10px;
  font-weight: 500;
}

.status-tag.pending {
  background: var(--el-color-primary-light-8);
  color: var(--el-color-primary);
}

.status-tag.executing {
  background: var(--el-color-warning-light-8);
  color: var(--el-color-warning);
}

.status-tag.executed {
  display: flex;
  align-items: center;
  gap: 4px;
  background: var(--el-color-success-light-8);
  color: var(--el-color-success);
}

.status-tag.cancelled {
  background: var(--el-fill-color-darker);
  color: var(--el-text-color-secondary);
}

.card-body {
  padding: 12px 14px;
}

.form-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 10px;
}

.form-item {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.form-item label {
  font-size: 11px;
  color: var(--el-text-color-secondary);
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
  gap: 8px;
  padding: 10px 14px;
  background: var(--el-fill-color-lighter);
  border-top: 1px solid var(--el-border-color-extra-light);
}

.confirm-btn {
  display: flex;
  align-items: center;
  gap: 4px;
}

.btn-icon {
  font-size: 14px;
}

.card-success-banner {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 8px 14px;
  background: var(--el-color-success-light-9);
  color: var(--el-color-success-dark-2);
  font-size: 12px;
  font-weight: 500;
  border-top: 1px solid var(--el-color-success-light-7);
}

.success-icon {
  font-size: 16px;
  color: var(--el-color-success);
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
