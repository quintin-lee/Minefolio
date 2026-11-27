<template>
  <div class="setup-container">
    <div class="setup-overlay"></div>
    <el-card class="setup-card glass-panel fade-in">
      <template #header>
        <div class="card-header">
          <div class="brand-badge">
            <span class="pulse-dot"></span>
            首次部署
          </div>
          <div class="setup-brand-header">
            <AppLogo :size="40" />
            <h2 class="app-title">Minefolio 初始化</h2>
          </div>
          <p class="subtitle">欢迎使用个人资产管理系统，请设置管理员账号</p>
        </div>
      </template>

      <el-form ref="formRef" :model="form" :rules="rules" label-position="top" class="setup-form">
        <el-alert
          type="info"
          :closable="false"
          show-icon
          title="系统将为您自动注入预置的收支与交易分类模版"
          style="margin-bottom: 20px;"
        />

        <el-form-item label="管理员用户名" prop="username">
          <el-input v-model="form.username" placeholder="请输入管理员用户名 (例如: admin)" prefix-icon="User" size="large" />
        </el-form-item>

        <el-form-item label="设置密码" prop="password">
          <el-input v-model="form.password" type="password" placeholder="请输入密码 (至少 6 位)"
            prefix-icon="Lock" show-password size="large" />
        </el-form-item>

        <el-form-item label="确认密码" prop="confirmPassword">
          <el-input v-model="form.confirmPassword" type="password" placeholder="请再次输入密码"
            prefix-icon="Lock" show-password size="large" @keyup.enter="handleSubmit" />
        </el-form-item>

        <el-alert
          type="info"
          :closable="false"
          show-icon
          title="数据库连接由部署侧环境变量 MINEFOLIO_DB_DRIVER / MINEFOLIO_DB_DSN 提供，首次初始化无需在此配置"
          style="margin-bottom: 20px;"
        />

        <el-form-item class="submit-item">
          <el-button type="primary" size="large" :loading="loading" class="submit-btn" @click="handleSubmit">
            完成初始化并登录
          </el-button>
        </el-form-item>
      </el-form>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { useAuthStore } from '@/stores/auth'
import AppLogo from '@/components/AppLogo.vue'

const router = useRouter()
const auth = useAuthStore()
const formRef = ref()
const loading = ref(false)

const form = reactive({
  username: '',
  password: '',
  confirmPassword: '',
})

const validateConfirm = (_rule: any, value: string, callback: any) => {
  if (value !== form.password) {
    callback(new Error('两次输入的密码不一致'))
  } else {
    callback()
  }
}

const rules = {
  username: [
    { required: true, message: '请输入管理员用户名', trigger: 'blur' },
    { min: 2, message: '用户名至少需要 2 个字符', trigger: 'blur' },
  ],
  password: [
    { required: true, message: '请输入密码', trigger: 'blur' },
    { min: 6, message: '密码至少需要 6 个字符', trigger: 'blur' },
  ],
  confirmPassword: [
    { required: true, message: '请确认密码', trigger: 'blur' },
    { validator: validateConfirm, trigger: 'blur' },
  ],
}

async function handleSubmit() {
  if (loading.value) return
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid || loading.value) return
    loading.value = true
    try {
      // 数据库连接由部署侧环境变量 MINEFOLIO_DB_DRIVER / MINEFOLIO_DB_DSN 提供，
      // setup 不再下发 db 配置 (历史上写入 config/db.json 会与 env 优先级冲突)。
      await auth.setup(form.username, form.password)
      ElMessage.success('初始化成功！已自动为您登录系统')
      router.push('/dashboard')
    } catch (e: any) {
      ElMessage.error(e?.response?.data?.message || (e?.crypto_failed && e?.message ? e.message : null) || '初始化失败')
    } finally {
      loading.value = false
    }
  })
}
</script>

<style scoped>
.setup-container {
  min-height: 100vh;
  min-height: 100dvh;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 16px;
  box-sizing: border-box;
  background: linear-gradient(135deg, #060b18 0%, #0a1628 50%, #0d1f3c 100%);
  position: relative;
  overflow: hidden;
}
.setup-overlay {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background: radial-gradient(circle at top right, rgba(0, 212, 255, 0.1), transparent 40%),
              radial-gradient(circle at bottom left, rgba(124, 58, 237, 0.1), transparent 40%);
  pointer-events: none;
}
.setup-card {
  width: min(440px, calc(100vw - 32px));
  max-width: 100%;
  box-sizing: border-box;
  z-index: 1;
  padding: 10px;
}
.glass-panel {
  background: var(--mf-surface-card) !important;
  backdrop-filter: blur(20px);
  -webkit-backdrop-filter: blur(20px);
  border: 1px solid var(--mf-border) !important;
  box-shadow: var(--mf-shadow-lg) !important;
}
:deep(.el-card__header) {
  border-bottom: 1px solid var(--mf-border);
  padding-bottom: 20px;
}
.card-header {
  text-align: center;
}
.brand-badge {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 4px 12px;
  border-radius: 9999px;
  background: var(--mf-primary-light);
  border: 1px solid var(--mf-primary-border);
  color: var(--mf-primary);
  font-size: 12px;
  font-weight: 500;
  margin-bottom: 12px;
}
.pulse-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background-color: var(--mf-primary);
  box-shadow: var(--mf-shadow-glow);
}
.setup-brand-header {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
}
.app-title {
  margin: 0;
  font-size: 28px;
  font-weight: 700;
  background: linear-gradient(to right, var(--mf-primary), var(--mf-accent));
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  letter-spacing: 1px;
}
.subtitle {
  margin: 8px 0 0;
  color: var(--mf-text-muted);
  font-size: 14px;
}
.setup-form {
  margin-top: 20px;
}
:deep(.el-form-item__label) {
  color: var(--mf-text-main);
  font-weight: 500;
}
:deep(.el-input__wrapper) {
  background-color: var(--mf-surface-muted) !important;
  box-shadow: 0 0 0 1px var(--mf-primary-light) inset !important;
}
:deep(.el-input__wrapper.is-focus) {
  box-shadow: 0 0 0 1px var(--mf-primary) inset, var(--mf-shadow-glow) !important;
}
:deep(.el-input__inner) {
  color: var(--mf-text-main);
}
.submit-item {
  margin-top: 30px;
  margin-bottom: 0;
}
.submit-btn {
  width: 100%;
  height: 44px;
  font-size: 16px;
  border-radius: 8px;
  letter-spacing: 1px;
}

:deep(.el-divider__text) {
  color: #64748b;
  font-size: 13px;
  background: transparent !important;
}
:deep(.el-form-item__label) {
  color: var(--mf-text-regular);
  font-size: 13px;
}
</style>
