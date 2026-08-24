<template>
  <div class="settings-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>{{ t('settings.title') }}</h2>
      </div>
    </div>

    <el-row :gutter="24" class="info-cards">
      <el-col :span="24">
        <div class="panel-container">
          <div class="panel-header">
            <h3>{{ t('settings.userInfo') }}</h3>
          </div>
          <div class="info-row">
            <span class="info-label">{{ t('settings.username') }}</span>
            <span class="info-value">{{ auth.user?.username || '-' }}</span>
          </div>
          <div class="info-row">
            <span class="info-label">{{ t('settings.accountId') }}</span>
            <span class="info-value mono-text">{{ auth.user?.id || '-' }}</span>
          </div>
          <div class="info-row">
            <span class="info-label">{{ t('settings.registeredAt') }}</span>
            <span class="info-value">{{ formatDate(auth.user?.created_at || '') }}</span>
          </div>
        </div>
      </el-col>
    </el-row>

    <div class="panel-container" style="margin-top: 24px;">
      <div class="panel-header">
        <h3>{{ t('settings.changePassword') }}</h3>
      </div>
      <el-form :model="form" :rules="rules" ref="formRef" label-width="120px" class="premium-form" @keyup.enter="submit">
        <el-form-item :label="t('settings.oldPassword')" prop="old_password">
          <el-input v-model="form.old_password" type="password" show-password :placeholder="t('settings.oldPassword')" />
        </el-form-item>
        <el-form-item :label="t('settings.newPassword')" prop="new_password">
          <el-input v-model="form.new_password" type="password" show-password :placeholder="t('settings.newPassword')" />
        </el-form-item>
        <el-form-item :label="t('settings.confirmPassword')" prop="confirmPassword">
          <el-input v-model="form.confirmPassword" type="password" show-password :placeholder="t('settings.confirmPassword')" />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" class="action-btn" @click="submit" :loading="loading">
            {{ t('settings.savePassword') }}
          </el-button>
        </el-form-item>
      </el-form>
    </div>

    <div class="panel-container" style="margin-top: 24px;">
      <div class="panel-header">
        <h3>{{ t('settings.exportData') }}</h3>
      </div>
      <p class="export-hint">{{ t('settings.exportHint') }}</p>
      <el-button type="primary" class="action-btn" @click="handleExport" :loading="exporting">
        {{ t('settings.exportButton') }}
      </el-button>
    </div>

    <div class="panel-container" style="margin-top: 24px;">
      <div class="panel-header">
        <h3>{{ t('settings.aiTitle') }}</h3>
      </div>
      <p class="export-hint">{{ t('settings.aiDesc') }}</p>
      <el-form :model="aiForm" label-width="120px" class="premium-form">
        <el-form-item :label="t('settings.aiDefaultProvider')">
          <el-select v-model="aiForm.default_provider" style="width: 100%">
            <el-option
              v-for="p in aiProviders"
              :key="p.id"
              :label="p.name"
              :value="p.id"
            />
          </el-select>
        </el-form-item>
        <el-form-item :label="t('settings.aiDefaultModel')">
          <el-select v-model="aiForm.default_model" style="width: 100%">
            <el-option
              v-for="m in availableModels"
              :key="m"
              :label="m"
              :value="m"
            />
          </el-select>
        </el-form-item>
        <el-form-item :label="t('settings.aiContextSize')">
          <el-input-number v-model="aiForm.context_size" :min="5" :max="100" :step="5" />
        </el-form-item>
        <el-form-item :label="t('settings.aiSystemPrompt')">
          <el-input v-model="aiForm.system_prompt" type="textarea" :rows="4" :maxlength="500" show-word-limit />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" @click="saveAiSettings" :loading="aiSaving">
            {{ aiSaving ? t('settings.aiSaving') : t('settings.aiSave') }}
          </el-button>
        </el-form-item>
      </el-form>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, computed } from 'vue'
import { ElMessage } from 'element-plus'
import { useAuthStore } from '@/stores/auth'
import { transactionsApi } from '@/api/transactions'
import { getSettings, updateSettings } from '@/api/ai'
import type { AiSettings } from '@/api/ai'
import { zhCN } from '@/locales/zh-CN'
import { formatDate } from '@/utils/format'
import type { FormInstance, FormRules } from 'element-plus'

