<template>
  <el-drawer v-model="visible" direction="btt" size="55%" :with-header="false" class="mobile-sheet">
    <div class="sheet-head">
      <strong>{{ record ? '编辑标签' : '新增标签' }}</strong>
      <el-button text @click="visible = false">关闭</el-button>
    </div>
    <el-form label-position="top" class="sheet-form">
      <el-form-item label="名称">
        <el-input v-model="form.name" placeholder="标签名称" />
      </el-form-item>
      <el-form-item label="颜色">
        <el-input v-model="form.color" placeholder="#ff5722" />
      </el-form-item>
    </el-form>
    <el-button type="primary" size="large" :loading="saving" class="save-btn" @click="save">保存</el-button>
  </el-drawer>
</template>

<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { offlineApi } from '@/utils/offline-http'
import type { Tag } from '@/types'

const props = defineProps<{ modelValue: boolean; record?: Tag | null }>()
const emit = defineEmits<{ (e: 'update:modelValue', v: boolean): void; (e: 'saved'): void }>()

const visible = computed({
  get: () => props.modelValue,
  set: (v: boolean) => emit('update:modelValue', v),
})
const saving = ref(false)
const form = reactive({ name: '', color: '' })

watch(() => props.modelValue, (open) => {
  if (!open) return
  form.name = props.record?.name ?? ''
  form.color = props.record?.color ?? ''
})

async function save() {
  if (!form.name.trim()) {
    ElMessage.warning('请填写标签名称')
    return
  }
  const payload: Record<string, unknown> = { name: form.name.trim() }
  if (form.color) payload.color = form.color
  saving.value = true
  try {
    if (props.record) await offlineApi.put(`/tags/${props.record.id}`, payload)
    else await offlineApi.post('/tags', payload)
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
