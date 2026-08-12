<template>
  <el-container class="layout-container">
    <el-aside width="260px" class="aside">
      <div class="logo">
        <div class="logo-icon-wrapper">💰</div>
        <span class="logo-text">Minefolio</span>
      </div>
      <el-menu :default-active="activeMenu" router class="sidebar-menu">
        <el-menu-item index="/dashboard">
          <el-icon><DataAnalysis /></el-icon>
          <span>{{ t('nav.dashboard') }}</span>
        </el-menu-item>
        <el-menu-item index="/assets">
          <el-icon><Wallet /></el-icon>
          <span>{{ t('nav.assets') }}</span>
        </el-menu-item>
        <el-menu-item index="/transactions">
          <el-icon><List /></el-icon>
          <span>{{ t('nav.transactions') }}</span>
        </el-menu-item>
        <el-menu-item index="/daily-expenses">
          <el-icon><Money /></el-icon>
          <span>{{ t('nav.dailyExpenses') }}</span>
        </el-menu-item>
        <el-menu-item index="/categories">
          <el-icon><Folder /></el-icon>
          <span>{{ t('nav.categories') }}</span>
        </el-menu-item>
        <el-menu-item index="/transfer">
          <el-icon><Switch /></el-icon>
          <span>{{ t('nav.transfer') }}</span>
        </el-menu-item>
        <el-menu-item index="/reports">
          <el-icon><PieChart /></el-icon>
          <span>{{ t('nav.reports') }}</span>
        </el-menu-item>
        <el-menu-item index="/audit-logs">
          <el-icon><List /></el-icon>
          <span>{{ t('nav.auditLogs') }}</span>
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
          <transition name="fade-transform" mode="out-in">
            <component :is="Component" />
          </transition>
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

const pageTitle = computed(() => {
  const map: Record<string, string> = {
    '/dashboard': '仪表盘',
    '/assets': '资产管理',
    '/transactions': '交易记录',
    '/daily-expenses': '日常收支',
    '/categories': '分类管理',
    '/transfer': '资产转账',
    '/reports': '报表中心',
    '/audit-logs': '日志',
  }
  return map[route.path] || 'Minefolio'
})

function handleCommand(cmd: string) {
  if (cmd === 'logout') {
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
  background: linear-gradient(180deg, #0f172a 0%, #1e293b 100%);
  min-height: 100vh;
  box-shadow: 4px 0 24px rgba(0, 0, 0, 0.1);
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
  border-bottom: 1px solid rgba(255, 255, 255, 0.05);
}
.logo-icon-wrapper {
  font-size: 24px;
  background: rgba(255, 255, 255, 0.1);
  width: 40px;
  height: 40px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 10px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.1);
}
.logo-text {
  font-size: 22px;
  font-weight: 700;
  letter-spacing: 0.5px;
  background: linear-gradient(to right, #fff, #94a3b8);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
}
.sidebar-menu {
  border-right: none;
  background: transparent;
  padding: 16px 12px;
  flex: 1;
}
.sidebar-menu :deep(.el-menu-item) {
  color: #94a3b8;
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
  color: #f8fafc;
  background: rgba(255, 255, 255, 0.05) !important;
}
.sidebar-menu :deep(.el-menu-item.is-active) {
  color: #fff;
  background: linear-gradient(90deg, rgba(37, 99, 235, 0.15) 0%, transparent 100%) !important;
  position: relative;
}
.sidebar-menu :deep(.el-menu-item.is-active::before) {
  content: '';
  position: absolute;
  left: -12px;
  top: 10%;
  height: 80%;
  width: 4px;
  background: #3b82f6;
  border-radius: 0 4px 4px 0;
  box-shadow: 0 0 10px rgba(59, 130, 246, 0.5);
}

.main-container {
  display: flex;
  flex-direction: column;
}
.header {
  background: #fff;
  height: 72px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 32px;
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.05);
  z-index: 5;
}
.page-title {
  font-size: 20px;
  font-weight: 600;
  color: #0f172a;
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
}
.user-profile:hover {
  background: #f1f5f9;
}
.avatar-circle {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  background: linear-gradient(135deg, #3b82f6 0%, #2dd4bf 100%);
  color: white;
  display: flex;
  align-items: center;
  justify-content: center;
  font-weight: 600;
  font-size: 16px;
  box-shadow: 0 2px 8px rgba(59, 130, 246, 0.3);
}
.username {
  font-weight: 500;
  color: #334155;
}
.user-dropdown .danger-item {
  color: #ef4444;
}
.user-dropdown .danger-item:hover {
  background-color: #fef2f2;
  color: #dc2626;
}
.main {
  padding: 24px 32px;
  background-color: var(--mf-background);
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
</style>
