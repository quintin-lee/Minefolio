// frontend/src/main-mobile.ts
import { createApp } from 'vue'
import { createPinia } from 'pinia'
import ElementPlus from 'element-plus'
import zhCn from 'element-plus/es/locale/lang/zh-cn'
import 'element-plus/dist/index.css'
import './styles/index.css'
import * as ElementPlusIconsVue from '@element-plus/icons-vue'
import App from './App.vue'
import router from './router/mobile'
import i18n from '@/composables/useI18n'
import { initLocalDb } from '@/db/local'
import { useSyncStore } from '@/stores/sync'
import { registerNetworkListeners } from '@/utils/sync-network'

function detectLocale(): 'zh-CN' | 'en-US' {
  try {
    const saved = localStorage.getItem('minefolio_lang')
    if (saved === 'zh-CN' || saved === 'en-US') return saved
  } catch {
    // ignore
  }
  return 'zh-CN'
}

async function bootstrap() {
  await initLocalDb()
  const app = createApp(App)

  for (const [key, component] of Object.entries(ElementPlusIconsVue)) {
    app.component(key, component)
  }

  app.use(createPinia())
  app.use(router)
  app.use(ElementPlus, { locale: zhCn })
  app.use(i18n)
  i18n.global.locale.value = detectLocale()

  const sync = useSyncStore()
  sync.init()
  registerNetworkListeners(() => sync.syncNow())

  app.mount('#app')
}

bootstrap().catch((err) => {
  // Defense-in-depth: never leave a silent blank screen. If local-DB/wasm init
  // or mount fails, render a visible error so the user knows something broke.
  console.error('[minefolio-mobile] bootstrap failed:', err)
  const root = document.getElementById('app')
  if (root) {
    root.innerHTML =
      '<div style="padding:24px;font-family:system-ui;color:#f87171;font-size:14px">' +
      '<strong>Minefolio 启动失败</strong><br/>' +
      '<span style="display:block;margin-top:8px;color:#94a3b8">' +
      String((err as Error)?.message || err) +
      '</span></div>'
  }
})
