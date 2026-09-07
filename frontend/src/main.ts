import { createApp } from 'vue'
import { createPinia } from 'pinia'
import 'element-plus/dist/index.css'
import './styles/index.css'
import { registerIcons } from '@/icons'
import App from './App.vue'
import router from './router'
import i18n from '@/composables/useI18n'
import { setAuthErrorHandler } from '@/utils/http'

setAuthErrorHandler(() => {
  if (router.currentRoute.value?.path !== '/login') {
    router.push('/login')
  }
})

const app = createApp(App)

app.config.errorHandler = (err, instance, info) => {
  console.error('[GlobalErrorHandler]', err, info)
}

registerIcons(app)

app.use(createPinia())
app.use(router)
app.use(i18n)

function initAppLocale() {
  try {
    const saved = localStorage.getItem('minefolio_lang')
    if (saved === 'zh-CN' || saved === 'en-US') {
      i18n.global.locale.value = saved
    }
  } catch {
    // ignore
  }
}

initAppLocale()
app.mount('#app')
