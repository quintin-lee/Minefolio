<template>
  <div class="login-mobile">
    <div class="brand-header">
      <AppLogo :size="44" :with-text="true" />
    </div>

    <!-- 服务端地址快捷设置入口 -->
    <div class="server-bar" role="button" tabindex="0" @click="showServerDialog = true">
      <el-icon class="server-bar-icon"><Connection /></el-icon>
      <div class="server-bar-info">
        <span class="server-bar-label">服务端：</span>
        <span class="server-bar-url">{{ displayUrl }}</span>
      </div>
      <el-tag size="small" :type="isCustom ? 'warning' : 'info'" class="server-bar-tag">
        {{ isCustom ? '自定义' : '默认' }}
      </el-tag>
      <el-icon class="server-bar-arrow"><ArrowRight /></el-icon>
    </div>

    <ServerConfigDialog v-model="showServerDialog" @saved="handleServerSaved" />

    <div v-if="isTwoFactorStep" class="two-factor-mobile-box">
      <p class="two-factor-hint">当前账号已开启两步验证，请输入身份验证器动态码或 8 位应急备用码：</p>
      <el-form label-position="top">
        <el-form-item label="动态验证码 / 备用码">
          <el-input
            v-model="twoFactorCode"
            placeholder="请输入 6 位动态验证码或备用码"
            size="large"
            autofocus
            @keyup.enter="handle2FaSubmit"
          />
        </el-form-item>
        <el-button type="primary" :loading="loading" class="submit-btn" @click="handle2FaSubmit">
          验证并登录
        </el-button>
        <div class="switch-mode-mobile">
          <el-button link type="info" @click="isTwoFactorStep = false">
            返回密码登录
          </el-button>
        </div>
      </el-form>
    </div>

    <el-form v-else :model="form" label-position="top">
      <el-form-item label="用户名">
        <el-input v-model="form.username" placeholder="请输入用户名" />
      </el-form-item>
      <el-form-item label="密码">
        <el-input
          v-model="form.password"
          type="password"
          show-password
          :placeholder="isRegister ? '请设置密码 (≥6位)' : '请输入密码'"
          @keyup.enter="submit"
        />
      </el-form-item>
      <el-form-item v-if="isRegister" label="确认密码">
        <el-input
          v-model="form.confirmPassword"
          type="password"
          show-password
          placeholder="请再次输入密码"
          @keyup.enter="submit"
        />
      </el-form-item>
      <el-button type="primary" :loading="loading" class="submit-btn" @click="submit">
        {{ isRegister ? '注册并登录' : '登录系统' }}
      </el-button>

      <div class="switch-mode-mobile">
        <span>{{ isRegister ? '已有账号？' : '还没有账号？' }}</span>
        <el-button link type="primary" @click="isRegister = !isRegister">
          {{ isRegister ? '返回登录' : '立即注册' }}
        </el-button>
      </div>

      <div v-if="oauthProviders.length > 0" class="oauth-mobile-section">
        <div class="oauth-divider-mobile"><span>第三方登录</span></div>
        <button
          v-for="p in oauthProviders"
          :key="p.id"
          type="button"
          class="oauth-btn-mobile"
          @click="handleOAuth(p)"
        >
          {{ p.name }} 登录
        </button>
      </div>
    </el-form>
  </div>
</template>

