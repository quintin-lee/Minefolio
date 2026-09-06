/**
 * @file 数据展示格式化工具模块
 * @description 提供金额货币符号格式化 (¥)、带符号盈亏格式化 (±¥)、日期本地化转换等公共工具函数
 */

// frontend/src/utils/format.ts

/** 默认币种 (与历史行为保持一致，用于未显式传币种的调用方) */
const DEFAULT_CURRENCY = 'CNY'

/** 货币格式化器缓存 (键: 大写 ISO 币种代码) */
const formatterCache = new Map<string, Intl.NumberFormat>()

/**
 * 获取缓存化的货币格式化器
 * @param currency ISO 4217 币种代码 (如 'CNY', 'USD')
 * @returns 货币格式化器 (币种非法时抛出 RangeError，由调用方兜底)
 */
function currencyFormatter(currency: string): Intl.NumberFormat {
  const code = (currency || DEFAULT_CURRENCY).toUpperCase()
  let formatter = formatterCache.get(code)
  if (!formatter) {
    formatter = new Intl.NumberFormat('zh-CN', { style: 'currency', currency: code })
    formatterCache.set(code, formatter)
  }
  return formatter
}

/**
 * 非 ISO 4217 币种或非法代码的兜底文本 (如 "BTC 123.45")
 * @param val 金额数值
 * @param currency 币种代码
 * @returns 保留两位小数的"代码 + 数值"文本
 */
function formatPlainCurrency(val: number, currency: string): string {
  const code = (currency || DEFAULT_CURRENCY).toUpperCase()
  return `${code} ${val.toLocaleString('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 })}`
}

/**
 * 格式化数值为指定币种货币字符串
 * @param val 金额数值 (如 120.5)
 * @param currency ISO 4217 币种代码 (如 'CNY', 'USD')，默认 'CNY'
 * @returns 格式化后的货币文本 (如 "¥120.50"、"$120.50"；非法币种代码回退为 "BTC 120.50" 形式)
 * @example
 * formatCurrency(120.5)     // "¥120.50"
 * formatCurrency(120.5, 'USD') // "$120.50"
 */
export function formatCurrency(val: number, currency = DEFAULT_CURRENCY): string {
  const value = val ?? 0
  const code = (currency || DEFAULT_CURRENCY).toUpperCase()
  try {
    return currencyFormatter(code).format(value)
  } catch {
    return formatPlainCurrency(value, code)
  }
}

/**
 * 格式化带正负符号的指定币种货币字符串 (正数加 +，负数加 -)
 * @param val 盈亏金额数值 (如 50 或 -50)
 * @param currency ISO 4217 币种代码 (如 'CNY', 'USD')，默认 'CNY'
 * @returns 带正负前缀的货币文本 (如 "+¥50.00" 或 "-$50.00")
 * @example
 * formatSigned(50)  // "+¥50.00"
 * formatSigned(-50) // "-¥50.00"
 */
export function formatSigned(val: number, currency = DEFAULT_CURRENCY): string {
  const value = val ?? 0
  const s = formatCurrency(Math.abs(value), currency)
  if (value > 0) return `+${s}`
  if (value < 0) return `-${s}`
  return s
}

/**
 * 将 ISO 日期字符串 (YYYY-MM-DD) 格式化为中文年月日格式
 * @param val ISO 日期字符串 (如 "2026-08-15")
 * @returns 中文格式日期文本 (如 "2026年8月15日")
 * @example
 * formatDate("2026-08-05") // "2026年8月5日"
 */
export function formatDate(val: string): string {
  if (!val) return ''
  const [y, m, d] = val.slice(0, 10).split('-')
  return `${y}年${Number(m)}月${Number(d)}日`
}

