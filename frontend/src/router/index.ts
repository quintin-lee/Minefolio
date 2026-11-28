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
      path: '/oauth/callback',
      name: 'OAuthCallback',
      component: () => import('@/views/OAuthCallback.vue'),
      meta: { requiresAuth: false },
    },
    { path: '/m/login', redirect: '/login' },
    { path: '/m/dashboard', redirect: '/dashboard' },
    { path: '/m/expenses', redirect: '/daily-expenses' },
    { path: '/m/transactions', redirect: '/transactions' },
    { path: '/m/assets', redirect: '/assets' },
    { path: '/m/holdings', redirect: '/holdings' },
    { path: '/m/plans', redirect: '/plans' },
    { path: '/m/reports', redirect: '/reports' },
    { path: '/m/settings', redirect: '/settings' },
    { path: '/m/assets/:id', redirect: (to) => `/assets/${to.params.id}` },
    { path: '/m/market', redirect: '/market' },
    { path: '/m/news', redirect: '/news' },
    { path: '/m/chat', redirect: '/chat' },
    { path: '/m/categories', redirect: '/categories' },
    { path: '/m', redirect: '/dashboard' },
    {
      path: '/',
      component: () => import('@/views/Layout.vue'),
      meta: { requiresAuth: true },
      children: [
        { path: '', redirect: '/dashboard' },
        { path: 'dashboard', name: 'Dashboard', component: () => import('@/views/Dashboard.vue') },
        { path: 'assets', name: 'Assets', component: () => import('@/views/Assets.vue') },
        { path: 'assets/:id', name: 'AssetDetail', component: () => import('@/views/AssetDetail.vue') },
        { path: 'holdings', name: 'Holdings', component: () => import('@/views/Holdings.vue') },
        { path: 'transactions', name: 'Transactions', component: () => import('@/views/Transactions.vue') },
        { path: 'daily-expenses', name: 'DailyExpenses', component: () => import('@/views/DailyExpenses.vue') },
        { path: 'plans', name: 'Plans', component: () => import('@/views/Plans.vue') },
        { path: 'categories', name: 'Categories', component: () => import('@/views/Categories.vue') },
        { path: 'reports', name: 'Reports', component: () => import('@/views/Reports.vue') },
        { path: 'market', name: 'Market', component: () => import('@/views/Market.vue') },
        { path: 'scheduler', name: 'Scheduler', component: () => import('@/views/Scheduler.vue') },
        { path: 'audit-logs', name: 'AuditLogs', component: () => import('@/views/AuditLogs.vue') },
        { path: 'settings', name: 'Settings', component: () => import('@/views/Settings.vue') },
        { path: 'chat', name: 'Chat', component: () => import('@/views/Chat.vue'), meta: { fullBleed: true } },
        { path: 'skills', name: 'Skills', component: () => import('@/views/Skills.vue') },
        { path: 'mcp', name: 'Mcp', component: () => import('@/views/Mcp.vue') },
        { path: 'news', name: 'News', component: () => import('@/views/NewsPulse.vue') },
        { path: 'ai-traces', name: 'AiTraces', component: () => import('@/views/AiTraces.vue') },
      ],
    },
    {
      path: '/:pathMatch(.*)*',
      name: 'NotFound',
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
  } else if (to.path === '/login' && auth.token) {
    next('/dashboard')
  } else {
    next()
  }
})

export default router
