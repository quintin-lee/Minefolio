<template>
  <el-container class="layout-container">
    <el-aside :width="isCollapsed ? '64px' : '260px'" class="aside" :class="{ 'is-open': mobileMenuOpen, 'is-collapsed': isCollapsed }">
      <div class="logo" @click="goTo('/dashboard')">
        <AppLogo :size="32" :with-text="!isCollapsed" />
      </div>
      <el-menu :default-active="activeMenu" class="sidebar-menu" :collapse="isCollapsed" :collapse-transition="false">
        <div v-show="!isCollapsed" class="nav-group-label">{{ t('navGroups.assetsOverview') }}</div>
        <el-menu-item index="/dashboard" @click="goTo('/dashboard')">
          <Icon icon="ph:squares-four-duotone" class="nav-icon" />
          <span>{{ t('nav.dashboard') }}</span>
        </el-menu-item>
        <el-menu-item index="/assets" @click="goTo('/assets')">
          <Icon icon="ph:vault-duotone" class="nav-icon" />
          <span>{{ t('nav.assets') }}</span>
        </el-menu-item>
        <el-menu-item index="/holdings" @click="goTo('/holdings')">
          <Icon icon="ph:trend-up-duotone" class="nav-icon" />
          <span>{{ t('nav.holdings') }}</span>
        </el-menu-item>
        <el-menu-item index="/reports" @click="goTo('/reports')">
          <Icon icon="ph:presentation-chart-duotone" class="nav-icon" />
          <span>{{ t('nav.reports') }}</span>
        </el-menu-item>

        <div v-show="!isCollapsed" class="nav-group-label">{{ t('navGroups.incomeExpense') }}</div>
        <el-menu-item index="/transactions" @click="goTo('/transactions')">
          <Icon icon="ph:arrows-left-right-duotone" class="nav-icon" />
          <span>{{ t('nav.transactions') }}</span>
        </el-menu-item>
        <el-menu-item index="/daily-expenses" @click="goTo('/daily-expenses')">
          <Icon icon="ph:receipt-duotone" class="nav-icon" />
          <span>{{ t('nav.dailyExpenses') }}</span>
        </el-menu-item>
        <el-menu-item index="/plans" @click="goTo('/plans')">
          <Icon icon="ph:target-duotone" class="nav-icon" />
          <span>{{ t('nav.plans') }}</span>
        </el-menu-item>
        <el-menu-item index="/categories" @click="goTo('/categories')">
          <Icon icon="ph:tree-structure-duotone" class="nav-icon" />
          <span>{{ t('nav.categories') }}</span>
        </el-menu-item>

        <div v-show="!isCollapsed" class="nav-group-label">{{ t('navGroups.aiSpace') }}</div>
        <el-menu-item index="/chat" @click="goTo('/chat')">
          <Icon icon="ph:sparkle-duotone" class="nav-icon" />
          <span>{{ t('nav.aiChat') }}</span>
        </el-menu-item>
        <el-menu-item index="/ai-traces" @click="goTo('/ai-traces')">
          <Icon icon="ph:activity-duotone" class="nav-icon" />
          <span>{{ t('nav.aiTraces') }}</span>
        </el-menu-item>

        <div v-show="!isCollapsed" class="nav-group-label">{{ t('navGroups.system') }}</div>
        <el-menu-item index="/audit-logs" @click="goTo('/audit-logs')">
          <Icon icon="ph:shield-check-duotone" class="nav-icon" />
          <span>{{ t('nav.auditLogs') }}</span>
        </el-menu-item>
        <el-menu-item index="/settings" @click="goTo('/settings')">
          <Icon icon="ph:sliders-horizontal-duotone" class="nav-icon" />
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
          <AppLogo :size="22" :with-text="false" class="header-mobile-logo" />
          <el-tooltip :content="isCollapsed ? t('common.expandMenu') : t('common.collapseMenu')" placement="bottom" :show-after="300">
            <div class="collapse-btn" @click="toggleCollapse">
              <Icon icon="ph:sidebar-simple" class="collapse-icon" />
            </div>
          </el-tooltip>
          <h2 class="page-title">{{ pageTitle }}</h2>
        </div>
        <div class="header-right">
          <el-tooltip :content="t('nav.quickAddHint')" placement="bottom" :show-after="300">
            <el-button type="primary" size="small" class="quick-add-btn" @click="quickRecordVisible = true">
              <Icon icon="ph:plus-bold" class="btn-icon" />
              <span>{{ t('nav.quickAdd') }}</span>
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
            <component :is="Component" :key="`${route.fullPath}-${routerViewKey}`" />
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
import { useCategoryStore } from '@/stores/category'
import { t } from '@/utils/locale'
import LedgerSelector from '@/components/LedgerSelector.vue'
import ThemeToggle from '@/components/ThemeToggle.vue'
import QuickRecordDialog from '@/components/QuickRecordDialog.vue'
import AppLogo from '@/components/AppLogo.vue'

