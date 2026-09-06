<template>
  <el-container class="layout-container">
    <el-aside :width="isCollapsed ? '64px' : '260px'" class="aside" :class="{ 'is-open': mobileMenuOpen, 'is-collapsed': isCollapsed }">
      <div class="logo">
        <div class="logo-icon-wrapper">
          <Icon icon="ph:wallet" class="logo-icon-phosphor" />
        </div>
        <span v-show="!isCollapsed" class="logo-text">Minefolio</span>
      </div>
      <el-menu :default-active="activeMenu" class="sidebar-menu" :collapse="isCollapsed" :collapse-transition="false">
        <div v-show="!isCollapsed" class="nav-group-label">资产全景</div>
        <el-menu-item index="/dashboard" @click="goTo('/dashboard')">
          <Icon icon="ph:chart-line" class="nav-icon" />
          <span>{{ t('nav.dashboard') }}</span>
        </el-menu-item>
        <el-menu-item index="/assets" @click="goTo('/assets')">
          <Icon icon="ph:wallet" class="nav-icon" />
          <span>{{ t('nav.assets') }}</span>
        </el-menu-item>
        <el-menu-item index="/holdings" @click="goTo('/holdings')">
          <Icon icon="ph:chart-bar" class="nav-icon" />
          <span>{{ t('nav.holdings') }}</span>
        </el-menu-item>
        <el-menu-item index="/reports" @click="goTo('/reports')">
          <Icon icon="ph:chart-pie" class="nav-icon" />
          <span>{{ t('nav.reports') }}</span>
        </el-menu-item>

        <div v-show="!isCollapsed" class="nav-group-label">收支管理</div>
        <el-menu-item index="/transactions" @click="goTo('/transactions')">
          <Icon icon="ph:list" class="nav-icon" />
          <span>{{ t('nav.transactions') }}</span>
        </el-menu-item>
        <el-menu-item index="/daily-expenses" @click="goTo('/daily-expenses')">
          <Icon icon="ph:currency-cny" class="nav-icon" />
          <span>{{ t('nav.dailyExpenses') }}</span>
        </el-menu-item>
        <el-menu-item index="/plans" @click="goTo('/plans')">
          <Icon icon="ph:calendar-check" class="nav-icon" />
          <span>{{ t('nav.plans') }}</span>
        </el-menu-item>
        <el-menu-item index="/categories" @click="goTo('/categories')">
          <Icon icon="ph:folder" class="nav-icon" />
          <span>{{ t('nav.categories') }}</span>
        </el-menu-item>

        <div v-show="!isCollapsed" class="nav-group-label">AI 智能空间</div>
        <el-menu-item index="/chat" @click="goTo('/chat')">
          <Icon icon="ph:chat-circle-text" class="nav-icon" />
          <span>{{ t('nav.aiChat') }}</span>
        </el-menu-item>
        <el-menu-item index="/ai-traces" @click="goTo('/ai-traces')">
          <Icon icon="ph:scan" class="nav-icon" />
          <span>{{ t('nav.aiTraces') }}</span>
        </el-menu-item>

        <div v-show="!isCollapsed" class="nav-group-label">系统管理</div>
        <el-menu-item index="/audit-logs" @click="goTo('/audit-logs')">
          <Icon icon="ph:scroll" class="nav-icon" />
          <span>{{ t('nav.auditLogs') }}</span>
        </el-menu-item>
        <el-menu-item index="/settings" @click="goTo('/settings')">
          <Icon icon="ph:gear" class="nav-icon" />
          <span>{{ t('nav.settings') }}</span>
        </el-menu-item>
      </el-menu>
    </el-aside>

    <el-container class="main-container">
      <el-header class="header">
        <div class="header-left">
          <el-icon class="hamburger" :class="{ 'is-active': mobileMenuOpen }" @click="mobileMenuOpen = !mobileMenuOpen">
            <Grid />
          </el-icon>
          <el-tooltip :content="isCollapsed ? '展开菜单' : '收起菜单'" placement="bottom" :show-after="300">
            <div class="collapse-btn" @click="toggleCollapse">
              <Icon icon="ph:sidebar-simple" class="collapse-icon" />
            </div>
          </el-tooltip>
          <h2 class="page-title">{{ pageTitle }}</h2>
        </div>
        <div class="header-right">
          <el-tooltip content="快捷记账 (按快捷键 N)" placement="bottom" :show-after="300">
            <el-button type="primary" size="small" class="quick-add-btn" @click="quickRecordVisible = true">
              <Icon icon="ph:plus-bold" class="btn-icon" />
              <span>记账</span>
            </el-button>
          </el-tooltip>
          <LedgerSelector />
          <ThemeToggle />
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
        <router-view v-slot="{ Component, route }">
          <transition name="fade-transform" mode="out-in">
            <component :is="Component" :key="route.path" />
          </transition>
        </router-view>
      </el-main>
    </el-container>

    <!-- 全局快捷记账弹窗 -->
    <QuickRecordDialog v-model="quickRecordVisible" />
  </el-container>
