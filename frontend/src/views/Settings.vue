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
            <span class="info-value">{{ auth.user?.created_at || '-' }}</span>
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
  </div>
</template>

<script setup lang="ts">
import { ref, reactive } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { useAuthStore } from '@/stores/auth'
import { authApi } from '@/api/auth'
import { zhCN } from '@/locales/zh-CN'
import type { FormInstance, FormRules } from 'element-plus'

const t = (key: string) => {
  const keys = key.split('.')
  let obj: any = zhCN
  for (const k of keys) obj = obj?.[k]
  return obj || key
}

const router = useRouter()
const auth = useAuthStore()
const loading = ref(false)
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
      await authApi.changePassword({
        old_password: form.old_password,
        new_password: form.new_password,
      })
      ElMessage.success(t('settings.passwordSuccess'))
      auth.logout()
      router.push('/login')
    } catch {
      // error handled by http interceptor
    } finally {
      loading.value = false
    }
  })
}
</script>

<style scoped>
.settings-page {
  max-width: 720px;
  margin: 0 auto;
}
.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 24px;
}
.header-title {
  display: flex;
  align-items: center;
  gap: 12px;
}
.title-accent {
  width: 4px;
  height: 24px;
  background: linear-gradient(180deg, #3b82f6 0%, #2563eb 100%);
  border-radius: 4px;
}
.header-title h2 {
  margin: 0;
  font-size: 20px;
  font-weight: 600;
  color: #1e293b;
  letter-spacing: 0.5px;
}
.info-cards {
  margin-bottom: 24px;
}
.panel-container {
  background: #ffffff;
  border-radius: 16px;
  padding: 20px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.04);
}
.panel-header {
  margin-bottom: 20px;
}
.panel-header h3 {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
  color: #334155;
}
.info-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 0;
  border-bottom: 1px solid #f1f5f9;
}
.info-row:last-child {
  border-bottom: none;
}
.info-label {
  color: #64748b;
  font-size: 14px;
}
.info-value {
  color: #0f172a;
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
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.05);
  border-radius: 8px;
  padding: 6px 12px;
}
.action-btn {
  width: 160px;
}
</style>
