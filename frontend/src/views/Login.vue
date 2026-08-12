<template>
  <div class="login-container">
    <div class="login-overlay"></div>
    <el-card class="login-card glass-panel fade-in">
      <template #header>
        <div class="card-header">
          <h2 class="app-title">Minefolio</h2>
          <p class="subtitle">综合资产管理</p>
        </div>
      </template>

      <el-form ref="formRef" :model="form" :rules="rules" label-position="top" class="login-form">
        <el-form-item label="用户名" prop="username">
          <el-input v-model="form.username" placeholder="请输入用户名" prefix-icon="User" size="large" />
        </el-form-item>

        <el-form-item label="密码" prop="password">
          <el-input v-model="form.password" type="password" placeholder="请输入密码"
            prefix-icon="Lock" show-password size="large" @keyup.enter="handleSubmit" />
        </el-form-item>

        <el-form-item class="submit-item">
          <el-button type="primary" size="large" :loading="loading" class="submit-btn" @click="handleSubmit">
            登录系统
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
import { zhCN } from '@/locales/zh-CN'

const t = (key: string) => {
  const keys = key.split('.')
  let obj: any = zhCN
  for (const k of keys) obj = obj?.[k]
  return obj || key
}

const router = useRouter()
const auth = useAuthStore()
const formRef = ref()
const loading = ref(false)

const form = reactive({ username: '', password: '' })
const rules = {
  username: [
    { required: true, message: t('login.usernameRequired'), trigger: 'blur' },
    { min: 2, message: t('login.usernameMin'), trigger: 'blur' },
  ],
  password: [
    { required: true, message: t('login.passwordRequired'), trigger: 'blur' },
    { min: 4, message: t('login.passwordMin'), trigger: 'blur' },
  ],
}

async function handleSubmit() {
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    loading.value = true
    try {
      await auth.login(form.username, form.password)
      ElMessage.success('登录成功')
      router.push('/dashboard')
    } catch (e: any) {
      ElMessage.error(e?.response?.data?.message || '登录失败')
    } finally {
      loading.value = false
    }
  })
}
</script>

<style scoped>
.login-container {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, #060b18 0%, #0a1628 50%, #0d1f3c 100%);
  position: relative;
  overflow: hidden;
}
.login-overlay {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background: radial-gradient(circle at top right, rgba(0, 212, 255, 0.1), transparent 40%),
              radial-gradient(circle at bottom left, rgba(124, 58, 237, 0.1), transparent 40%);
  pointer-events: none;
}
.login-card {
  width: 420px;
  z-index: 1;
  padding: 10px;
}
.glass-panel {
  background: rgba(255, 255, 255, 0.05) !important;
  backdrop-filter: blur(20px);
  -webkit-backdrop-filter: blur(20px);
  border: 1px solid rgba(255, 255, 255, 0.1) !important;
  box-shadow: 0 25px 50px -12px rgba(0, 0, 0, 0.5) !important;
}
:deep(.el-card__header) {
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
  padding-bottom: 20px;
}
.card-header {
  text-align: center;
}
.app-title {
  margin: 0;
  font-size: 32px;
  font-weight: 700;
  background: linear-gradient(to right, #00d4ff, #a78bfa);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  letter-spacing: 2px;
}
.subtitle {
  margin: 8px 0 0;
  color: #64748b;
  font-size: 15px;
  letter-spacing: 3px;
}
.login-form {
  margin-top: 20px;
}
:deep(.el-form-item__label) {
  color: #e2e8f0;
  font-weight: 500;
}
:deep(.el-input__wrapper) {
  background-color: rgba(15, 23, 42, 0.4) !important;
  box-shadow: 0 0 0 1px rgba(255, 255, 255, 0.1) inset !important;
}
:deep(.el-input__wrapper.is-focus) {
  box-shadow: 0 0 0 1px #00d4ff inset, 0 0 10px rgba(0, 212, 255, 0.25) !important;
}
:deep(.el-input__inner) {
  color: #f8fafc;
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
.switch-mode {
  text-align: center;
  margin-top: 24px;
  color: #94a3b8;
  font-size: 14px;
}
.switch-btn {
  font-size: 14px;
  font-weight: 600;
  color: #00d4ff;
}
.switch-btn:hover {
  color: #22d3ee;
}
</style>
