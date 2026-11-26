/**
 * @file 从本地资产镜像派生持仓报告（离线近似，服务端口径优先）
 */

import { query, rowsFrom } from '@/db/local'
import type { HoldingsReport, HoldingsItem, HoldingsSummary } from '@/api/reports'

export interface LocalHoldingsResult {
  report: HoldingsReport
  approximate: boolean
}

/** 仅投资类资产、当前价值按 quantity*net_value 近似、已实现盈亏置 0 */
export function deriveLocalHoldings(): LocalHoldingsResult {
  const activeLedgerId = Number(localStorage.getItem('minefolio_active_ledger_id') || 0)
  const rows = rowsFrom(query(
    `SELECT a.id, a.name, a.currency, a.quantity, a.net_value, a.cost_basis, c.asset_type
     FROM assets a
     JOIN categories c ON c.id = a.category_id
     WHERE a.__deleted = 0 AND c.__deleted = 0
       AND c.asset_type IN ('stock', 'fund', 'bond', 'crypto')
       AND (a.ledger_id = ? OR a.ledger_id = 0)
     ORDER BY a.id ASC`,
    [activeLedgerId]
  ))

  const holdings: HoldingsItem[] = rows.map((r) => {
    const quantity = Number(r.quantity ?? 0)
    const netValue = Number(r.net_value ?? 0)
    const costBasis = Number(r.cost_basis ?? 0)
    const currentValue = quantity * netValue
    const floatingPnl = currentValue - costBasis
    return {
      asset_id: Number(r.id),
      name: String(r.name ?? ''),
      asset_type: String(r.asset_type ?? ''),
      currency: String(r.currency ?? 'CNY'),
      quantity,
      net_value: netValue,
      cost_basis: costBasis,
      current_value: currentValue,
      floating_pnl: floatingPnl,
      floating_pct: costBasis > 0 ? Math.round((floatingPnl / costBasis) * 1000) / 10 : 0,
      realized_pnl: 0,
    }
  })

  const totalMarketValue = holdings.reduce((s, h) => s + h.current_value, 0)
  const totalCostBasis = holdings.reduce((s, h) => s + h.cost_basis, 0)
  const totalFloatingPnl = holdings.reduce((s, h) => s + h.floating_pnl, 0)
  const summary: HoldingsSummary = {
    total_market_value: totalMarketValue,
    total_cost_basis: totalCostBasis,
    total_floating_pnl: totalFloatingPnl,
    total_realized_pnl: 0,
    floating_pct: totalCostBasis > 0 ? Math.round((totalFloatingPnl / totalCostBasis) * 1000) / 10 : 0,
  }

  return { report: { summary, holdings }, approximate: true }
}
