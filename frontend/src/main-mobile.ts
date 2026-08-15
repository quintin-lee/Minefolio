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
import { initLocalDb } from '@/db/local'
import { useSyncStore } from '@/stores/sync'
import { registerNetworkListeners } from '@/utils/sync-network'

async function bootstrap() {
  await initLocalDb()
  const app = createApp(App)

  for (const [key, component] of Object.entries(ElementPlusIconsVue)) {
    app.component(key, component)
  }

  app.use(createPinia())
  app.use(router)
  app.use(ElementPlus, { locale: zhCn })

  const sync = useSyncStore()
  sync.init()
  registerNetworkListeners(() => sync.syncNow())

  app.mount('#app')
}

bootstrap()
