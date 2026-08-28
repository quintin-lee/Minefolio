# Frontend Left Sidebar Collapse Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement collapsible left sidebar menu in the Vue 3 desktop layout with smooth transitions, localStorage persistence, and tooltip cues.

**Architecture:** Integrate `isCollapsed` reactive state in `Layout.vue` with `localStorage` persistence, wire it into `el-aside :width` and `el-menu :collapse`, and add a dedicated toggle icon button in the header left area.

**Tech Stack:** Vue 3 (Composition API), TypeScript, Element Plus (`el-menu`, `el-aside`, `el-tooltip`), `@iconify/vue`.

---

### Task 1: Add Sidebar Collapse State, Toggle Button, and Styles in Layout.vue

**Files:**
- Modify: `frontend/src/views/Layout.vue`

- [ ] **Step 1: Update `Layout.vue` template with collapse bindings & header toggle button**

```html
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
        <el-menu-item index="/transactions" @click="goTo('/transactions')">
          <Icon icon="ph:list" class="nav-icon" />
          <span>{{ t('nav.transactions') }}</span>
        </el-menu-item>
        <el-menu-item index="/daily-expenses" @click="goTo('/daily-expenses')">
          <Icon icon="ph:currency-cny" class="nav-icon" />
          <span>{{ t('nav.dailyExpenses') }}</span>
        </el-menu-item>
        <el-menu-item index="/categories" @click="goTo('/categories')">
          <Icon icon="ph:folder" class="nav-icon" />
          <span>{{ t('nav.categories') }}</span>
        </el-menu-item>
        <el-menu-item index="/reports" @click="goTo('/reports')">
          <Icon icon="ph:chart-pie" class="nav-icon" />
          <span>{{ t('nav.reports') }}</span>
        </el-menu-item>
        <el-menu-item index="/audit-logs" @click="goTo('/audit-logs')">
          <Icon icon="ph:scroll" class="nav-icon" />
          <span>{{ t('nav.auditLogs') }}</span>
        </el-menu-item>
        <el-menu-item index="/chat" @click="goTo('/chat')">
          <Icon icon="ph:chat-circle-text" class="nav-icon" />
          <span>{{ t('nav.aiChat') }}</span>
        </el-menu-item>
        <el-menu-item index="/ai-traces" @click="goTo('/ai-traces')">
          <Icon icon="ph:scan" class="nav-icon" />
          <span>{{ t('nav.aiTraces') }}</span>
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
          <!-- Mobile hamburger -->
          <el-icon class="hamburger" :class="{ 'is-active': mobileMenuOpen }" @click="mobileMenuOpen = !mobileMenuOpen">
            <Grid />
          </el-icon>
          <!-- Desktop sidebar collapse toggle -->
          <el-tooltip :content="isCollapsed ? '展开菜单' : '收起菜单'" placement="bottom" :show-after="300">
            <div class="collapse-btn" @click="toggleCollapse">
              <Icon :icon="isCollapsed ? 'ph:sidebar-simple' : 'ph:sidebar-simple'" class="collapse-icon" />
            </div>
          </el-tooltip>
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
        <router-view v-slot="{ Component, route }">
          <transition name="fade-transform" mode="out-in">
            <component :is="Component" :key="route.path" />
          </transition>
        </router-view>
      </el-main>
    </el-container>
  </el-container>
</template>
```

- [ ] **Step 2: Add `isCollapsed` reactive state and `toggleCollapse` handler in `Layout.vue` script**

```ts
const isCollapsed = ref(localStorage.getItem('sidebar_collapsed') === 'true')

function toggleCollapse() {
  isCollapsed.value = !isCollapsed.value
  localStorage.setItem('sidebar_collapsed', String(isCollapsed.value))
}
```

- [ ] **Step 3: Add CSS styles for `.aside.is-collapsed`, `.collapse-btn`, and `.el-menu--collapse`**

```css
.aside {
  background: linear-gradient(180deg, #060b18 0%, #0f1d32 100%);
  min-height: 100vh;
  box-shadow: 4px 0 24px rgba(0, 0, 0, 0.4);
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
.collapse-btn {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 34px;
  height: 34px;
  border-radius: 8px;
  cursor: pointer;
  color: #94a3b8;
  margin-right: 14px;
  transition: all 0.2s ease;
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid rgba(255, 255, 255, 0.06);
}
.collapse-btn:hover {
  color: #00d4ff;
  background: rgba(0, 212, 255, 0.08);
  border-color: rgba(0, 212, 255, 0.2);
}
.collapse-icon {
  font-size: 20px;
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
@media (max-width: 768px) {
  .collapse-btn {
    display: none;
  }
}
```

- [ ] **Step 4: Build and verify frontend compilation**

Run: `npm --prefix frontend run build`
Expected: 0 errors, build succeeds.

- [ ] **Step 5: Run tests to verify existing suite passes**

Run: `npm --prefix frontend test`
Expected: PASS (7 tests pass).

- [ ] **Step 6: Commit**

```bash
git add frontend/src/views/Layout.vue
git commit -m "feat(layout): ✨ support collapsible sidebar with persistence"
```
