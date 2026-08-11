// frontend/src/utils/format.ts
// Shared formatting helpers (spec §6.0)

const cnyFormatter = new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' })

/** Format a number as CNY currency, e.g. 120.5 → ¥120.50 */
export function formatCurrency(val: number): string {
  return cnyFormatter.format(val ?? 0)
}

/** Format an ISO date string (YYYY-MM-DD) as YYYY年M月D日 */
export function formatDate(val: string): string {
  if (!val) return ''
  const [y, m, d] = val.slice(0, 10).split('-')
  return `${y}年${Number(m)}月${Number(d)}日`
}
