import { vi } from 'vitest'
import { formatCurrency, formatSigned, formatDate, localToday, localThisMonth } from '@/utils/format'
import i18n from '@/composables/useI18n'

describe('format currency helpers', () => {
  it('formats CNY by default', () => {
    expect(formatCurrency(120.5)).toBe('¥120.50')
    expect(formatCurrency(0)).toBe('¥0.00')
    expect(formatCurrency(undefined as unknown as number)).toBe('¥0.00')
  })

  it('formats a provided ISO currency code', () => {
    expect(formatCurrency(120.5, 'USD')).toBe('US$120.50')
    expect(formatCurrency(120.5, 'usd')).toBe(formatCurrency(120.5, 'USD'))
  })

  it('handles non-ISO and blank currency codes gracefully', () => {
    // 非 ISO 4217 币种：Intl 渲染为"代码 + 数值"，与兜底文本等价 (仅空白字符差异)
    expect(formatCurrency(123.4, 'BTC').replace(/\u00a0/g, ' ')).toBe('BTC 123.40')
    // 空币种代码回退默认币种
    expect(formatCurrency(120.5, '')).toBe('¥120.50')
  })

  it('formats signed amounts with the sign prefix', () => {
    expect(formatSigned(50)).toBe('+¥50.00')
    expect(formatSigned(-50)).toBe('-¥50.00')
    expect(formatSigned(0)).toBe('¥0.00')
    expect(formatSigned(50, 'USD')).toBe(`+${formatCurrency(50, 'USD')}`)
  })

  it('formats dates respecting active locale', () => {
    i18n.global.locale.value = 'zh-CN'
    expect(formatDate('2026-08-15')).toBe('2026年8月15日')
    expect(formatDate('')).toBe('')

    i18n.global.locale.value = 'en-US'
    expect(formatDate('2026-08-15')).toBe('2026-08-15')

    // Reset back to zh-CN
    i18n.global.locale.value = 'zh-CN'
  })
})

describe('local date helpers', () => {
  afterEach(() => {
    vi.useRealTimers()
  })

  it('returns today in YYYY-MM-DD built from local date parts', () => {
    const d = new Date()
    const expected = `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`
    expect(localToday()).toBe(expected)
    expect(localToday()).toMatch(/^\d{4}-\d{2}-\d{2}$/)
  })

  it('returns the current month in YYYY-MM built from local date parts', () => {
    const d = new Date()
    const expected = `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}`
    expect(localThisMonth()).toBe(expected)
    expect(localThisMonth()).toMatch(/^\d{4}-\d{2}$/)
  })

  it('stays on the local date near midnight instead of drifting to the UTC date', () => {
    // 本地 09-07 00:30（UTC+ 时区下 toISOString() 会给出 09-06，旧实现即在此出错）
    vi.useFakeTimers()
    vi.setSystemTime(new Date(2026, 8, 7, 0, 30))
    expect(localToday()).toBe('2026-09-07')
    expect(localThisMonth()).toBe('2026-09')
  })
})
