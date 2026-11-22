export type CostInputMode = 'total' | 'unit'

export function resolveTotalCostBasis(params: {
  mode: CostInputMode
  quantity: number
  netValue: number
  inputValue: number | null | undefined
}): number {
  const qty = Number(params.quantity) || 0
  const nv = Number(params.netValue) || 0
  const raw = params.inputValue
  const hasVal = raw !== null && raw !== undefined && Number.isFinite(Number(raw)) && Number(raw) > 0

  if (params.mode === 'unit') {
    const unitCost = hasVal ? Number(raw) : nv
    if (qty > 0 && unitCost > 0) {
      return Number((qty * unitCost).toFixed(2))
    }
    return 0
  }

  if (!hasVal) {
    if (qty > 0 && nv > 0) {
      return Number((qty * nv).toFixed(2))
    }
    return 0
  }
  return Number(Math.max(0, Number(raw)).toFixed(2))
}

export function detectSuspiciousUnitCostInput(params: {
  mode: CostInputMode
  quantity: number
  netValue: number
  totalCost: number | null | undefined
}): { suspicious: boolean; suggestedTotalCost: number; marketValue: number } {
  const qty = Number(params.quantity) || 0
  const nv = Number(params.netValue) || 0
  const cost = Number(params.totalCost) || 0
  const marketValue = qty > 0 && nv > 0 ? Number((qty * nv).toFixed(2)) : 0

  if (params.mode !== 'total' || qty < 10 || nv <= 0 || cost <= 0 || marketValue <= 0) {
    return { suspicious: false, suggestedTotalCost: 0, marketValue }
  }

  if (cost <= nv * 2 && marketValue / cost >= 10) {
    return {
      suspicious: true,
      suggestedTotalCost: Number((qty * cost).toFixed(2)),
      marketValue,
    }
  }

  return { suspicious: false, suggestedTotalCost: 0, marketValue }
}
