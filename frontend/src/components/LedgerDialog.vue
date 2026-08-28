<script setup lang="ts">
import { ref, reactive, watch } from 'vue'
import { ElMessage, type FormInstance, type FormRules } from 'element-plus'
import { Icon } from '@iconify/vue'
import { ledgerApi } from '@/api/ledgers'
import type { Ledger } from '@/types'

const props = defineProps<{
  visible: boolean
  ledger?: Ledger | null
}>()

const emit = defineEmits<{
  (e: 'update:visible', val: boolean): void
  (e: 'saved', id?: number): void
}>()

const formRef = ref<FormInstance>()
const submitting = ref(false)

const iconOptions = [
  'ph:wallet',
  'ph:house',
  'ph:briefcase',
  'ph:piggy-bank',
  'ph:chart-line-up',
  'ph:credit-card',
  'ph:currency-circle-dollar',
  'ph:baby',
  'ph:airplane-tilt',
  'ph:shopping-bag'
]

const colorOptions = [
  '#3b82f6',
  '#10b981',
  '#8b5cf6',
  '#f59e0b',
  '#ef4444',
  '#06b6d4',
  '#ec4899',
  '#64748b'
]

const form = reactive({
  name: '',
  description: '',
  currency: 'CNY',
  icon: 'ph:wallet',
  color: '#3b82f6'
})

const rules: FormRules = {
  name: [{ required: true, message: '请输入账本名称', trigger: 'blur' }]
}

watch(
  () => props.visible,
  (val) => {
    if (val) {
      if (props.ledger) {
        form.name = props.ledger.name
        form.description = props.ledger.description || ''
        form.currency = props.ledger.currency || 'CNY'
        form.icon = props.ledger.icon || 'ph:wallet'
        form.color = props.ledger.color || '#3b82f6'
      } else {
        form.name = ''
        form.description = ''
        form.currency = 'CNY'
        form.icon = 'ph:wallet'
        form.color = '#3b82f6'
      }
    }
  }
)

async function handleSubmit() {
  if (!formRef.value) return
  await formRef.value.validate()
  submitting.value = true
  try {
    if (props.ledger?.id) {
      await ledgerApi.update(props.ledger.id, form)
      ElMessage.success('账本更新成功')
      emit('saved', props.ledger.id)
    } else {
      const res = await ledgerApi.create(form)
      ElMessage.success('账本创建成功')
      emit('saved', res.id)
    }
    emit('update:visible', false)
  } finally {
    submitting.value = false
  }
}
</script>

<template>
  <el-dialog
    :model-value="visible"
    :title="ledger ? '编辑账本' : '创建新账本'"
    width="480px"
    destroy-on-close
    @update:model-value="emit('update:visible', $event)"
  >
    <el-form ref="formRef" :model="form" :rules="rules" label-width="80px">
      <el-form-item label="名称" prop="name">
        <el-input v-model="form.name" placeholder="例如：家庭公共账本、副业工作室" />
      </el-form-item>

      <el-form-item label="描述" prop="description">
        <el-input
          v-model="form.description"
          type="textarea"
          :rows="2"
          placeholder="说明此账本的用途与记账规则..."
        />
      </el-form-item>

      <el-form-item label="币种" prop="currency">
        <el-select v-model="form.currency" style="width: 100%">
          <el-option label="人民币 (CNY)" value="CNY" />
          <el-option label="美元 (USD)" value="USD" />
          <el-option label="港币 (HKD)" value="HKD" />
          <el-option label="欧元 (EUR)" value="EUR" />
          <el-option label="日元 (JPY)" value="JPY" />
          <el-option label="泰铢 (THB)" value="THB" />
        </el-select>
      </el-form-item>

      <el-form-item label="图标">
        <div class="icon-selector">
          <div
            v-for="ic in iconOptions"
            :key="ic"
            class="icon-item"
            :class="{ active: form.icon === ic }"
            @click="form.icon = ic"
          >
            <Icon :icon="ic" width="20" />
          </div>
        </div>
      </el-form-item>

      <el-form-item label="主题色">
        <div class="color-selector">
          <div
            v-for="c in colorOptions"
            :key="c"
            class="color-item"
            :style="{ backgroundColor: c }"
            :class="{ active: form.color === c }"
            @click="form.color = c"
          />
        </div>
      </el-form-item>
    </el-form>

    <template #footer>
      <el-button @click="emit('update:visible', false)">取消</el-button>
      <el-button type="primary" :loading="submitting" @click="handleSubmit">
        确定
      </el-button>
    </template>
  </el-dialog>
</template>

<style scoped>
.icon-selector,
.color-selector {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
}
.icon-item {
  width: 36px;
  height: 36px;
  border-radius: 8px;
  border: 1px solid var(--el-border-color);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all 0.2s;
}
.icon-item:hover {
  border-color: var(--el-color-primary);
  color: var(--el-color-primary);
}
.icon-item.active {
  border-color: var(--el-color-primary);
  background-color: var(--el-color-primary-light-9);
  color: var(--el-color-primary);
}
.color-item {
  width: 28px;
  height: 28px;
  border-radius: 50%;
  cursor: pointer;
  border: 2px solid transparent;
  transition: transform 0.2s;
}
.color-item:hover {
  transform: scale(1.15);
}
.color-item.active {
  border-color: #000;
  box-shadow: 0 0 0 2px #fff;
}
</style>
