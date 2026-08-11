<template>
  <div class="login-container">
    <el-card class="login-card">
      <template #header>
        <div class="card-header">
          <h2>Minefolio</h2>
          <p class="subtitle">个人资产管理系统</p>
        </div>
      </template>

      <el-form ref="formRef" :model="form" :rules="rules" label-position="top">
        <el-form-item label="用户名" prop="username">
          <el-input v-model="form.username" placeholder="请输入用户名" prefix-icon="User" />
        </el-form-item>

        <el-form-item label="密码" prop="password">
          <el-input v-model="form.password" type="password" placeholder="请输入密码"
            prefix-icon="Lock" show-password @keyup.enter="handleSubmit" />
        </el-form-item>

        <el-form-item>
          <el-button type="primary" :loading="loading" class="submit-btn" @click="handleSubmit">
            {{ isRegister ? '注册' : '登录' }}
          </el-button>
        </el-form-item>
      </el-form>

      <div class="switch-mode">
        <span v-if="isRegister">{{ t('login.hasAccount') }} </span>
        <span v-else>{{ t('login.noAccount') }} </span>
        <el-button type="text" @click="toggleMode">
          {{ isRegister ? t('login.login') : t('login.register') }}
        </el-button>
      </div>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed } from 'vue'
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
const isRegister = ref(false)

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

function toggleMode() {
  isRegister.value = !isRegister.value
  form.username = ''
  form.password = ''
}

async function handleSubmit() {
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    loading.value = true
    try {
      if (isRegister.value) {
        await auth.register(form.username, form.password)
        ElMessage.success('注册成功')
      } else {
        await auth.login(form.username, form.password)
        ElMessage.success('登录成功')
      }
      router.push('/dashboard')
    } catch (e: any) {
      ElMessage.error(e?.response?.data?.message || '操作失败')
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
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
}
.login-card {
  width: 400px;
}
.card-header {
  text-align: center;
}
.card-header h2 {
  margin: 0;
  color: #303133;
}
.subtitle {
  margin: 8px 0 0;
  color: #909399;
  font-size: 14px;
}
.submit-btn {
  width: 100%;
}
.switch-mode {
  text-align: center;
  margin-top: 16px;
  color: #909399;
  font-size: 14px;
}
</style>
