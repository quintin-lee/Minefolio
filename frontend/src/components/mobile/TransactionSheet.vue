<template>
  <el-drawer v-model="visible" direction="btt" size="88%" :with-header="false" class="mobile-sheet">
    <div class="sheet-head">
      <strong>{{ record ? '编辑交易' : '新增交易' }}</strong>
      <el-button text @click="visible = false">关闭</el-button>
    </div>
    <el-form label-position="top" class="sheet-form">
      <el-form-item label="资产">
        <el-select v-model="form.asset_id" filterable placeholder="选择资产" style="width:100%">
          <el-option v-for="a in assets" :key="a.id" :label="a.name" :value="Number(a.id)" />
        </el-select>
      </el-form-item>
      <el-form-item label="关联资产">
        <el-select v-model="form.linked_asset_id" filterable clearable placeholder="转账/买入出资时可选" style="width:100%">
          <el-option v-for="a in assets" :key="a.id" :label="a.name" :value="Number(a.id)" />
        </el-select>
      </el-form-item>
      <el-form-item label="交易类型">
        <el-select v-model="form.transaction_type" style="width:100%">
          <el-option v-for="t in typeOptions" :key="t.value" :label="t.label" :value="t.value" />
        </el-select>
      </el-form-item>
      <el-form-item label="金额">
        <el-input-number v-model="form.amount" :precision="2" :step="1" :min="0" style="width:100%" />
      </el-form-item>
      <el-form-item label="分类">
        <el-select v-model="form.category_id" filterable clearable placeholder="可选" style="width:100%">
          <el-option v-for="c in categories" :key="c.id" :label="c.name" :value="Number(c.id)" />
        </el-select>
      </el-form-item>
      <el-form-item label="单价">
        <el-input-number v-model="form.price_per_unit" :precision="4" :step="0.01" style="width:100%" />
      </el-form-item>
      <el-form-item label="数量">
        <el-input-number v-model="form.quantity" :precision="4" :step="1" style="width:100%" />
      </el-form-item>
      <el-form-item label="手续费">
        <el-input-number v-model="form.fee" :precision="2" :step="1" :min="0" style="width:100%" />
      </el-form-item>
      <el-form-item label="币种">
        <el-input v-model="form.currency" placeholder="可选" />
      </el-form-item>
      <el-form-item label="日期">
        <el-date-picker v-model="form.transaction_date" type="date" value-format="YYYY-MM-DD" :editable="false" style="width:100%" />
      </el-form-item>
      <el-form-item label="收支方向">
        <el-radio-group v-model="form.source_type">
          <el-radio-button value="">自动</el-radio-button>
          <el-radio-button value="income">收入</el-radio-button>
          <el-radio-button value="expense">支出</el-radio-button>
        </el-radio-group>
      </el-form-item>
      <el-form-item label="备注">
        <el-input v-model="form.note" placeholder="可选" />
      </el-form-item>
    </el-form>
    <el-button type="primary" size="large" :loading="saving" class="save-btn" @click="save">保存</el-button>
  </el-drawer>
</template>

<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { offlineApi } from '@/utils/offline-http'
import { localToday } from '@/utils/format'
import type { Transaction, Asset, Category, TransactionType } from '@/types'

const props = defineProps<{
  modelValue: boolean
  record?: Transaction | null
  assets: Asset[]
  categories: Category[]
}>()
const emit = defineEmits<{ (e: 'update:modelValue', v: boolean): void; (e: 'saved'): void }>()

const visible = computed({
  get: () => props.modelValue,
  set: (v: boolean) => emit('update:modelValue', v),
})
const saving = ref(false)
const typeOptions: { value: TransactionType; label: string }[] = [
  { value: 'deposit', label: '存入' },
  { value: 'withdrawal', label: '取出' },
  { value: 'buy', label: '买入' },
  { value: 'sell', label: '卖出' },
  { value: 'transfer_in', label: '转入' },
  { value: 'transfer_out', label: '转出' },
  { value: 'fee', label: '手续费' },
  { value: 'income', label: '收益' },
  { value: 'loss', label: '亏损' },
  { value: 'interest', label: '利息' },
]

function makeForm() {
  return {
    asset_id: null as number | null,
    linked_asset_id: null as number | null,
    category_id: null as number | null,
    transaction_type: 'deposit' as TransactionType,
    amount: 0,
    price_per_unit: 0,
    quantity: 0,
    fee: 0,
    currency: '',
    transaction_date: localToday(),
    source_type: '' as 'income' | 'expense' | '',
    note: '',
  }
}
const form = reactive(makeForm())

watch(() => props.modelValue, (open) => {
  if (!open) return
  Object.assign(form, makeForm())
  const r = props.record
  if (!r) return
  form.asset_id = Number(r.asset_id)
  form.linked_asset_id = r.linked_asset_id == null ? null : Number(r.linked_asset_id)
  form.category_id = r.category_id == null ? null : Number(r.category_id)
  form.transaction_type = r.transaction_type
  form.amount = Number(r.amount)
  form.price_per_unit = Number(r.price_per_unit ?? 0)
  form.quantity = Number(r.quantity ?? 0)
  form.fee = Number(r.fee ?? 0)
  form.currency = r.currency ?? ''
  form.transaction_date = r.transaction_date
  form.note = r.note ?? ''
})

async function save() {
  if (!form.asset_id) {
    ElMessage.warning('请选择资产')
    return
  }
  if (!form.amount || form.amount <= 0) {
    ElMessage.warning('请输入有效金额')
    return
  }
  if (!form.transaction_date) {
    ElMessage.warning('请选择交易日期')
    return
  }
  const payload: Record<string, unknown> = {
    asset_id: form.asset_id,
    transaction_type: form.transaction_type,
    amount: form.amount,
    transaction_date: form.transaction_date,
  }
  if (form.linked_asset_id != null) payload.linked_asset_id = form.linked_asset_id
  if (form.category_id != null) payload.category_id = form.category_id
  if (form.price_per_unit) payload.price_per_unit = form.price_per_unit
  if (form.quantity) payload.quantity = form.quantity
  if (form.fee) payload.fee = form.fee
  if (form.currency) payload.currency = form.currency
  if (form.source_type) payload.source_type = form.source_type
  if (form.note) payload.note = form.note
  // 注意：不写入 direction，本地列由服务端根据 transaction_type 回填
  saving.value = true
  try {
    if (props.record) await offlineApi.put(`/transactions/${props.record.id}`, payload)
    else await offlineApi.post('/transactions', payload)
    emit('saved')
    visible.value = false
  } catch (err: any) {
    ElMessage.error(String(err?.response?.data?.message || err?.message || '保存失败'))
  } finally {
    saving.value = false
  }
}
</script>

<style scoped>
.sheet-head { display: flex; align-items: center; justify-content: space-between; padding: 4px 0 8px; }
.sheet-form { padding-top: 8px; }
.save-btn { width: 100%; margin-top: 8px; }
</style>
