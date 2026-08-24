import { createRouter, createWebHistory } from 'vue-router'
import { useAuthStore } from '@/stores/auth'

const router = createRouter({
  history: createWebHistory(),
  routes: [
    {
      path: '/setup',
      name: 'Setup',
      component: () => import('@/views/Setup.vue'),
      meta: { requiresAuth: false },
    },
    {
      path: '/login',
      name: 'Login',
      component: () => import('@/views/Login.vue'),
      meta: { requiresAuth: false },
    },
    {
      path: '/',
      component: () => import('@/views/Layout.vue'),
      meta: { requiresAuth: true },
      children: [
        { path: '', redirect: '/dashboard' },
        { path: 'dashboard', name: 'Dashboard', component: () => import('@/views/Dashboard.vue') },
        { path: 'assets', name: 'Assets', component: () => import('@/views/Assets.vue') },
        { path: 'holdings', name: 'Holdings', component: () => import('@/views/Holdings.vue') },
        { path: 'transactions', name: 'Transactions', component: () => import('@/views/Transactions.vue') },
        { path: 'daily-expenses', name: 'DailyExpenses', component: () => import('@/views/DailyExpenses.vue') },
        { path: 'categories', name: 'Categories', component: () => import('@/views/Categories.vue') },
        { path: 'reports', name: 'Reports', component: () => import('@/views/Reports.vue') },
        { path: 'audit-logs', name: 'AuditLogs', component: () => import('@/views/AuditLogs.vue') },
        { path: 'settings', name: 'Settings', component: () => import('@/views/Settings.vue') },
        { path: 'chat', name: 'Chat', component: () => import('@/views/Chat.vue') },
      ],
    },
  ],
})

router.beforeEach(async (to, _from, next) => {
  const auth = useAuthStore()

  if (auth.isInitialized === null) {
    await auth.checkSystemStatus()
  }

  if (auth.isInitialized === false) {
    if (to.path !== '/setup') {
      return next('/setup')
    }
  } else if (auth.isInitialized === true) {
    if (to.path === '/setup') {
      return next('/login')
    }
  }

  if (to.meta.requiresAuth !== false && !auth.token) {
    next('/login')
  } else {
    next()
  }
})

export default router
