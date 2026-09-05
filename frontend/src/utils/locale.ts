import { zhCN } from '@/locales/zh-CN'

export type LocaleDictionary = Record<string, any>

export function createTranslator(dict: LocaleDictionary = zhCN) {
  return (key: string): string => {
    const keys = key.split('.')
    let obj: any = dict
    for (const k of keys) obj = obj?.[k]
    return (obj && typeof obj === 'string' ? obj : key) as string
  }
}

export const t = createTranslator(zhCN)
