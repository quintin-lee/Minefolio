<template>
  <div class="not-found-container">
    <div class="not-found-content">
      <h1 class="error-code">404</h1>
      <h2 class="error-title">页面未找到 / Page Not Found</h2>
      <p class="error-desc">抱歉，您访问的页面不存在或已被移除。</p>
      <div class="actions">
        <el-button type="primary" size="large" @click="goHome">
          返回主页 / Back Home
        </el-button>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { useRouter } from 'vue-router'

const router = useRouter()

function goHome() {
  const isMobile = window.location.pathname.startsWith('/m')
  router.push(isMobile ? '/m/dashboard' : '/dashboard')
}
</script>

<style scoped>
.not-found-container {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
  background-color: var(--mf-background, #090d16);
  color: var(--mf-text-main, #f8fafc);
  padding: 24px;
}

.not-found-content {
  text-align: center;
  max-width: 480px;
}

.error-code {
  font-size: 96px;
  font-weight: 800;
  line-height: 1;
  margin: 0 0 16px;
  background: linear-gradient(135deg, var(--mf-primary, #3b82f6) 0%, var(--mf-accent, #6366f1) 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
}

.error-title {
  font-size: 22px;
  font-weight: 600;
  margin: 0 0 12px;
}

.error-desc {
  font-size: 14px;
  color: var(--mf-text-muted, #94a3b8);
  margin: 0 0 24px;
}

.actions {
  display: flex;
  justify-content: center;
}
</style>
