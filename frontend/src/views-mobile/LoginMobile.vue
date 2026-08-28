<template>
  <div class="login-mobile">
    <h1 class="brand">Minefolio</h1>
    <el-form :model="form" label-position="top">
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
      <el-button type="primary" :loading="loading" @click="submit" block style="margin-top: 12px">
        {{ isRegister ? '注册并登录' : '登录系统' }}
      </el-button>

      <div class="switch-mode-mobile">
        <span>{{ isRegister ? '已有账号？' : '还没有账号？' }}</span>
        <el-button link type="primary" @click="isRegister = !isRegister">
          {{ isRegister ? '返回登录' : '立即注册' }}
        </el-button>
      </div>
    </el-form>
  </div>
</template>

<script setup lang="ts">
import { reactive, ref } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { useAuthStore } from '@/stores/auth'
import { buildApiUrl } from '@/utils/http'

const router = useRouter()
const auth = useAuthStore()
const loading = ref(false)
const isRegister = ref(false)
const form = reactive({ username: '', password: '', confirmPassword: '' })

// 移动端内联 RSA 加密：使用 buildApiUrl 统一获取公钥
async function encryptPassword(pw: string): Promise<string> {
  const r = await fetch(buildApiUrl('/auth/public-key'))
  if (!r.ok) throw new Error('Failed to fetch public key')
  const jwk = (await r.json()).data.public_key
  const key = await crypto.subtle.importKey('jwk', jwk, { name: 'RSA-OAEP', hash: 'SHA-256' }, false, ['encrypt'])
  const enc = await crypto.subtle.encrypt({ name: 'RSA-OAEP' }, key, new TextEncoder().encode(pw))
  return btoa(String.fromCharCode(...new Uint8Array(enc))).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '')
}

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
    } else {
      await auth.login(form.username, await encryptPassword(form.password))
      ElMessage.success('登录成功')
    }
    router.replace('/m/dashboard')
  } catch (e: any) {
    ElMessage.error(e?.response?.data?.message || (isRegister.value ? '注册失败' : '登录失败'))
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.login-mobile { padding: 48px 24px; display: flex; flex-direction: column; gap: 24px; }
.brand { text-align: center; font-size: 28px; color: var(--mf-primary); }
.switch-mode-mobile {
  text-align: center;
  margin-top: 18px;
  font-size: 14px;
  color: var(--mf-text-secondary);
}
</style>
