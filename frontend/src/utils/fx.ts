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

  const first = points[0]
  const last = points[points.length - 1]
  if (!first || !last) {
    return { min: 0, max: 0, avg: 0, changeRate: 0 }
  }

  let min = first.rate
  let max = first.rate
  let sum = 0

  for (const pt of points) {
    if (pt.rate < min) min = pt.rate
    if (pt.rate > max) max = pt.rate
    sum += pt.rate
  }

  const avg = sum / points.length
  const firstRate = first.rate
  const lastRate = last.rate
  const changeRate = firstRate > 0 ? ((lastRate - firstRate) / firstRate) * 100 : 0

  return { min, max, avg, changeRate }
}

export interface FxYAxisBounds {
  min: number
  max: number
}

/**
 * 计算外汇走势图紧凑 Y 轴上下界，避免常量或窄幅波动汇率被 ECharts 拉成 3–10 宽区间
 */
export function computeFxYAxisBounds(values: number[]): FxYAxisBounds | null {
  const valid = values.filter((v) => typeof v === 'number' && Number.isFinite(v) && v > 0)
  if (valid.length === 0) return null

  const minVal = Math.min(...valid)
  const maxVal = Math.max(...valid)
  const span = maxVal - minVal
  const decimals = maxVal >= 100 ? 2 : 4

  const pad =
    span <= Math.max(Math.abs(minVal) * 0.0005, 1e-6)
      ? Math.max(Math.abs(minVal) * 0.005, decimals === 2 ? 0.1 : 0.002)
      : Math.max(span * 0.15, Math.abs(minVal) * 0.0005)

  return {
    min: Number(Math.max(0, minVal - pad).toFixed(decimals)),
    max: Number((maxVal + pad).toFixed(decimals)),
  }
}
