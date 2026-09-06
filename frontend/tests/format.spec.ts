import { describe, it, expect } from 'vitest'
import { formatCurrency, formatSigned } from '@/utils/format'

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
})
