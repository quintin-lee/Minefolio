import { createI18n } from 'vue-i18n'
import { zhCN } from '@/locales/zh-CN'
import { enUS } from '@/locales/en-US'

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

  return {
    t,
    locale,
  }
}

export const useAppLocale = useI18n

export default i18n
