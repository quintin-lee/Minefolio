/**
 * @file 数据展示格式化工具模块
 * @description 提供金额货币符号格式化 (¥)、带符号盈亏格式化 (±¥)、日期本地化转换等公共工具函数
 */

// frontend/src/utils/format.ts

/** 人民币数字格式化器 (保留两位小数，千分位逗号) */
const cnyFormatter = new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' })

/**
 * 格式化数值为人民币货币字符串
 * @param val 金额数值 (如 120.5)
 * @returns 格式化后的货币文本 (如 "¥120.50")
 * @example
 * formatCurrency(120.5) // "¥120.50"
 */
export function formatCurrency(val: number): string {
  return cnyFormatter.format(val ?? 0)
}

/**
 * 格式化带正负符号的人民币货币字符串 (正数加 +，负数加 -)
 * @param val 盈亏金额数值 (如 50 或 -50)
 * @returns 带正负前缀的货币文本 (如 "+¥50.00" 或 "-¥50.00")
 * @example
 * formatSigned(50)  // "+¥50.00"
 * formatSigned(-50) // "-¥50.00"
 */
export function formatSigned(val: number): string {
  const abs = Math.abs(val ?? 0)
  const s = cnyFormatter.format(abs)
  if (val > 0) return `+${s}`
  if (val < 0) return `-${s}`
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

