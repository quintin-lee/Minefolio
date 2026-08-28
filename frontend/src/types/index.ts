export type CategoryType = 'asset' | 'income' | 'expense' | 'transaction'

export interface Category {
  id: number
  name: string
  parent_id: number | null
  type: CategoryType
  asset_type?: string
  currency: string
  icon?: string
  sort_order: number
  children?: Category[]
}

export interface Asset {
  id: number
  user_id: number
  category_id: number
  name: string
  account_no?: string
  symbol?: string
  quote_source?: string
  last_sync_at?: string
  current_value: number
  currency: string
  note?: string
  created_at: string
  updated_at: string
  category_name?: string
  asset_type?: string
  quantity?: number
  cost_basis?: number
  net_value?: number
}

export type Direction = 'in' | 'out' | 'neutral'

export type TransactionType =
  | 'deposit' | 'withdrawal' | 'buy' | 'sell'
  | 'transfer_in' | 'transfer_out' | 'fee'
  | 'income' | 'loss' | 'interest'

export interface Transaction {
  id: number
  asset_id: number
  linked_asset_id?: number | null
  category_id: number
  transaction_type: TransactionType
  source_type?: 'income' | 'expense'
  direction?: Direction
  linked_direction?: Direction
  amount: number
  price_per_unit?: number
  quantity?: number
  fee?: number
  currency: string
  transaction_date: string
  note?: string
  asset_name?: string
  linked_asset_name?: string
  category_name?: string
}

export type ExpenseType = 'income' | 'expense'

export interface Tag {
  id: number
  user_id: number
  name: string
  color: string
  created_at: string
}

export interface DailyExpense {
  id: number
  user_id: number
  category_id: number
  asset_id: number
  asset_name?: string
  expense_type: ExpenseType
  amount: number
  currency: string
  expense_date: string
  note?: string
  tags?: Tag[]
  category_name?: string
  created_at: string
  updated_at: string
}

export interface Summary {
  total_assets: number
  total_liabilities: number
  net_worth: number
  breakdown: { category_name: string; value: number; pct: number }[]
  trend: { date: string; net_worth: number }[]
}

export interface ExpenseMonthly {
  year: number
  month: number
  total_income: number
  total_expense: number
  balance: number
  by_category: { name: string; type: ExpenseType; amount: number; pct: number }[]
  by_tag: { tag_name: string; amount: number; count: number }[]
  daily_breakdown: { date: string; income: number; expense: number }[]
}

export interface PageResult<T> {
  list: T[]
  total: number
  page: number
  page_size: number
}

export interface TransactionMonthly {
  total_volume: number
  inflows: number
  outflows: number
  count: number
}

export interface ApiResponse<T> {
  code: number
  message: string
  data: T
}

export interface AssetBalanceLog {
  id: number
  asset_id: number
  asset_name?: string
  user_id: number
  delta: number
  balance_after: number
  source_type: string
  source_id: number
  note?: string
  created_at: string
}

export interface WorkflowStepDef {
  step_id: string
  title: string
  description: string
}

export interface WorkflowDef {
  id: string
  title: string
  description: string
  icon: string
  step_count: number
  steps: WorkflowStepDef[]
}

export interface WorkflowStepState {
  step_index: number
  step_id: string
  title: string
  status: 'pending' | 'running' | 'completed' | 'error'
  summary?: string
}

export interface WorkflowRunState {
  workflow_id: string
  title: string
  total_steps: number
  status: 'running' | 'completed' | 'error'
  steps: WorkflowStepState[]
}

export interface MarketSearchItem {
  symbol: string
  name: string
  source: string
  market_desc: string
  current_price: number
  currency: string
}

export interface MarketQuote {
  symbol: string
  name: string
  source: string
  current_price: number
  change_percent: number
  currency: string
  quote_time: string
}

export interface PriceHistoryItem {
  id: number
  asset_id: number
  price_date: string
  price: number
  currency: string
  created_at: string
}

export interface MarketSettings {
  market_proxy: string
  market_auto_sync: boolean
  market_sync_interval_min: number
}

export interface TestProxyResult {
  success: boolean
  message: string
  latency_ms: number
}

