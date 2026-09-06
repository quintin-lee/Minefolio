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
    width="500px"
    destroy-on-close
    append-to-body
    class="premium-dialog"
    @update:model-value="emit('update:visible', $event)"
  >
    <template #header>
      <div class="modal-header">
        <div
          class="modal-header-icon"
          :style="{
            backgroundColor: form.color ? form.color + '20' : 'var(--mf-primary-light)',
            borderColor: form.color ? form.color + '60' : 'var(--mf-primary-border)',
            color: form.color || 'var(--mf-primary)'
          }"
        >
          <Icon :icon="form.icon || 'ph:wallet-bold'" width="22" />
        </div>
        <div class="modal-header-text">
          <div class="modal-title">{{ ledger ? '编辑账本' : '创建新账本' }}</div>
          <div class="modal-subtitle">配置账本基本信息、基准币种与主题外观</div>
        </div>
      </div>
    </template>

    <el-form ref="formRef" :model="form" :rules="rules" label-width="70px" class="ledger-form">
      <el-form-item label="名称" prop="name">
        <el-input
          v-model="form.name"
          placeholder="例如：家庭公共账本、副业工作室、旅游基金"
          clearable
        />
      </el-form-item>

      <el-form-item label="描述" prop="description">
        <el-input
          v-model="form.description"
          type="textarea"
          :rows="2"
          placeholder="说明此账本的用途与核算规则..."
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
      <div class="dialog-footer">
        <el-button @click="emit('update:visible', false)">取消</el-button>
        <el-button
          type="primary"
          class="submit-btn"
          :loading="submitting"
          @click="handleSubmit"
        >
          <template #icon>
            <Icon icon="ph:check-bold" width="15" />
          </template>
          保存账本
        </el-button>
      </div>
    </template>
  </el-dialog>
</template>

<style scoped>
.modal-header {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 4px 0;
}
.modal-header-icon {
  width: 44px;
  height: 44px;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  border: 1px solid;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.2);
  flex-shrink: 0;
  transition: all 0.3s ease;
}
.modal-header-text {
  display: flex;
  flex-direction: column;
  gap: 3px;
}
.modal-title {
  font-size: 18px;
  font-weight: 700;
  color: var(--mf-text-main);
}
.modal-subtitle {
  font-size: 12px;
  color: var(--mf-text-regular);
}

.ledger-form {
  padding-top: 6px;
}

.icon-selector,
.color-selector {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
}
.icon-item {
  width: 38px;
  height: 38px;
  border-radius: 10px;
  border: 1px solid var(--mf-border);
  background: var(--mf-surface);
  color: var(--mf-text-regular);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
}
.icon-item:hover {
  border-color: var(--mf-primary);
  color: var(--mf-primary);
  transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(0, 212, 255, 0.15);
}
.icon-item.active {
  border-color: var(--mf-primary);
  background: rgba(0, 212, 255, 0.12);
  color: var(--mf-primary);
  box-shadow: 0 0 12px rgba(0, 212, 255, 0.3);
}
.color-item {
  width: 30px;
  height: 30px;
  border-radius: 50%;
  cursor: pointer;
  border: 2px solid transparent;
  transition: transform 0.2s cubic-bezier(0.4, 0, 0.2, 1);
  box-shadow: 0 2px 6px rgba(0, 0, 0, 0.3);
}
.color-item:hover {
  transform: scale(1.2);
}
.color-item.active {
  border-color: #fff;
  box-shadow: 0 0 0 2px var(--mf-primary), 0 0 12px rgba(0, 212, 255, 0.4);
  transform: scale(1.15);
}
.dialog-footer {
  display: flex;
  justify-content: flex-end;
  gap: 10px;
}
.submit-btn {
  border-radius: 8px;
  font-weight: 600;
  padding: 8px 20px;
}
</style>
