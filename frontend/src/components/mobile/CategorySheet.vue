<template>
  <el-drawer v-model="visible" direction="btt" size="78%" :with-header="false" class="mobile-sheet">
    <div class="sheet-head">
      <strong>{{ record ? '编辑分类' : '新增分类' }}</strong>
      <el-button text @click="visible = false">关闭</el-button>
    </div>
    <el-form label-position="top" class="sheet-form">
      <el-form-item label="名称">
        <el-input v-model="form.name" placeholder="分类名称" />
      </el-form-item>
      <el-form-item label="类型">
        <el-select v-model="form.type" style="width:100%">
          <el-option v-for="t in types" :key="t.value" :label="t.label" :value="t.value" />
        </el-select>
      </el-form-item>
      <el-form-item v-if="form.type === 'asset'" label="资产细分类型">
        <el-input v-model="form.asset_type" placeholder="如 cash / stock / fund" />
      </el-form-item>
      <el-form-item label="父分类">
        <el-select v-model="form.parent_id" filterable clearable placeholder="顶级分类" style="width:100%">
          <el-option v-for="c in candidates" :key="c.id" :label="c.name" :value="Number(c.id)" />
        </el-select>
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
    </el-form>
    <el-button type="primary" size="large" :loading="saving" class="save-btn" @click="save">保存</el-button>
  </el-drawer>
</template>

<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { offlineApi } from '@/utils/offline-http'
import type { Category } from '@/types'

const props = defineProps<{
  modelValue: boolean
  record?: Category | null
  categories: Category[]
}>()
const emit = defineEmits<{ (e: 'update:modelValue', v: boolean): void; (e: 'saved'): void }>()

const visible = computed({
  get: () => props.modelValue,
  set: (v: boolean) => emit('update:modelValue', v),
})
const saving = ref(false)
const types = [
  { value: 'asset', label: '资产' },
  { value: 'expense', label: '支出' },
  { value: 'income', label: '收入' },
  { value: 'transaction', label: '交易' },
]
const candidates = computed(() => props.categories.filter((c) => c.id !== props.record?.id))
const form = reactive({
  name: '', type: 'expense', asset_type: '', parent_id: null as number | null,
  currency: 'CNY', icon: '', sort_order: 0,
})

watch(() => props.modelValue, (open) => {
  if (!open) return
  const r = props.record
  form.name = r?.name ?? ''
  form.type = r?.type ?? 'expense'
  form.asset_type = r?.asset_type ?? ''
  form.parent_id = r?.parent_id == null ? null : Number(r.parent_id)
  form.currency = r?.currency ?? 'CNY'
  form.icon = r?.icon ?? ''
  form.sort_order = Number(r?.sort_order ?? 0)
})

async function save() {
  if (!form.name.trim()) {
    ElMessage.warning('请填写分类名称')
    return
  }
  const payload: Record<string, unknown> = {
    name: form.name.trim(), type: form.type, currency: form.currency || 'CNY',
    sort_order: form.sort_order,
  }
  if (form.type === 'asset' && form.asset_type) payload.asset_type = form.asset_type
  if (form.parent_id != null) payload.parent_id = form.parent_id
  if (form.icon) payload.icon = form.icon
  saving.value = true
  try {
    if (props.record) await offlineApi.put(`/categories/${props.record.id}`, payload)
    else await offlineApi.post('/categories', payload)
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
