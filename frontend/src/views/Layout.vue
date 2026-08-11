<template>
  <el-container class="layout-container">
    <el-aside width="220px" class="aside">
      <div class="logo">
        <span class="logo-icon">💰</span>
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
      </el-menu>
    </el-aside>

    <el-container>
      <el-header class="header">
        <div class="header-left">
          <span class="page-title">{{ pageTitle }}</span>
        </div>
        <div class="header-right">
          <el-dropdown @command="handleCommand">
            <span class="user-info">
              <el-icon><User /></el-icon>
              {{ auth.user?.username }}
              <el-icon class="el-icon--right"><ArrowDown /></el-icon>
            </span>
            <template #dropdown>
              <el-dropdown-menu>
                <el-dropdown-item command="logout">
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
          <transition name="fade" mode="out-in">
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
}
.aside {
  background: #304156;
  min-height: 100vh;
}
.logo {
  height: 60px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  color: #fff;
  font-size: 20px;
  font-weight: bold;
  border-bottom: 1px solid #3d4d60;
}
.logo-icon {
  font-size: 24px;
}
.sidebar-menu {
  border-right: none;
  background: #304156;
}
.sidebar-menu .el-menu-item {
  color: #bfcbd9;
}
.sidebar-menu .el-menu-item:hover,
.sidebar-menu .el-menu-item.is-active {
  color: #fff;
  background: #263445 !important;
}
.header {
  background: #fff;
  border-bottom: 1px solid #e4e7ed;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 20px;
}
.page-title {
  font-size: 18px;
  font-weight: 600;
  color: #303133;
}
.user-info {
  display: flex;
  align-items: center;
  gap: 4px;
  cursor: pointer;
  color: #606266;
}
.main {
  background: #f5f7fa;
  padding: 20px;
}
.fade-enter-active,
.fade-leave-active {
  transition: opacity 0.2s ease;
}
.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}
</style>
