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
          <h2 class="app-title">Minefolio 初始化</h2>
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
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    loading.value = true
    try {
      await auth.setup(form.username, form.password)
      ElMessage.success('初始化成功！已自动为您登录系统')
      router.push('/dashboard')
    } catch (e: any) {
      ElMessage.error(e?.response?.data?.message || '初始化失败')
    } finally {
      loading.value = false
    }
  })
}
</script>

<style scoped>
.setup-container {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, #0f172a 0%, #1e1b4b 50%, #312e81 100%);
  position: relative;
  overflow: hidden;
}
.setup-overlay {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background: radial-gradient(circle at top right, rgba(59, 130, 246, 0.15), transparent 40%),
              radial-gradient(circle at bottom left, rgba(99, 102, 241, 0.15), transparent 40%);
  pointer-events: none;
}
.setup-card {
  width: 440px;
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
.brand-badge {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 4px 12px;
  border-radius: 9999px;
  background: rgba(59, 130, 246, 0.15);
  border: 1px solid rgba(59, 130, 246, 0.3);
  color: #60a5fa;
  font-size: 12px;
  font-weight: 500;
  margin-bottom: 12px;
}
.pulse-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background-color: #3b82f6;
  box-shadow: 0 0 8px #3b82f6;
}
.app-title {
  margin: 0;
  font-size: 28px;
  font-weight: 700;
  background: linear-gradient(to right, #60a5fa, #a78bfa);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  letter-spacing: 1px;
}
.subtitle {
  margin: 8px 0 0;
  color: #94a3b8;
  font-size: 14px;
}
.setup-form {
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
  box-shadow: 0 0 0 1px #60a5fa inset, 0 0 10px rgba(96, 165, 250, 0.2) !important;
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
</style>
