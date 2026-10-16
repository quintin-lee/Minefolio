// frontend/tests/fx.spec.ts
import { describe, it, expect } from 'vitest'
import {
  calcCrossRate,
  convertCurrency,
  formatRate,
  calculatePeriodStats,
} from '@/utils/fx'

describe('fx utils', () => {
  const mockRates: Record<string, number> = {
    USD: 7.2345,
    EUR: 7.852,
    HKD: 0.9248,
    JPY: 0.04781,
    CNY: 1.0,
  }

  describe('calcCrossRate', () => {
    it('returns 1.0 when target and base are identical', () => {
      expect(calcCrossRate('USD', 'USD', mockRates)).toBe(1.0)
      expect(calcCrossRate('CNY', 'CNY', mockRates)).toBe(1.0)
    })

    it('returns direct rate when base is CNY', () => {
      expect(calcCrossRate('USD', 'CNY', mockRates)).toBe(7.2345)
      expect(calcCrossRate('EUR', 'CNY', mockRates)).toBe(7.852)
    })

    it('calculates cross-rates between non-CNY currencies correctly', () => {
      // 1 EUR in USD = 7.8520 / 7.2345 ≈ 1.085355
      const eurInUsd = calcCrossRate('EUR', 'USD', mockRates)
      expect(eurInUsd).toBeCloseTo(1.085355, 4)

      // 1 USD in HKD = 7.2345 / 0.9248 ≈ 7.82277
      const usdInHkd = calcCrossRate('USD', 'HKD', mockRates)
      expect(usdInHkd).toBeCloseTo(7.82277, 4)
    })

    it('safely handles missing or zero rates without divide-by-zero crash', () => {
      expect(calcCrossRate('UNKNOWN', 'USD', mockRates)).toBe(1.0)
      expect(calcCrossRate('USD', 'ZERO', { ...mockRates, ZERO: 0 })).toBe(1.0)
    })
  })

  describe('convertCurrency', () => {
    it('converts amount between currencies accurately', () => {
      // 100 USD to CNY = 723.45
      expect(convertCurrency(100, 'USD', 'CNY', mockRates)).toBeCloseTo(723.45, 2)
      // 723.45 CNY to USD = 100
      expect(convertCurrency(723.45, 'CNY', 'USD', mockRates)).toBeCloseTo(100, 2)
    })

    it('returns 0 for non-positive or NaN inputs', () => {
      expect(convertCurrency(0, 'USD', 'CNY', mockRates)).toBe(0)
      expect(convertCurrency(-50, 'USD', 'CNY', mockRates)).toBe(0)
      expect(convertCurrency(NaN, 'USD', 'CNY', mockRates)).toBe(0)
    })
  })

  describe('formatRate', () => {
    it('formats standard currencies with 4 decimal places', () => {
      expect(formatRate(7.23456, 'USD')).toBe('7.2346')
      expect(formatRate(1.085, 'EUR')).toBe('1.0850')
    })

    it('formats low-value currencies like JPY appropriately', () => {
      expect(formatRate(0.04781, 'JPY')).toBe('0.0478')
    })
  })

  describe('calculatePeriodStats', () => {
    it('calculates min, max, avg, and change percentage from historical points', () => {
      const points = [
        { rate_date: '2026-10-01', rate: 7.15, base_currency: 'CNY', target_currency: 'USD' },
        { rate_date: '2026-10-02', rate: 7.25, base_currency: 'CNY', target_currency: 'USD' },
        { rate_date: '2026-10-03', rate: 7.20, base_currency: 'CNY', target_currency: 'USD' },
      ]
      const stats = calculatePeriodStats(points)
      expect(stats.min).toBe(7.15)
      expect(stats.max).toBe(7.25)
      expect(stats.avg).toBeCloseTo(7.20, 2)
      expect(stats.changeRate).toBeCloseTo(((7.20 - 7.15) / 7.15) * 100, 2)
    })

    it('returns zero stats for empty points list', () => {
      const stats = calculatePeriodStats([])
      expect(stats.min).toBe(0)
      expect(stats.max).toBe(0)
      expect(stats.avg).toBe(0)
      expect(stats.changeRate).toBe(0)
    })
  })
})
