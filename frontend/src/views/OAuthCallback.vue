<template>
  <div class="oauth-callback-container">
    <div class="callback-card">
      <div class="spinner">⏳</div>
      <h2>{{ t('oauth.processing') }}</h2>
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
import { t } from '@/utils/locale'

const router = useRouter()
const route = useRoute()
const auth = useAuthStore()
const statusMsg = ref(t('oauth.verifying'))

onMounted(async () => {
  const code = (route.query.code as string) || ''
  const provider = (route.query.provider as string) || (route.params.provider as string) || 'github'
  const state = (route.query.state as string) || ''

  if (!code && !route.query.oauth_id) {
    statusMsg.value = t('oauth.missingCode')
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
      ElMessage.success(t('oauth.loginSuccess'))
      router.replace('/dashboard')
    } else {
      throw new Error(t('oauth.noToken'))
    }
  } catch (err: any) {
    const raw = err.message || ''
    statusMsg.value = raw
      ? t('oauth.loginFailedWith', { msg: raw })
      : t('oauth.loginFailedWith', { msg: t('oauth.invalidCredential') })
    ElMessage.error(raw || t('oauth.failed'))
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
