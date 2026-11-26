<template>
  <el-drawer v-model="visible" direction="btt" size="78%" :with-header="false" class="mobile-sheet">
    <div class="sheet-head">
      <strong>{{ record ? '编辑账户' : '新增账户' }}</strong>
      <el-button text @click="visible = false">关闭</el-button>
    </div>
    <el-form label-position="top" class="sheet-form">
      <el-form-item label="名称">
        <el-input v-model="form.name" placeholder="账户名称" />
      </el-form-item>
      <el-form-item label="机构">
        <el-select v-model="form.institution" style="width:100%">
          <el-option v-for="i in institutions" :key="i.value" :label="i.label" :value="i.value" />
        </el-select>
      </el-form-item>
      <el-form-item label="账号">
        <el-input v-model="form.account_no" placeholder="可选" />
      </el-form-item>
      <el-form-item label="币种">
        <el-input v-model="form.currency" placeholder="CNY" />
      </el-form-item>
      <el-form-item label="图标">
        <el-input v-model="form.icon" placeholder="可选" />
      </el-form-item>
      <el-form-item label="排序">
        <el-input-number v-model="form.sort_order" :step="1" style="width:100%" />
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
import type { Account } from '@/types'

const props = defineProps<{ modelValue: boolean; record?: Account | null }>()
const emit = defineEmits<{ (e: 'update:modelValue', v: boolean): void; (e: 'saved'): void }>()

const visible = computed({
  get: () => props.modelValue,
  set: (v: boolean) => emit('update:modelValue', v),
})
const saving = ref(false)
// 与 AssetsMobile 的 institutionLabels 及后端枚举保持一致
const institutions = [
  { value: 'bank_card', label: '银行卡' },
  { value: 'platform', label: '平台账户' },
  { value: 'broker', label: '券商' },
  { value: 'fund_platform', label: '基金平台' },
  { value: 'cash_wallet', label: '现金钱包' },
  { value: 'other', label: '其他' },
]
const form = reactive({
  name: '', institution: 'other', account_no: '', currency: 'CNY',
  icon: '', note: '', sort_order: 0,
})

watch(() => props.modelValue, (open) => {
  if (!open) return
  const r = props.record
  form.name = r?.name ?? ''
  form.institution = r?.institution ?? 'other'
  form.account_no = r?.account_no ?? ''
  form.currency = r?.currency ?? 'CNY'
  form.icon = r?.icon ?? ''
  form.note = r?.note ?? ''
  form.sort_order = Number(r?.sort_order ?? 0)
})

async function save() {
  if (!form.name.trim()) {
    ElMessage.warning('请填写账户名称')
    return
  }
  const payload: Record<string, unknown> = {
    name: form.name.trim(), institution: form.institution, account_no: form.account_no,
    currency: form.currency || 'CNY', icon: form.icon, note: form.note, sort_order: form.sort_order,
  }
  saving.value = true
  try {
    if (props.record) await offlineApi.put(`/accounts/${props.record.id}`, payload)
    else await offlineApi.post('/accounts', payload)
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