<script setup lang="ts">
import { reactive, ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { Connection, ArrowRight } from '@element-plus/icons-vue'
import { useAuthStore } from '@/stores/auth'
import { authApi } from '@/api/auth'
import AppLogo from '@/components/AppLogo.vue'
import ServerConfigDialog from '@/components/mobile/ServerConfigDialog.vue'
import { useServerUrl } from '@/composables/useServerUrl'
import type { OAuthProvider } from '@/types'

const router = useRouter()
const auth = useAuthStore()
const { displayUrl, isCustom } = useServerUrl()

const loading = ref(false)
const isRegister = ref(false)
const showServerDialog = ref(false)
const form = reactive({ username: '', password: '', confirmPassword: '' })
const isTwoFactorStep = ref(false)
const tempToken = ref('')
const twoFactorCode = ref('')
const oauthProviders = ref<OAuthProvider[]>([])

function handleOAuth(p: OAuthProvider) {
  if (p.auth_url) {
    window.location.href = p.auth_url
  }
}

async function loadOAuthProviders() {
  try {
    const res = await authApi.getOAuthProviders()
    if (res && res.providers) {
      oauthProviders.value = res.providers
    } else {
      oauthProviders.value = []
    }
  } catch {
    oauthProviders.value = []
  }
}

async function handleServerSaved() {
  await auth.checkSystemStatus()
  await loadOAuthProviders()
}

onMounted(async () => {
  await loadOAuthProviders()
})

async function submit() {
  if (!form.username || !form.password) return ElMessage.warning('请输入用户名和密码')
  if (isRegister.value) {
    if (form.password.length < 6) return ElMessage.warning('密码至少需6个字符')
    if (form.password !== form.confirmPassword) return ElMessage.warning('两次输入的密码不一致')
  }

  loading.value = true
  try {
    if (isRegister.value) {
      await auth.register(form.username, form.password)
      ElMessage.success('注册成功')
      router.replace('/m/dashboard')
    } else {
      const res = await auth.login(form.username, form.password)
      if (res?.require_2fa) {
        isTwoFactorStep.value = true
        tempToken.value = res.temp_token || ''
        twoFactorCode.value = ''
        ElMessage.info('请输入两步验证动态码或备用码')
        return
      }
      ElMessage.success('登录成功')
      router.replace('/m/dashboard')
    }
  } catch (e: any) {
    const isNetworkErr = !e?.response
    if (isNetworkErr) {
      ElMessage.error('无法连接到服务端，请点击上方“服务端”检查地址与网络')
    } else {
      ElMessage.error(e?.response?.data?.message || (isRegister.value ? '注册失败' : '登录失败'))
    }
  } finally {
    loading.value = false
  }
}

async function handle2FaSubmit() {
  if (!twoFactorCode.value.trim()) {
    ElMessage.warning('请输入 6 位动态验证码或备用码')
    return
  }
  loading.value = true
  try {
    await auth.verify2FaLogin(tempToken.value, twoFactorCode.value.trim())
    ElMessage.success('登录成功')
    router.replace('/m/dashboard')
  } catch (e: any) {
    ElMessage.error(e?.response?.data?.message || '动态验证码错误或备用码失效')
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.login-mobile { padding: 40px 24px; display: flex; flex-direction: column; gap: 20px; }
.brand-header {
  display: flex;
  justify-content: center;
  align-items: center;
  margin-bottom: 4px;
}

.server-bar {
  display: flex;
  align-items: center;
  gap: 8px;
  background: var(--mf-surface-card);
  border: 1px solid var(--mf-border);
  border-radius: 10px;
  padding: 10px 14px;
  cursor: pointer;
  transition: var(--mf-transition);
}
.server-bar:hover,
.server-bar:active {
  border-color: var(--mf-primary);
  background: var(--mf-surface-hover);
}
.server-bar-icon {
  font-size: 16px;
  color: var(--mf-primary);
  flex-shrink: 0;
}
.server-bar-info {
  flex: 1;
  display: flex;
  align-items: center;
  overflow: hidden;
  font-size: 13px;
}
.server-bar-label {
  color: var(--mf-text-muted);
  flex-shrink: 0;
}
.server-bar-url {
  color: var(--mf-text-main);
  font-weight: 500;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.server-bar-tag {
  flex-shrink: 0;
}
.server-bar-arrow {
  font-size: 14px;
  color: var(--mf-text-muted);
  flex-shrink: 0;
}

.switch-mode-mobile {
  text-align: center;
  margin-top: 18px;
  font-size: 14px;
  color: var(--mf-text-regular);
}
.oauth-mobile-section {
  margin-top: 24px;
}
.oauth-divider-mobile {
  text-align: center;
  font-size: 12px;
  color: var(--mf-text-muted);
  margin-bottom: 12px;
}
.oauth-btn-mobile {
  width: 100%;
  height: 40px;
  border-radius: 8px;
  border: 1px solid var(--mf-border);
  background: var(--mf-surface-card);
  color: var(--mf-text-main);
  font-size: 14px;
  font-weight: 500;
  cursor: pointer;
  margin-bottom: 8px;
}
.two-factor-mobile-box {
  margin-top: 12px;
}
.two-factor-hint {
  font-size: 13px;
  color: var(--mf-text-muted);
  line-height: 1.5;
  margin-bottom: 16px;
}
.submit-btn {
  width: 100%;
  margin-top: 12px;
  height: 42px;
  font-size: 15px;
  font-weight: 600;
  border-radius: 8px;
}
</style>
