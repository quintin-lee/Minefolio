import { createRouter, createWebHistory } from 'vue-router'
import { useAuthStore } from '@/stores/auth'

const router = createRouter({
  history: createWebHistory(),
  routes: [
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
        { path: 'transactions', name: 'Transactions', component: () => import('@/views/Transactions.vue') },
        { path: 'daily-expenses', name: 'DailyExpenses', component: () => import('@/views/DailyExpenses.vue') },
        { path: 'categories', name: 'Categories', component: () => import('@/views/Categories.vue') },
        { path: 'transfer', name: 'Transfer', component: () => import('@/views/Transfer.vue') },
        { path: 'reports', name: 'Reports', component: () => import('@/views/Reports.vue') },
        { path: 'audit-logs', name: 'AuditLogs', component: () => import('@/views/AuditLogs.vue') },
      ],
    },
  ],
})

router.beforeEach((to, _from, next) => {
  const auth = useAuthStore()
  if (to.meta.requiresAuth !== false && !auth.token) {
    next('/login')
  } else {
    next()
  }
})

export default router
