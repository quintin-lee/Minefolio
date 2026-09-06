import { createApp } from 'vue'
import { createPinia } from 'pinia'
import ElementPlus from 'element-plus'
import zhCn from 'element-plus/es/locale/lang/zh-cn'
import 'element-plus/dist/index.css'
import './styles/index.css'
import * as ElementPlusIconsVue from '@element-plus/icons-vue'
import App from './App.vue'
import router from './router'
import i18n from '@/composables/useI18n'

const app = createApp(App)

for (const [key, component] of Object.entries(ElementPlusIconsVue)) {
  app.component(key, component)
}

app.use(createPinia())
app.use(router)
app.use(ElementPlus, { locale: zhCn })
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
