<template>
  <div class="mobile-layout">
    <main class="mobile-content">
      <router-view />
    </main>
    <nav class="tab-bar">
      <button
        v-for="tab in tabs"
        :key="tab.name"
        class="tab-item"
        :class="{ active: route.path.startsWith(tab.prefix) }"
        @click="go(tab)"
      >
        <el-icon :size="22"><component :is="tab.icon" /></el-icon>
        <span>{{ tab.label }}</span>
      </button>
    </nav>
  </div>
</template>

<script setup lang="ts">
import { useRoute, useRouter } from 'vue-router'
import { DataAnalysis, Plus, Wallet, PieChart, Setting } from '@element-plus/icons-vue'

const route = useRoute()
const router = useRouter()

const tabs = [
  { name: 'dashboard', label: '首页', icon: DataAnalysis, prefix: '/m/dashboard' },
  { name: 'expenses', label: '记账', icon: Plus, prefix: '/m/expenses' },
  { name: 'assets', label: '资产', icon: Wallet, prefix: '/m/assets' },
  { name: 'reports', label: '报表', icon: PieChart, prefix: '/m/reports' },
  { name: 'settings', label: '我的', icon: Setting, prefix: '/m/settings' },
]

function go(tab: (typeof tabs)[number]) {
  router.push(tab.prefix)
}
</script>

<style scoped>
.mobile-layout {
  display: flex;
  flex-direction: column;
  height: 100vh;
  background: var(--mf-background);
}
.mobile-content {
  flex: 1;
  overflow-y: auto;
  padding: 16px;
  padding-bottom: 80px;
}
.tab-bar {
  position: fixed;
  bottom: 0;
  left: 0;
  right: 0;
  height: 64px;
  display: flex;
  background: var(--mf-surface);
  border-top: 1px solid var(--mf-border);
  padding-bottom: env(safe-area-inset-bottom);
}
.tab-item {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 2px;
  background: none;
  border: none;
  color: var(--mf-text-muted);
  font-size: 12px;
  cursor: pointer;
}
.tab-item.active {
  color: var(--mf-primary);
}
</style>
