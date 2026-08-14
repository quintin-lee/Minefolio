<template>
  <el-container class="layout-container">
    <el-aside width="260px" class="aside">
      <div class="logo">
        <div class="logo-icon-wrapper">💰</div>
        <span class="logo-text">Minefolio</span>
      </div>
      <el-menu :default-active="activeMenu" class="sidebar-menu">
        <el-menu-item index="/dashboard" @click="goTo('/dashboard')">
          <el-icon><DataAnalysis /></el-icon>
          <span>{{ t('nav.dashboard') }}</span>
        </el-menu-item>
        <el-menu-item index="/assets" @click="goTo('/assets')">
          <el-icon><Wallet /></el-icon>
          <span>{{ t('nav.assets') }}</span>
        </el-menu-item>
        <el-menu-item index="/holdings" @click="goTo('/holdings')">
          <el-icon><TrendCharts /></el-icon>
          <span>{{ t('nav.holdings') }}</span>
        </el-menu-item>
        <el-menu-item index="/transactions" @click="goTo('/transactions')">
          <el-icon><List /></el-icon>
          <span>{{ t('nav.transactions') }}</span>
        </el-menu-item>
        <el-menu-item index="/daily-expenses" @click="goTo('/daily-expenses')">
          <el-icon><Money /></el-icon>
          <span>{{ t('nav.dailyExpenses') }}</span>
        </el-menu-item>
        <el-menu-item index="/categories" @click="goTo('/categories')">
          <el-icon><Folder /></el-icon>
          <span>{{ t('nav.categories') }}</span>
        </el-menu-item>
        <el-menu-item index="/reports" @click="goTo('/reports')">
          <el-icon><PieChart /></el-icon>
          <span>{{ t('nav.reports') }}</span>
        </el-menu-item>
        <el-menu-item index="/audit-logs" @click="goTo('/audit-logs')">
          <el-icon><List /></el-icon>
          <span>{{ t('nav.auditLogs') }}</span>
        </el-menu-item>
        <el-menu-item index="/settings" @click="goTo('/settings')">
          <el-icon><Setting /></el-icon>
          <span>{{ t('nav.settings') }}</span>
        </el-menu-item>
      </el-menu>
    </el-aside>

    <el-container class="main-container">
      <el-header class="header">
        <div class="header-left">
          <h2 class="page-title">{{ pageTitle }}</h2>
        </div>
        <div class="header-right">
          <el-dropdown @command="handleCommand" trigger="click">
            <div class="user-profile">
              <div class="avatar-circle">
                {{ auth.user?.username?.charAt(0).toUpperCase() || 'U' }}
              </div>
              <span class="username">{{ auth.user?.username }}</span>
              <el-icon><ArrowDown /></el-icon>
            </div>
            <template #dropdown>
              <el-dropdown-menu class="user-dropdown">
                <el-dropdown-item command="settings">
                  <el-icon><Setting /></el-icon>
                  {{ t('nav.changePassword') }}
                </el-dropdown-item>
                <el-dropdown-item command="logout" class="danger-item">
                  <el-icon><SwitchButton /></el-icon>
                  {{ t('nav.logout') }}
                </el-dropdown-item>
              </el-dropdown-menu>
            </template>
          </el-dropdown>
        </div>
      </el-header>

      <el-main class="main">
        <router-view v-slot="{ Component }">
          <suspense>
            <template #default>
              <transition name="fade-transform" mode="out-in">
                <component :is="Component" />
              </transition>
            </template>
            <template #fallback>
              <div class="page-loading">加载中...</div>
            </template>
          </suspense>
        </router-view>
      </el-main>
    </el-container>
  </el-container>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useRouter, useRoute } from 'vue-router'
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
const route = useRoute()
const auth = useAuthStore()

const activeMenu = computed(() => route.path)

function goTo(path: string) {
  router.push(path)
}

  const pageTitle = computed(() => {
    const map: Record<string, string> = {
      '/dashboard': '仪表盘',
      '/assets': '资产',
      '/holdings': '持仓',
      '/transactions': '交易',
      '/daily-expenses': '收支',
      '/categories': '分类',
      '/reports': '报表',
      '/audit-logs': '日志',
      '/settings': '设置',
    }
    return map[route.path] || 'Minefolio'
  })

function handleCommand(cmd: string) {
  if (cmd === 'settings') {
    router.push('/settings')
  } else if (cmd === 'logout') {
    auth.logout()
    ElMessage.success('已退出登录')
    router.push('/login')
  }
}
</script>

