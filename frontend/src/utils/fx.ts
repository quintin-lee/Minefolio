// frontend/src/utils/fx.ts
import type { FxHistoryPoint } from '@/types'

/**
 * 计算目标币种兑本位币的交叉汇率: 1 TargetCurrency = ? BaseCurrency
 * 公式: Rate(Target / Base) = Rate(Target / CNY) / Rate(Base / CNY)
 */
export function calcCrossRate(
  targetCurrency: string,
  baseCurrency: string,
  rates: Record<string, number>
): number {
  if (targetCurrency === baseCurrency) {
    return 1.0
  }

  const targetRate = targetCurrency === 'CNY' ? 1.0 : rates[targetCurrency]
  const baseRate = baseCurrency === 'CNY' ? 1.0 : rates[baseCurrency]

  if (!targetRate || !baseRate || baseRate <= 0 || targetRate <= 0) {
    return 1.0
  }

  return targetRate / baseRate
}

/**
 * 按实时汇率对金额进行币种兑换折算
 */
export function convertCurrency(
  amount: number,
  fromCurrency: string,
  toCurrency: string,
  rates: Record<string, number>
): number {
  if (!amount || amount <= 0 || isNaN(amount)) {
    return 0
  }
  if (fromCurrency === toCurrency) {
    return amount
  }

  const rate = calcCrossRate(fromCurrency, toCurrency, rates)
  return amount * rate
}

/**
 * 格式化汇率展示数字
 */
export function formatRate(rate: number, _currency?: string): string {
  if (isNaN(rate) || rate === 0) {
    return '0.0000'
  }
  if (rate >= 100) {
    return rate.toFixed(2)
  }
  return rate.toFixed(4)
}

export interface PeriodStats {
  min: number
  max: number
  avg: number
  changeRate: number
}

/**
 * 计算历史汇率点位区间的最高、最低、均值与涨跌幅度
 */
export function calculatePeriodStats(points: FxHistoryPoint[]): PeriodStats {
  if (!points || points.length === 0) {
    return { min: 0, max: 0, avg: 0, changeRate: 0 }
  }

  let min = points[0].rate
  let max = points[0].rate
  let sum = 0

  for (const pt of points) {
    if (pt.rate < min) min = pt.rate
    if (pt.rate > max) max = pt.rate
    sum += pt.rate
  }

  const avg = sum / points.length
  const firstRate = points[0].rate
  const lastRate = points[points.length - 1].rate
  const changeRate = firstRate > 0 ? ((lastRate - firstRate) / firstRate) * 100 : 0

  return { min, max, avg, changeRate }
}
