<template>
  <el-drawer v-model="visible" direction="btt" size="82%" :with-header="false" class="mobile-sheet">
    <div class="sheet-head">
      <strong>{{ record ? '编辑资产' : '新增资产' }}</strong>
      <el-button text @click="visible = false">关闭</el-button>
    </div>
    <el-form label-position="top" class="sheet-form">
      <el-form-item label="名称">
        <el-input v-model="form.name" placeholder="资产名称" />
      </el-form-item>
      <el-form-item label="分类">
        <el-select v-model="form.category_id" filterable placeholder="选择分类" style="width:100%">
          <el-option v-for="c in categories" :key="c.id" :label="c.name" :value="Number(c.id)" />
        </el-select>
      </el-form-item>
      <el-form-item label="归属账户">
        <el-select v-model="form.account_id" filterable clearable placeholder="可选" style="width:100%">
          <el-option v-for="a in accounts" :key="a.id" :label="a.name" :value="Number(a.id)" />
        </el-select>
      </el-form-item>
      <el-form-item label="当前价值">
        <el-input-number v-model="form.current_value" :precision="2" :step="1" style="width:100%" />
      </el-form-item>
      <el-form-item label="数量/份额">
        <el-input-number v-model="form.quantity" :precision="4" :step="1" style="width:100%" />
      </el-form-item>
      <el-form-item label="成本">
        <el-input-number v-model="form.cost_basis" :precision="2" :step="1" style="width:100%" />
      </el-form-item>
      <el-form-item label="币种">
        <el-input v-model="form.currency" placeholder="CNY" />
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
import type { Asset, Account, Category } from '@/types'

const props = defineProps<{
  modelValue: boolean
  record?: Asset | null
  categories: Category[]
  accounts: Account[]
}>()
const emit = defineEmits<{ (e: 'update:modelValue', v: boolean): void; (e: 'saved'): void }>()

const visible = computed({
  get: () => props.modelValue,
  set: (v: boolean) => emit('update:modelValue', v),
})
const saving = ref(false)
const form = reactive({
  name: '', category_id: 0 as number | null, account_id: null as number | null,
  current_value: 0, quantity: 0, cost_basis: 0, net_value: 0,
  currency: 'CNY', note: '',
})

watch(() => props.modelValue, (open) => {
  if (!open) return
  const r = props.record
  form.name = r?.name ?? ''
  form.category_id = r ? Number(r.category_id) : null
  form.account_id = r?.account_id == null ? null : Number(r.account_id)
  form.current_value = Number(r?.current_value ?? 0)
  form.quantity = Number(r?.quantity ?? 0)
  form.cost_basis = Number(r?.cost_basis ?? 0)
  form.net_value = Number(r?.net_value ?? 0)
  form.currency = r?.currency ?? 'CNY'
  form.note = r?.note ?? ''
})

async function save() {
  if (!form.name.trim() || !form.category_id) {
    ElMessage.warning('请填写名称并选择分类')
    return
  }
  const payload: Record<string, unknown> = {
    name: form.name.trim(), category_id: form.category_id, currency: form.currency || 'CNY',
    current_value: form.current_value, quantity: form.quantity, cost_basis: form.cost_basis,
    net_value: form.net_value, note: form.note,
  }
  if (form.account_id != null) payload.account_id = form.account_id
  saving.value = true
  try {
    if (props.record) await offlineApi.put(`/assets/${props.record.id}`, payload)
    else await offlineApi.post('/assets', payload)
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
