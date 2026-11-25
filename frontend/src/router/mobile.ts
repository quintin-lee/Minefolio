// frontend/src/router/mobile.ts
import { createRouter, createWebHistory } from 'vue-router'
import { useAuthStore } from '@/stores/auth'

const router = createRouter({
  history: createWebHistory(),
  routes: [
    {
      path: '/',
      redirect: '/m/dashboard',
    },
    {
      path: '/m/login',
      name: 'MobileLogin',
      component: () => import('@/views-mobile/LoginMobile.vue'),
      meta: { requiresAuth: false },
    },
    {
      path: '/oauth/callback',
      name: 'MobileOAuthCallback',
      component: () => import('@/views/OAuthCallback.vue'),
      meta: { public: true, requiresAuth: false },
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
        { path: 'plans', name: 'MobilePlans', component: () => import('@/views-mobile/PlansMobile.vue') },
        { path: 'chat', name: 'MobileChat', component: () => import('@/views/Chat.vue'), meta: { fullBleed: true } },
        { path: 'reports', name: 'MobileReports', component: () => import('@/views-mobile/ReportsMobile.vue') },
        { path: 'settings', name: 'MobileSettings', component: () => import('@/views-mobile/SettingsMobile.vue') },
      ],
    },
    {
      path: '/:pathMatch(.*)*',
      name: 'MobileNotFound',
      component: () => import('@/views/NotFound.vue'),
      meta: { requiresAuth: false },
    },
  ],
})

router.beforeEach(async (to, _from, next) => {
  const auth = useAuthStore()
  if (auth.isInitialized === null) {
    await auth.checkSystemStatus()
  }
  if (auth.isInitialized === false) {
    if (to.path !== '/m/login') {
      return next('/m/login')
    }
    return next()
  }
  if (to.meta.requiresAuth !== false && !auth.token) {
    if (to.path !== '/m/login') {
      return next('/m/login')
    }
    return next()
  }
  if (to.path === '/m/login' && auth.token) {
    return next('/m/dashboard')
  }
  next()
})

export default router
