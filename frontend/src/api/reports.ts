// frontend/src/api/reports.ts
import http from '@/utils/http'

export interface ExpenseMonthlyReport {
  year: number
  month: number
  total_income: number
  total_expense: number
  balance: number
  by_category: { name: string; type: string; amount: number; pct: number }[]
  by_tag: { tag_name: string; amount: number; count: number }[]
  daily_breakdown: { date: string; income: number; expense: number }[]
}

export interface ExpenseTrend {
  labels: string[]
  income: number[]
  expense: number[]
}

export interface ExpenseYearlyReport {
  year: number
  labels: string[]
  income: number[]
  expense: number[]
}

export interface ExpenseCategoryBreakdown {
  period: string
  items: { name: string; amount: number; pct: number }[]
}

export interface ExpenseTagBreakdown {
  period: string
  items: { tag_name: string; amount: number; count: number; pct: number }[]
}

export interface AssetTrend {
  period: string
  labels: string[]
  net_worth: number[]
  assets: number[]
  liabilities: number[]
}

export interface AssetBreakdown {
  assets: { name: string; value: number; pct: number }[]
  liabilities: { name: string; value: number; pct: number }[]
  total_assets: number
  total_liabilities: number
  net_worth: number
}

export interface TransactionPerformance {
  total_trades: number
  total_gain: number
  total_loss: number
  net_gain: number
  total_cost_basis_remaining?: number
  total_market_value?: number
  floating_pnl?: number
  realized_pnl?: number
  trades: {
    id: number
    asset_name: string
    type: string
    date: string
    quantity: number
    price: number
    amount: number
    profit?: number
    avg_cost_at_trade?: number
    realized?: number
    fee?: number
  }[]
}

export interface AssetSummary {
  current_value: number
  total_assets: number
  total_liabilities: number
  change_30d: number
  change_30d_pct: number
  by_category: { name: string; value: number; is_liability: boolean }[]
}

export interface HoldingsItem {
  asset_id: number
  name: string
  asset_type: string
  currency: string
  quantity: number
  net_value: number
  cost_basis: number
  current_value: number
  floating_pnl: number
  floating_pct: number
  realized_pnl: number
}

export interface HoldingsSummary {
  total_market_value: number
  total_cost_basis: number
  total_floating_pnl: number
  total_realized_pnl: number
  floating_pct: number
}

export interface HoldingsReport {
  summary: HoldingsSummary
  holdings: HoldingsItem[]
}

export const reportsApi = {
  expenseMonthly: (year: number, month: number) =>
    http.get<ExpenseMonthlyReport, ExpenseMonthlyReport>('/reports/expense/monthly', {
      params: { year, month },
    }),
  expenseTrend: (months = 6) =>
    http.get<ExpenseTrend, ExpenseTrend>('/reports/expense/trend', {
      params: { months },
    }),
  expenseYearly: (year?: number) =>
    http.get<ExpenseYearlyReport, ExpenseYearlyReport>('/reports/expense/yearly', {
      params: { year },
    }),
  expenseCategory: (year?: number, month?: number) =>
    http.get<ExpenseCategoryBreakdown, ExpenseCategoryBreakdown>('/reports/expense/category', {
      params: { year, month },
    }),
  expenseTag: (year?: number, month?: number) =>
    http.get<ExpenseTagBreakdown, ExpenseTagBreakdown>('/reports/expense/tag', {
      params: { year, month },
    }),
  assetTrend: (period = '30d') =>
    http.get<AssetTrend, AssetTrend>('/reports/asset/trend', {
      params: { period },
    }),
  assetBreakdown: () => http.get<AssetBreakdown, AssetBreakdown>('/reports/asset/breakdown'),
  transactionPerformance: () =>
    http.get<TransactionPerformance, TransactionPerformance>('/reports/transaction/performance'),
  assetSummary: () => http.get<AssetSummary, AssetSummary>('/reports/asset/summary'),
  holdings: () => http.get<HoldingsReport, HoldingsReport>('/reports/holdings'),
  fxPnl: (baseCurrency = 'CNY') =>
    http.get<import('@/types').FxPnlReport, import('@/types').FxPnlReport>('/reports/fx-pnl', {
      params: { base_currency: baseCurrency },
    }),
}
