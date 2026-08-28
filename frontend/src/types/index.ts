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

/* DCA Plans */
export interface DcaPlan {
  id: number
  user_id: number
  target_asset_id: number
  funding_asset_id: number
  name: string
  frequency: 'weekly' | 'biweekly' | 'monthly'
  day_of_period: number
  amount: number
  target_profit_rate: number
  target_total_amount: number
  target_total_periods: number
  status: 'active' | 'paused' | 'completed'
  note?: string
  created_at: string
  updated_at: string
  target_asset_name?: string
  target_symbol?: string
  target_net_value?: number
  target_quantity?: number
  target_cost_basis?: number
  target_current_value?: number
  target_quote_source?: string
  target_currency?: string
  funding_asset_name?: string
  funding_currency?: string
  executed_periods?: number
  total_invested_amount?: number
  profit_rate?: number
  profit_target_reached?: boolean
}

export interface DcaExecution {
  id: number
  plan_id: number
  user_id: number
  period_date: string
  planned_amount: number
  actual_amount: number
  executed_price: number
  executed_quantity: number
  transaction_id?: number
  status: 'pending' | 'confirmed' | 'skipped'
  created_at: string
  updated_at?: string
  plan_name?: string
  target_asset_id?: number
  funding_asset_id?: number
  target_profit_rate?: number
  target_asset_name?: string
  target_symbol?: string
  target_net_value?: number
  target_currency?: string
  funding_asset_name?: string
  funding_currency?: string
}

/* Cashflow Schedules & Calendar */
export interface CashflowSchedule {
  id: number
  user_id: number
  source_asset_id: number
  target_asset_id: number
  name: string
  flow_type: 'dividend' | 'interest' | 'rent' | 'maturity' | string
  frequency: 'once' | 'monthly' | 'quarterly' | 'semi_annual' | 'annual'
  start_date: string
  end_date?: string
  expected_amount: number
  status: 'active' | 'completed' | 'cancelled'
  note?: string
  created_at: string
  updated_at?: string
  source_asset_name?: string
  source_symbol?: string
  target_asset_name?: string
  target_currency?: string
}

export interface CashflowCalendarEvent {
  id?: number
  schedule_id?: number
  source_asset_id?: number
  target_asset_id?: number
  date: string
  name: string
  source_asset_name?: string
  target_asset_name?: string
  flow_type: string
  amount: number
  currency: string
  is_actual: boolean
  status: 'confirmed' | 'projected'
}

export interface MonthlyCashflowSummary {
  year: number
  month: number
  year_month: string
  actual_total: number
  projected_total: number
  annual_projected_total: number
  events: CashflowCalendarEvent[]
}

/* Multi-Ledger Spaces */
export interface Ledger {
  id: number
  owner_id: number
  name: string
  description?: string
  currency: string
  icon?: string
  color?: string
  is_default: boolean | number
  invite_code?: string
  invite_expires_at?: string
  created_at: string
  updated_at?: string
  my_role: 'owner' | 'editor' | 'viewer'
  owner_username?: string
  member_count?: number
  total_assets?: number
}

export interface LedgerMember {
  id: number
  ledger_id: number
  user_id: number
  username: string
  role: 'owner' | 'editor' | 'viewer'
  joined_at: string
}

export interface LedgerInviteResult {
  invite_code: string
  expires_at: string
}



