// frontend/src/main-mobile.ts
import { createApp } from 'vue'
import { createPinia } from 'pinia'
import 'element-plus/dist/index.css'
import './styles/index.css'
import { registerIcons } from '@/icons'
import App from './App.vue'
import router from './router/mobile'
import i18n from '@/composables/useI18n'
import { initLocalDb } from '@/db/local'
import { useAuthStore } from '@/stores/auth'
import { useSyncStore, setSyncOnAuthFail } from '@/stores/sync'
import { SyncEngine } from '@/sync/SyncEngine'
import { registerNetworkListeners } from '@/utils/sync-network'
import { setMobileMode, setAuthErrorHandler } from '@/utils/http'
import { hideNativeSplash, dismissBootSplash } from '@/utils/boot-splash'

setMobileMode(true)
const handleMobileAuthFail = () => {
  if (router.currentRoute.value?.path !== '/m/login') {
    router.push('/m/login')
  }
}
setAuthErrorHandler(handleMobileAuthFail)
setSyncOnAuthFail(handleMobileAuthFail)

async function bootstrap() {
  // Hand off from static native SplashScreen to the inline HTML boot animation.
  void hideNativeSplash()

  await initLocalDb()
  const app = createApp(App)

  app.config.errorHandler = (err, _instance, info) => {
    console.error('[MobileGlobalErrorHandler]', err, info)
  }

  registerIcons(app)

  app.use(createPinia())
  app.use(router)
  app.use(i18n)

  // SyncEngine 唯一执行者：显式注册 6 张可写表（构造函数已默认注册，幂等）
  SyncEngine.getInstance().registerDefaultTables()
  const sync = useSyncStore()
  sync.init()
  sync.subscribeSyncEngine()
  registerNetworkListeners(() => {
    if (!Boolean(useAuthStore().token)) return
    return sync.syncNow()
  })

  app.mount('#app')
  await router.isReady()
  dismissBootSplash()
}

bootstrap().catch((err) => {
  dismissBootSplash(true)
  // Defense-in-depth: never leave a silent blank screen. If local-DB/wasm init
  // or mount fails, render a visible error so the user knows something broke.
  console.error('[minefolio-mobile] bootstrap failed:', err)
  const root = document.getElementById('app')
  if (root) {
    const box = document.createElement('div')
    box.style.cssText = 'padding:24px;font-family:system-ui;color:#f87171;font-size:14px'

    const title = document.createElement('strong')
    title.textContent = 'Minefolio 启动失败'

    const msg = document.createElement('span')
    msg.style.cssText = 'display:block;margin-top:8px;color:#94a3b8'
    msg.textContent = String((err as Error)?.message || err)

    box.append(title, document.createElement('br'), msg)
    root.replaceChildren(box)
  }
})
