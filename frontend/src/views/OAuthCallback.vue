<template>
  <div class="oauth-callback-container">
    <div class="callback-card">
      <div class="spinner">⏳</div>
      <h2>第三方账号登录中...</h2>
      <p class="status-msg">{{ statusMsg }}</p>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { ElMessage } from 'element-plus'
import { authApi } from '@/api/auth'
import { useAuthStore } from '@/stores/auth'

const router = useRouter()
const route = useRoute()
const auth = useAuthStore()
const statusMsg = ref('正在验证授权凭据并同步用户信息...')

onMounted(async () => {
  const code = (route.query.code as string) || ''
  const provider = (route.query.provider as string) || (route.params.provider as string) || 'github'
  const state = (route.query.state as string) || ''

  if (!code && !route.query.oauth_id) {
    statusMsg.value = '未检测到有效的授权码，3秒后返回登录页...'
    setTimeout(() => router.replace('/login'), 3000)
    return
  }

  try {
    const res = await authApi.oauthCallback({
      provider,
      code,
      oauth_id: route.query.oauth_id as string,
      username: route.query.username as string,
    })

    if (res && res.token) {
      auth.setToken(res.token)
      if (res.user) {
        auth.setUser(res.user)
      }
      ElMessage.success('第三方登录成功！')
      router.replace('/dashboard')
    } else {
      throw new Error('未获取到有效的登录令牌')
    }
  } catch (err: any) {
    statusMsg.value = `登录失败: ${err.message || '凭据无效或已过期'}，3秒后返回登录页...`
    ElMessage.error(err.message || 'OAuth 登录失败')
    setTimeout(() => router.replace('/login'), 3000)
  }
})
</script>

<style scoped>
.oauth-callback-container {
  display: flex;
  justify-content: center;
  align-items: center;
  min-height: 100vh;
  background: var(--mf-background, #0f172a);
}
.callback-card {
  text-align: center;
  background: rgba(30, 41, 59, 0.8);
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 16px;
  padding: 40px;
  max-width: 400px;
  width: 90%;
  box-shadow: var(--mf-shadow-md);
}
.spinner {
  font-size: 40px;
  animation: spin 2s infinite linear;
  margin-bottom: 16px;
}
@keyframes spin {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}
h2 {
  color: #f8fafc;
  font-size: 20px;
  margin-bottom: 12px;
}
.status-msg {
  color: #94a3b8;
  font-size: 14px;
}
</style>
