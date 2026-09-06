import { createI18n } from 'vue-i18n'
import { zhCN } from '@/locales/zh-CN'
import { enUS } from '@/locales/en-US'

export type SupportedLocale = 'zh-CN' | 'en-US'

const i18n = createI18n({
  legacy: false,
  locale: 'zh-CN',
  fallbackLocale: 'en-US',
  messages: {
    'zh-CN': zhCN,
    'en-US': enUS,
  },
})

export const useI18n = () => {
  const { t, locale } = i18n.global

  function setLocale(lang: SupportedLocale) {
    locale.value = lang
    try {
      localStorage.setItem('minefolio_lang', lang)
    } catch {
      // ignore
    }
  }

  function initLocale() {
    try {
      const saved = localStorage.getItem('minefolio_lang') as SupportedLocale | null
      if (saved === 'zh-CN' || saved === 'en-US') {
        locale.value = saved
      }
    } catch {
      // ignore
    }
  }

  return {
    t,
    locale,
    setLocale,
    initLocale,
  }
}

export default i18n
