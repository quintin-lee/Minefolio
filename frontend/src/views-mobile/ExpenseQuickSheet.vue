<template>
  <el-drawer v-model="visible" direction="btt" size="75%" :with-header="false" class="quick-sheet">
    <div class="sheet-body">
      <div class="type-switch">
        <el-radio-group v-model="form.expense_type">
          <el-radio-button value="expense">支出</el-radio-button>
          <el-radio-button value="income">收入</el-radio-button>
        </el-radio-group>
      </div>

      <div class="amount-input">
        <span class="currency">¥</span>
        <input
          ref="amountRef"
          v-model="form.amount"
          type="text"
          inputmode="decimal"
          placeholder="0.00"
          class="amount-field"
        />
      </div>

      <el-form label-position="top">
        <el-form-item label="分类">
          <el-select v-model="form.category_id" filterable placeholder="选择分类" style="width:100%">
            <el-option v-for="c in categories" :key="c.id" :label="c.name" :value="Number(c.id)" />
          </el-select>
        </el-form-item>
        <el-form-item label="资产">
          <el-select v-model="form.asset_id" filterable placeholder="选择资产" style="width:100%">
            <el-option v-for="a in assets" :key="a.id" :label="a.name" :value="Number(a.id)" />
          </el-select>
        </el-form-item>
        <el-form-item label="日期">
          <el-date-picker v-model="form.expense_date" type="date" value-format="YYYY-MM-DD" style="width:100%" />
        </el-form-item>
        <el-form-item label="备注">
          <el-input v-model="form.note" placeholder="可选" />
        </el-form-item>
      </el-form>

      <el-button type="primary" size="large" :loading="saving" @click="save" block>保存</el-button>
    </div>
  </el-drawer>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted, nextTick } from 'vue'
import { ElMessage } from 'element-plus'
import { useCategoryStore } from '@/stores/category'
import { assetsApi } from '@/api/assets'
import { offlineApi } from '@/utils/offline-http'
import type { DailyExpense, Category, Asset } from '@/types'

const props = defineProps<{ modelValue?: boolean; record?: DailyExpense | null }>()
const emit = defineEmits<{ (e: 'saved'): void; (e: 'update:modelValue', v: boolean): void }>()

const visible = computed({
  get: () => props.modelValue ?? false,
  set: (v: boolean) => emit('update:modelValue', v),
})
const amountRef = ref<HTMLInputElement | null>(null)
const saving = ref(false)
const categories = ref<Category[]>([])
const assets = ref<Asset[]>([])
const categoryStore = useCategoryStore()

const form = reactive({
  expense_type: 'expense' as 'income' | 'expense',
  category_id: null as number | null,
  asset_id: null as number | null,
  amount: '' as string | number,
  expense_date: new Date().toISOString().slice(0, 10),
  note: '',
})

onMounted(async () => {
  await categoryStore.loadCategories()
  categories.value = categoryStore.incomeExpenseCategories
  const res = await assetsApi.list({ page_size: 500 })
  assets.value = res.list
  if (props.record) {
    form.expense_type = props.record.expense_type
    form.category_id = Number(props.record.category_id)
    form.asset_id = Number(props.record.asset_id)
    form.amount = props.record.amount
    form.expense_date = props.record.expense_date
    form.note = props.record.note ?? ''
  }
  await nextTick()
  amountRef.value?.focus()
})

async function save() {
  if (!form.amount || Number(form.amount) <= 0) return ElMessage.warning('请输入金额')
  if (!form.category_id || !form.asset_id) return ElMessage.warning('请选择分类和资产')
  saving.value = true
  const payload = {
    expense_type: form.expense_type,
    category_id: form.category_id,
    asset_id: form.asset_id,
    amount: Number(form.amount),
    currency: 'CNY',
    expense_date: form.expense_date,
    note: form.note,
  }
  try {
    if (props.record) await offlineApi.put(`/daily-expenses/${props.record.id}`, payload)
    else await offlineApi.post('/daily-expenses', payload)
    ElMessage.success(`已记录 ¥${form.amount}`)
    visible.value = false
    emit('saved')
  } finally {
    saving.value = false
  }
}
</script>

<style scoped>
.sheet-body { padding: 16px; }
.type-switch { display: flex; justify-content: center; margin-bottom: 16px; }
.amount-input { display: flex; align-items: center; gap: 8px; margin-bottom: 20px; }
.currency { font-size: 24px; color: var(--mf-text-muted); }
.amount-field { flex: 1; background: transparent; border: none; border-bottom: 2px solid var(--mf-border); color: var(--mf-text-main); font-size: 36px; font-family: 'JetBrains Mono', monospace; outline: none; text-align: right; }
.amount-field:focus { border-color: var(--mf-primary); }
</style>