const router = useRouter()
const route = useRoute()
const auth = useAuthStore()
const categoryStore = useCategoryStore()

const activeMenu = computed(() => route.path)
const mobileMenuOpen = ref(false)
const isCollapsed = ref(localStorage.getItem('sidebar_collapsed') === 'true')
const quickRecordVisible = ref(false)
const routerViewKey = ref(0)

function toggleCollapse() {
  isCollapsed.value = !isCollapsed.value
  localStorage.setItem('sidebar_collapsed', String(isCollapsed.value))
}

function handleResize() {
  if (window.innerWidth >= 768) mobileMenuOpen.value = false
}

function handleLedgerChanged() {
  categoryStore.invalidate()
  routerViewKey.value++
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
      '/dashboard': t('nav.dashboard'),
      '/assets': t('nav.assets'),
      '/holdings': t('nav.holdings'),
      '/transactions': t('nav.transactions'),
      '/daily-expenses': t('nav.dailyExpenses'),
      '/categories': t('nav.categories'),
      '/reports': t('nav.reports'),
      '/audit-logs': t('nav.auditLogs'),
      '/settings': t('nav.settings'),
      '/chat': t('nav.aiChat'),
      '/ai-traces': t('nav.aiTraces'),
    }
    return map[route.path] || 'Minefolio'
  })

function handleCommand(cmd: string) {
  if (cmd === 'settings') {
    router.push('/settings')
  } else if (cmd === 'logout') {
    auth.logout()
    ElMessage.success(t('common.logoutSuccess'))
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
  padding: 0 20px;
  cursor: pointer;
  color: var(--mf-text-main);
  border-bottom: 1px solid var(--mf-border);
  transition: padding 0.25s cubic-bezier(0.4, 0, 0.2, 1);
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
  font-size: 19px;
  margin-right: 12px;
  color: currentColor;
  opacity: 0.85;
  transition: transform 0.2s cubic-bezier(0.34, 1.56, 0.64, 1), filter 0.2s ease, opacity 0.2s ease, color 0.2s ease;
}
.sidebar-menu :deep(.el-menu-item:hover .nav-icon) {
  opacity: 1;
  transform: scale(1.12);
  color: var(--mf-primary);
}
.sidebar-menu :deep(.el-menu-item.is-active .nav-icon) {
  opacity: 1;
  color: var(--mf-primary);
  filter: drop-shadow(0 0 6px var(--mf-primary-light));
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
.sidebar-menu :deep(.el-menu-item.is-active)::after {
  content: '';
  position: absolute;
  right: 0;
  top: 50%;
  transform: translateY(-50%);
  width: 3px;
  height: 18px;
  background: var(--mf-primary);
  border-radius: 3px 0 0 3px;
  box-shadow: 0 0 8px var(--mf-primary);
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
  gap: 6px;
  font-weight: 600;
  border-radius: var(--mf-radius-md);
  padding: 0 14px;
  height: 34px;
  background: linear-gradient(135deg, var(--mf-primary) 0%, var(--mf-primary-hover) 100%) !important;
  border: none !important;
  box-shadow: 0 2px 10px rgba(59, 130, 246, 0.35) !important;
  transition: var(--mf-transition) !important;
}
.quick-add-btn:hover {
  box-shadow: 0 4px 16px rgba(59, 130, 246, 0.5) !important;
  transform: translateY(-1px);
}
.quick-add-btn:active {
  transform: translateY(0) scale(0.98);
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
  transition: opacity 0.18s cubic-bezier(0.4, 0, 0.2, 1),
              transform 0.18s cubic-bezier(0.4, 0, 0.2, 1);
}
.fade-transform-enter-from {
  opacity: 0;
  transform: translateY(4px);
}
.fade-transform-leave-to {
  opacity: 0;
  transform: translateY(-4px);
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
.header-mobile-logo {
  display: none;
}

@media (max-width: 768px) {
  .header-mobile-logo {
    display: inline-flex;
    margin-right: 8px;
  }
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