</template>

<script setup lang="ts">
import { computed, ref, onMounted, onBeforeUnmount } from 'vue'
import { Grid } from '@element-plus/icons-vue'
import { Icon } from '@iconify/vue'
import { useRouter, useRoute } from 'vue-router'
import { ElMessage } from 'element-plus'
import { useAuthStore } from '@/stores/auth'
import { t } from '@/utils/locale'
import LedgerSelector from '@/components/LedgerSelector.vue'
import ThemeToggle from '@/components/ThemeToggle.vue'
import QuickRecordDialog from '@/components/QuickRecordDialog.vue'

const router = useRouter()
const route = useRoute()
const auth = useAuthStore()

const activeMenu = computed(() => route.path)
const mobileMenuOpen = ref(false)
const isCollapsed = ref(localStorage.getItem('sidebar_collapsed') === 'true')
const quickRecordVisible = ref(false)

function toggleCollapse() {
  isCollapsed.value = !isCollapsed.value
  localStorage.setItem('sidebar_collapsed', String(isCollapsed.value))
}

function handleResize() {
  if (window.innerWidth >= 768) mobileMenuOpen.value = false
}

function handleLedgerChanged() {
  // Reload current route data
  const cur = route.fullPath
  router.replace({ path: '/empty' }).then(() => {
    router.replace(cur)
  }).catch(() => {
    window.location.reload()
  })
}

function handleKeydown(e: KeyboardEvent) {
  if (e.key === 'n' || e.key === 'N') {
    const target = e.target as HTMLElement
    if (target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.isContentEditable)) {
      return
    }
    e.preventDefault()
    quickRecordVisible.value = true
  }
}

