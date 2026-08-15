<template>
  <div class="login-mobile">
    <h1 class="brand">Minefolio</h1>
    <el-form :model="form" label-position="top">
      <el-form-item label="用户名">
        <el-input v-model="form.username" placeholder="用户名" />
      </el-form-item>
      <el-form-item label="密码">
        <el-input v-model="form.password" type="password" placeholder="密码" @keyup.enter="submit" />
      </el-form-item>
      <el-button type="primary" :loading="loading" @click="submit" block>登录</el-button>
    </el-form>
  </div>
</template>

<script setup lang="ts">
import { reactive, ref } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { useAuthStore } from '@/stores/auth'

const router = useRouter()
const auth = useAuthStore()
const loading = ref(false)
const form = reactive({ username: '', password: '' })

// 🔴 移动端内联 RSA 加密：Capacitor 无 dev proxy，必须用绝对地址取公钥
async function encryptPassword(pw: string): Promise<string> {
  const base = import.meta.env.VITE_API_URL || window.location.origin
  const r = await fetch(`${base}/api/auth/public-key`)
  if (!r.ok) throw new Error('Failed to fetch public key')
  const jwk = (await r.json()).data.public_key
  const key = await crypto.subtle.importKey('jwk', jwk, { name: 'RSA-OAEP', hash: 'SHA-256' }, false, ['encrypt'])
  const enc = await crypto.subtle.encrypt({ name: 'RSA-OAEP' }, key, new TextEncoder().encode(pw))
  return btoa(String.fromCharCode(...new Uint8Array(enc))).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '')
}

async function submit() {
  if (!form.username || !form.password) return ElMessage.warning('请输入用户名和密码')
  loading.value = true
  try {
    await auth.login(form.username, await encryptPassword(form.password))
    router.replace('/m/dashboard')
  } catch {
    ElMessage.error('登录失败')
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.login-mobile { padding: 48px 24px; display: flex; flex-direction: column; gap: 24px; }
.brand { text-align: center; font-size: 28px; color: var(--mf-primary); }
</style>