<style scoped>
.layout-container {
  min-height: 100vh;
  background-color: var(--mf-background);
}
.aside {
  background: linear-gradient(180deg, #060b18 0%, #0f1d32 100%);
  min-height: 100vh;
  box-shadow: 4px 0 24px rgba(0, 0, 0, 0.4);
  z-index: 10;
  display: flex;
  flex-direction: column;
}
.logo {
  height: 72px;
  display: flex;
  align-items: center;
  padding: 0 24px;
  gap: 12px;
  color: #fff;
  border-bottom: 1px solid rgba(0, 212, 255, 0.1);
}
.logo-icon-wrapper {
  font-size: 24px;
  background: rgba(0, 212, 255, 0.1);
  width: 40px;
  height: 40px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 10px;
  border: 1px solid rgba(0, 212, 255, 0.2);
  box-shadow: 0 0 12px rgba(0, 212, 255, 0.15);
}
.logo-text {
  font-size: 22px;
  font-weight: 700;
  letter-spacing: 1px;
  background: linear-gradient(to right, #00d4ff, #a5f3fc);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  text-shadow: 0 0 20px rgba(0, 212, 255, 0.3);
}
.sidebar-menu {
  border-right: none;
  background: transparent;
  padding: 16px 12px;
  flex: 1;
}
.sidebar-menu :deep(.el-menu-item) {
  color: #64748b;
  border-radius: 8px;
  margin-bottom: 4px;
  height: 50px;
  line-height: 50px;
  transition: all 0.3s ease;
}
.sidebar-menu :deep(.el-menu-item .el-icon) {
  font-size: 20px;
  margin-right: 12px;
}
.sidebar-menu :deep(.el-menu-item:hover) {
  color: #e2e8f0;
  background: rgba(0, 212, 255, 0.06) !important;
}
.sidebar-menu :deep(.el-menu-item.is-active) {
  color: #00d4ff;
  background: rgba(0, 212, 255, 0.08) !important;
  position: relative;
}
.sidebar-menu :deep(.el-menu-item.is-active::before) {
  content: '';
  position: absolute;
  left: -12px;
  top: 10%;
  height: 80%;
  width: 3px;
  background: #00d4ff;
  border-radius: 0 2px 2px 0;
  box-shadow: 0 0 12px rgba(0, 212, 255, 0.6);
}

.main-container {
  display: flex;
  flex-direction: column;
}
.header {
  background: rgba(15, 23, 42, 0.85);
  backdrop-filter: blur(12px);
  height: 72px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 32px;
  box-shadow: 0 1px 8px rgba(0, 0, 0, 0.4);
  border-bottom: 1px solid rgba(0, 212, 255, 0.08);
  z-index: 5;
}
.page-title {
  font-size: 20px;
  font-weight: 600;
  color: #e2e8f0;
  margin: 0;
}
.user-profile {
  display: flex;
  align-items: center;
  gap: 12px;
  cursor: pointer;
  padding: 6px 12px;
  border-radius: 30px;
  transition: background 0.2s;
  border: 1px solid transparent;
}
.user-profile:hover {
  background: rgba(0, 212, 255, 0.06);
  border-color: rgba(0, 212, 255, 0.15);
}
.avatar-circle {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  background: linear-gradient(135deg, #00d4ff 0%, #7c3aed 100%);
  color: white;
  display: flex;
  align-items: center;
  justify-content: center;
  font-weight: 600;
  font-size: 16px;
  box-shadow: 0 0 12px rgba(0, 212, 255, 0.4);
}
.username {
  font-weight: 500;
  color: #94a3b8;
}
.user-dropdown .danger-item {
  color: #f87171;
}
.user-dropdown .danger-item:hover {
  background-color: rgba(239, 68, 68, 0.1);
  color: #f87171;
}
.main {
  padding: 24px 32px;
  padding-bottom: 0;
  background-color: var(--mf-background);
  height: calc(100vh - 72px);
  overflow-y: auto;
  display: flex;
  flex-direction: column;
}
.fade-transform-enter-active,
.fade-transform-leave-active {
  transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
}
.fade-transform-enter-from {
  opacity: 0;
  transform: translateX(15px);
}
.fade-transform-leave-to {
  opacity: 0;
  transform: translateX(-15px);
}
.page-loading {
  display: flex;
  align-items: center;
  justify-content: center;
  height: 200px;
  color: #64748b;
  font-size: 14px;
}
</style>
