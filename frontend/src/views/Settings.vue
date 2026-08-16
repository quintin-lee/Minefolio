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
  </div>
</template>

<script setup lang="ts">
import { ref, reactive } from 'vue'
import { ElMessage } from 'element-plus'
import { useAuthStore } from '@/stores/auth'
import { transactionsApi } from '@/api/transactions'
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
