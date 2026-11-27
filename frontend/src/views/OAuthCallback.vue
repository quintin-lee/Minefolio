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
import { ref, onMounted, onBeforeUnmount } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { ElMessage } from 'element-plus'
import { authApi } from '@/api/auth'
import { useAuthStore } from '@/stores/auth'
import { isMobileMode } from '@/utils/http'
import { t } from '@/utils/locale'
import { consumeAndVerifyOAuthState } from '@/utils/oauth-state'
import { save2FaTempSession } from '@/utils/two-factor'

const router = useRouter()
const route = useRoute()
const auth = useAuthStore()
const statusMsg = ref(t('oauth.verifying'))
const timers: ReturnType<typeof setTimeout>[] = []

function isMobileApp(): boolean {
  return __IS_MOBILE__ || isMobileMode()
}

function scheduleRedirect(target: string) {
  const id = setTimeout(() => {
    router.replace(target)
  }, 3000)
  timers.push(id)
}

onBeforeUnmount(() => {
  while (timers.length) {
    const id = timers.pop()
    if (id !== undefined) clearTimeout(id)
  }
})

onMounted(async () => {
  const mobile = isMobileApp()
  const loginPath = mobile ? '/m/login' : '/login'
  const homePath = mobile ? '/m/dashboard' : '/dashboard'
  const code = (route.query.code as string) || ''
  const provider = (route.query.provider as string) || (route.params.provider as string) || 'github'
  const state = (route.query.state as string) || ''

  if (!code && !route.query.oauth_id) {
    statusMsg.value = t('oauth.missingCode')
    scheduleRedirect(loginPath)
    return
  }

  if (!consumeAndVerifyOAuthState(state)) {
    statusMsg.value = t('oauth.loginFailedWith', { msg: t('oauth.invalidState') })
    ElMessage.error(t('oauth.invalidState'))
    scheduleRedirect(loginPath)
    return
  }

  try {
    const res = await authApi.oauthCallback({
      provider,
      code,
      oauth_id: route.query.oauth_id as string,
      username: route.query.username as string,
    })

    if (res && res.require_2fa && res.temp_token) {
      save2FaTempSession(res.temp_token)
      ElMessage.info(t('login.twoFactorPrompt'))
      router.replace(loginPath)
      return
    }

    if (res && res.token) {
      auth.setToken(res.token)
      try {
        await auth.fetchUser()
      } catch {
        if (res.user) {
          auth.setUser({ created_at: '', ...res.user })
        }
      }
      await auth.reinitLocalDb()
      ElMessage.success(t('oauth.loginSuccess'))
      router.replace(homePath)
    } else {
      throw new Error(t('oauth.noToken'))
    }
  } catch (err: any) {
    const raw = err?.response?.data?.message || err?.message || ''
    statusMsg.value = raw
      ? t('oauth.loginFailedWith', { msg: raw })
      : t('oauth.loginFailedWith', { msg: t('oauth.invalidCredential') })
    ElMessage.error(raw || t('oauth.failed'))
    scheduleRedirect(loginPath)
  }
})
</script>

<style scoped>
.oauth-callback-container {
  display: flex;
  justify-content: center;
  align-items: center;
  min-height: 100vh;
  min-height: 100dvh;
  background: var(--mf-background, #0f172a);
}
.callback-card {
  text-align: center;
  background: var(--mf-surface-card);
  border: 1px solid var(--mf-border);
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
  color: var(--mf-text-main);
  font-size: 20px;
  margin-bottom: 12px;
}
.status-msg {
  color: var(--mf-text-muted);
  font-size: 14px;
}
</style>