const t = (key: string) => {
  const keys = key.split('.')
  let obj: any = zhCN
  for (const k of keys) obj = obj?.[k]
  return obj || key
}

const auth = useAuthStore()
const loading = ref(false)
const exporting = ref(false)
const formRef = ref<FormInstance>()
const aiSaving = ref(false)
const aiForm = reactive({
  default_provider: '',
  default_model: '',
  context_size: 20,
  system_prompt: '',
})
const aiProviders = ref<{ id: string; name: string }[]>([])
const availableModels = ref<string[]>([])

const form = reactive({
  old_password: '',
  new_password: '',
  confirmPassword: '',
})

const validateConfirm = (rule: any, value: string, callback: any) => {
  if (value !== form.new_password) {
    callback(new Error(t('settings.passwordMismatch')))
  } else {
    callback()
  }
}

const rules: FormRules = {
  old_password: [{ required: true, message: t('settings.oldPassword'), trigger: 'blur' }],
  new_password: [
    { required: true, message: t('settings.newPassword'), trigger: 'blur' },
    { min: 6, message: t('settings.passwordMin'), trigger: 'blur' },
  ],
  confirmPassword: [
    { required: true, message: t('settings.confirmPassword'), trigger: 'blur' },
    { validator: validateConfirm, trigger: 'blur' },
  ],
}

async function loadAiSettings() {
  try {
    const settings = await getSettings() as unknown as AiSettings
    aiForm.default_provider = settings.default_provider
    aiForm.default_model = settings.default_model
    aiForm.context_size = settings.context_size
    aiForm.system_prompt = settings.system_prompt
    aiProviders.value = settings.providers ?? []
    availableModels.value = settings.providers
      ?.flatMap(p => (p.models ?? []).map(m => `${p.id}/${m}`))
      ?? []
  } catch {
    // Load failed silently
  }
}

async function saveAiSettings() {
  aiSaving.value = true
  try {
    await updateSettings(aiForm)
    ElMessage.success(t('settings.aiSaved'))
    await loadAiSettings()
  } catch {
    ElMessage.error(t('settings.aiSaveFailed') || '保存失败')
  } finally {
    aiSaving.value = false
  }
}

async function submit() {
  if (!formRef.value) return
  await formRef.value.validate(async (valid) => {
    if (!valid) return
    try {
      loading.value = true
      await auth.changePassword(form.old_password, form.new_password)
      ElMessage.success(t('settings.passwordSuccess'))
      form.old_password = ''
      form.new_password = ''
      form.confirmPassword = ''
    } catch {
      // error handled by http interceptor
    } finally {
      loading.value = false
    }
  })
}

async function handleExport() {
  exporting.value = true
  try {
    const blob = await transactionsApi.exportCsv()
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `minefolio_transactions_${new Date().toISOString().slice(0, 10)}.csv`
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
    URL.revokeObjectURL(url)
    ElMessage.success('导出成功')
  } catch {
    ElMessage.error('导出失败')
  } finally {
    exporting.value = false
  }
}

onMounted(() => {
  loadAiSettings()
})
</script>

<style scoped>
.settings-page {
  background-color: var(--mf-background);
  height: 100%;
  overflow: hidden;
}

.info-cards {
  margin-bottom: 24px;
}

.panel-container {
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
}

.panel-header {
  margin-bottom: 20px;
}

.panel-header h3 {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
  color: var(--mf-text-main);
}

.info-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 0;
  border-bottom: 1px solid var(--mf-border);
}

.info-row:last-child {
  border-bottom: none;
}

.info-label {
  color: var(--mf-text-muted);
  font-size: 14px;
}

.info-value {
  color: var(--mf-text-main);
  font-weight: 500;
  font-size: 14px;
}

.mono-text {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
}

.premium-form .el-form-item {
  margin-bottom: 24px;
}

.premium-form :deep(.el-input__wrapper) {
  background-color: rgba(15, 23, 42, 0.6) !important;
  box-shadow: 0 0 0 1px var(--mf-border) inset !important;
  border-radius: var(--mf-radius-md);
  padding: 6px 12px;
}

.action-btn {
  width: 160px;
}

.export-hint {
  font-size: 13px;
  color: var(--mf-text-muted);
  margin-bottom: 16px;
  line-height: 1.6;
}
</style>