onMounted(() => {
  window.addEventListener('resize', handleResize)
  window.addEventListener('keydown', handleKeydown)
  window.addEventListener('minefolio:ledger-changed', handleLedgerChanged)
  handleResize()
})
onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
  window.removeEventListener('keydown', handleKeydown)
  window.removeEventListener('minefolio:ledger-changed', handleLedgerChanged)
})

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
      '/chat': 'AI助手',
      '/ai-traces': 'AI追踪',
    };
    return map[route.path] || 'Minefolio';
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
}
.aside {
  background: linear-gradient(180deg, var(--mf-background) 0%, color-mix(in srgb, var(--mf-background) 70%, var(--mf-primary)) 100%);
  min-height: 100vh;
  box-shadow: var(--mf-shadow-md);
  z-index: 10;
  display: flex;
  flex-direction: column;
  transition: width 0.25s cubic-bezier(0.4, 0, 0.2, 1);
  overflow-x: hidden;
}
.aside.is-collapsed .logo {
  padding: 0;
  justify-content: center;
}
.logo {
  height: 72px;
  display: flex;
  align-items: center;
  padding: 0 24px;
  gap: 12px;
  color: var(--mf-text-main);
  border-bottom: 1px solid var(--mf-border);
  transition: padding 0.25s cubic-bezier(0.4, 0, 0.2, 1);
}
.logo-icon-wrapper {
  font-size: 24px;
  background: var(--mf-primary-light);
  width: 40px;
  height: 40px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: var(--mf-radius-md);
  border: 1px solid var(--mf-border);
  box-shadow: var(--mf-shadow-sm);
}
.logo-icon-phosphor {
  font-size: 22px;
  color: var(--mf-primary);
}
.logo-text {
  font-size: 20px;
  font-weight: 700;
  letter-spacing: 0.5px;
  background: linear-gradient(135deg, var(--mf-primary) 0%, var(--mf-accent) 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
}
.sidebar-menu {
  border-right: none;
  background: transparent;
  padding: 12px 8px;
  flex: 1;
}
.nav-group-label {
  font-size: 11px;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.6px;
  color: var(--mf-text-placeholder);
  padding: 14px 12px 6px;
  user-select: none;
}
.sidebar-menu :deep(.el-menu-item) {
  color: var(--mf-text-muted);
  border-radius: var(--mf-radius-md);
  margin-bottom: 3px;
  height: 42px;
  line-height: 42px;
  font-weight: 500;
  transition: var(--mf-transition);
}
.sidebar-menu :deep(.el-menu-item .el-icon) {
  font-size: 18px;
  margin-right: 10px;
}
.sidebar-menu :deep(.nav-icon) {
  font-size: 18px;
  margin-right: 10px;
  color: currentColor;
  opacity: 0.85;
  transition: opacity 0.2s ease;
}
.sidebar-menu :deep(.el-menu-item:hover .nav-icon),
.sidebar-menu :deep(.el-menu-item.is-active .nav-icon) {
  opacity: 1;
}
.sidebar-menu :deep(.el-menu-item:hover) {
  color: var(--mf-text-main);
  background: var(--mf-surface-hover) !important;
}
.sidebar-menu :deep(.el-menu-item.is-active) {
  color: var(--mf-primary) !important;
  background: var(--mf-primary-light) !important;
  font-weight: 600;
  position: relative;
}
.sidebar-menu :deep(.el-menu-item.is-active::before) {
  content: '';
  position: absolute;
  left: 0;
  top: 15%;
  height: 70%;
  width: 3px;
  background: var(--mf-primary);
  border-radius: 0 2px 2px 0;
}

.sidebar-menu.el-menu--collapse {
  width: 64px;
  padding: 16px 6px;
}
.sidebar-menu.el-menu--collapse :deep(.el-menu-item) {
  padding: 0 !important;
  justify-content: center;
  text-align: center;
}
.sidebar-menu.el-menu--collapse :deep(.nav-icon) {
  margin-right: 0;
}
.sidebar-menu.el-menu--collapse :deep(.el-menu-item.is-active::before) {
  left: 0;
  width: 3px;
}

.main-container {
  display: flex;
  flex-direction: column;
}
.header {
  background: var(--mf-surface);
  backdrop-filter: blur(16px);
  -webkit-backdrop-filter: blur(16px);
  height: 64px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 28px;
  box-shadow: var(--mf-shadow-sm);
  border-bottom: 1px solid var(--mf-border);
  z-index: 5;
}
.header-left {
  display: flex;
  align-items: center;
}
.header-right {
  display: flex;
  align-items: center;
  gap: 12px;
}
.quick-add-btn {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-weight: 500;
  border-radius: var(--mf-radius-md);
  padding: 0 12px;
  height: 34px;
}
.quick-add-btn .btn-icon {
  font-size: 14px;
}
.collapse-btn {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 34px;
  height: 34px;
  border-radius: var(--mf-radius-md);
  cursor: pointer;
  color: var(--mf-text-muted);
  margin-right: 14px;
  transition: var(--mf-transition);
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border);
}
.collapse-btn:hover {
  color: var(--mf-primary);
  background: var(--mf-surface-hover);
  border-color: var(--mf-border-hover);
}
.collapse-icon {
  font-size: 19px;
}
.page-title {
  font-size: 18px;
  font-weight: 600;
  color: var(--mf-text-main);
  margin: 0;
}
.user-profile {
  display: flex;
  align-items: center;
  gap: 10px;
  cursor: pointer;
  padding: 4px 10px;
  border-radius: 30px;
  transition: var(--mf-transition);
  border: 1px solid transparent;
}
.user-profile:hover {
  background: var(--mf-surface-hover);
  border-color: var(--mf-border);
}
.avatar-circle {
  width: 32px;
  height: 32px;
  border-radius: 50%;
  background: linear-gradient(135deg, var(--mf-primary) 0%, var(--mf-accent) 100%);
  color: white;
  display: flex;
  align-items: center;
  justify-content: center;
  font-weight: 600;
  font-size: 14px;
  box-shadow: 0 2px 8px rgba(59, 130, 246, 0.35);
}
.username {
  font-weight: 500;
  color: var(--mf-text-regular);
  font-size: 14px;
}
.user-dropdown .danger-item {
  color: var(--mf-danger);
}
.user-dropdown .danger-item:hover {
  background-color: var(--mf-danger-light);
  color: var(--mf-danger);
}
.main {
  padding: 24px 28px;
  padding-bottom: 0;
  background-color: var(--mf-background);
  height: calc(100vh - 64px);
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

.hamburger {
  display: none;
  font-size: 22px;
  cursor: pointer;
  color: var(--mf-text-muted);
  padding: 4px;
  margin-right: 12px;
  transition: color 0.2s;
}
.hamburger.is-active {
  color: var(--mf-primary);
}
.hamburger:hover {
  color: var(--mf-text-main);
}

@media (max-width: 768px) {
  .aside {
    position: fixed;
    left: -260px;
    top: 0;
    height: 100vh;
    z-index: 100;
    transition: left 0.3s ease;
  }
  .aside.is-open {
    left: 0;
  }
  .collapse-btn {
    display: none;
  }
  .hamburger {
    display: flex;
    align-items: center;
  }
  .main {
    padding: 16px;
    width: 100%;
  }
  .header {
    padding: 0 16px;
  }
  .username {
    display: none;
  }
}
</style>
