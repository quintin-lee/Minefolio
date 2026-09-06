import { zhCN } from '@/locales/zh-CN'
import { enUS } from '@/locales/en-US'

export type LocaleDictionary = Record<string, unknown>

const dictionaries: Record<string, LocaleDictionary> = {
  'zh-CN': zhCN as LocaleDictionary,
  'en-US': enUS as LocaleDictionary,
}

export function createTranslator(dict: LocaleDictionary = zhCN as LocaleDictionary) {
  return function translate(key: string): string {
    const keys = key.split('.')
    let current: unknown = dict
    for (const k of keys) {
      if (typeof current === 'object' && current !== null && k in current) {
        current = (current as Record<string, unknown>)[k]
      } else {
        return key
      }
    }
    return typeof current === 'string' ? current : key
  }
}

export function getCurrentLocale(): string {
  try {
    const saved = localStorage.getItem('minefolio_lang')
    if (saved === 'zh-CN' || saved === 'en-US') {
      return saved
    }
  } catch {
    // ignore
  }
  return 'zh-CN'
}

export const t = ((key: string): string => {
  const locale = getCurrentLocale()
  const dict = dictionaries[locale] || (zhCN as LocaleDictionary)
  const keys = key.split('.')
  let current: unknown = dict
  for (const k of keys) {
    if (typeof current === 'object' && current !== null && k in current) {
      current = (current as Record<string, unknown>)[k]
    } else {
      return key
    }
  }
  return typeof current === 'string' ? current : key
}) satisfies ((key: string) => string)
