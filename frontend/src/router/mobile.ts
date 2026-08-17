// frontend/src/router/mobile.ts
import { createRouter, createWebHistory } from 'vue-router'
import { useAuthStore } from '@/stores/auth'

const router = createRouter({
  history: createWebHistory(),
  routes: [
    {
      path: '/m/login',
      name: 'MobileLogin',
      component: () => import('@/views-mobile/LoginMobile.vue'),
      meta: { requiresAuth: false },
    },
    {
      path: '/m',
      component: () => import('@/views-mobile/MobileLayout.vue'),
      meta: { requiresAuth: true },
      children: [
        { path: '', redirect: '/m/dashboard' },
        { path: 'dashboard', name: 'MobileDashboard', component: () => import('@/views-mobile/DashboardMobile.vue') },
        { path: 'expenses', name: 'MobileExpenses', component: () => import('@/views-mobile/DailyExpensesMobile.vue') },
        { path: 'transactions', name: 'MobileTransactions', component: () => import('@/views-mobile/TransactionsMobile.vue') },
        { path: 'assets', name: 'MobileAssets', component: () => import('@/views-mobile/AssetsMobile.vue') },
        { path: 'holdings', name: 'MobileHoldings', component: () => import('@/views-mobile/HoldingsMobile.vue') },
        { path: 'reports', name: 'MobileReports', component: () => import('@/views-mobile/ReportsMobile.vue') },
        { path: 'settings', name: 'MobileSettings', component: () => import('@/views-mobile/SettingsMobile.vue') },
      ],
    },
  ],
})

router.beforeEach(async (to, _from, next) => {
  const auth = useAuthStore()
  if (auth.isInitialized === null) await auth.checkSystemStatus()
  if (auth.isInitialized === false) return next('/m/login')
  if (to.meta.requiresAuth !== false && !auth.token) next('/m/login')
  else next()
})

export default router
