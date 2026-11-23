import { describe, expect, it } from 'vitest'
import { detectSuspiciousUnitCostInput, resolveTotalCostBasis } from '@/utils/investment-cost'

describe('investment cost basis helpers', () => {
  it('computes total cost from unit cost mode cleanly', () => {
    expect(
      resolveTotalCostBasis({
        mode: 'unit',
        quantity: 500000,
        netValue: 1.2,
        inputValue: 1.2,
      }),
    ).toBe(600000)

    expect(
      resolveTotalCostBasis({
        mode: 'unit',
        quantity: 500000,
        netValue: 1.2,
        inputValue: undefined,
      }),
    ).toBe(600000)
  })

  it('defaults empty total cost to quantity * netValue', () => {
    expect(
      resolveTotalCostBasis({
        mode: 'total',
        quantity: 500000,
        netValue: 1.2,
        inputValue: null,
      }),
    ).toBe(600000)
  })

  it('detects when a unit price like 1.2 is mistakenly entered as total cost for 500000 shares', () => {
    const res = detectSuspiciousUnitCostInput({
      mode: 'total',
      quantity: 500000,
      netValue: 1.2,
      totalCost: 1.2,
    })
    expect(res.suspicious).toBe(true)
    expect(res.marketValue).toBe(600000)
    expect(res.suggestedTotalCost).toBe(600000)
  })

  it('does not flag normal total cost matching position scale', () => {
    const res = detectSuspiciousUnitCostInput({
      mode: 'total',
      quantity: 500000,
      netValue: 1.2,
      totalCost: 550000,
    })
    expect(res.suspicious).toBe(false)
  })
})
