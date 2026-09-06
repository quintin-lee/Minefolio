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

/** 创建/更新交易流水时的请求载荷 (POST/PUT /api/transactions) */
export interface TransactionInput {
  /** 主资产账户 ID */
  asset_id: number
  /** 关联出资/目标资产 ID (转账、买入出资等) */
  linked_asset_id?: number | null
  /** 交易分类 ID */
  category_id?: number | null
  /** 交易类型 */
  transaction_type: TransactionType
  /** 交易总金额 */
  amount: number
  /** 成交单价 (投资类型交易) */
  price_per_unit?: number
  /** 成交数量/份额 (投资类型交易) */
  quantity?: number
  /** 手续费金额 (大于 0 时服务端自动写入子交易) */
  fee?: number
  /** 币种 (如 'CNY', 'USD')，缺省由服务端按资产币种处理 */
  currency?: string
  /** 交易发生时间 (YYYY-MM-DD) */
  transaction_date: string
  /** 备注说明 */
  note?: string
  /** 收支方向 (创建时可选，缺省 'expense') */
  source_type?: 'income' | 'expense'
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

export interface WorkflowConfigState {
  workflow_id: string
  title: string
  icon: string
  description: string
  initialParams?: Record<string, unknown>
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
  market_sync_mode?: 'trading_hours' | 'interval' | 'manual'
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

/** 单币种资产池聚合数据 */
export interface CurrencyBucket {
  /** 币种代码 (如 "USD", "CNY", "HKD") */
  currency: string
  /** 该币种下的资产及负债账户数 */
  asset_count: number
  /** 原币计价的资产总额 */
  original_assets: number
  /** 原币计价的负债总额 */
  original_liabilities: number
  /** 原币计价的净资产总额 (original_assets - original_liabilities) */
  original_net_worth: number
  /** 该币种折合基准币种的汇率 */
  rate_to_base: number
  /** 折合为基准币种后的净资产额 */
  converted_net_worth: number
  /** 占全盘折算净资产的百分比 (0-100) */
  percentage: number
}

/** 多币种汇总与基准币种统一折算报表模型 */
export interface MultiCurrencySummary {
  /** 当前报表计算采用的基准币种 (默认 "CNY") */
  base_currency: string
  /** 全盘统一折算后的净资产总额 (CNY) */
  total_net_worth: number
  /** 全盘统一折算后的总资产规模 (CNY) */
  total_assets: number
  /** 全盘统一折算后的总负债规模 (CNY) */
  total_liabilities: number
  /** 各币种分桶统计明细列表 */
  currencies: CurrencyBucket[]
}

/** OAuth2 / OIDC 第三方单点登录服务商信息 */
export interface OAuthProvider {
  /** 服务商标识 (如 "github", "oidc") */
  id: string
  /** 显示名称 (如 "GitHub", "企业统一认证") */
  name: string
  /** 图标名称 (Iconify 格式) */
  icon?: string
  /** 触发授权跳转的前端入口或 API 授权地址 */
  auth_url: string
}

/** 外汇汇率历史每日走势快照点 */
export interface FxHistoryPoint {
  /** 快照日期 (YYYY-MM-DD) */
  rate_date: string
  /** 对基准币种汇率数值 */
  rate: number
  /** 基准币种 (如 "CNY") */
  base_currency: string
  /** 目标外币 (如 "USD") */
  target_currency: string
}

/** 外币资产「价格涨跌 vs 汇率损益」双因子拆解明细条目 */
export interface FxPnlAssetItem {
  /** 资产 ID */
  asset_id: number
  /** 资产名称 (如 "苹果股票", "标普500 ETF") */
  asset_name: string
  /** 计价币种 (如 "USD", "HKD") */
  currency: string
  /** 所属分类名称 */
  category_name: string
  /** 原币持仓成本 */
  cost_basis_orig: number
  /** 原币当前市值 */
  current_value_orig: number
  /** 当前对基准币的实时汇率 */
  current_fx_rate: number
  /** 买入建仓时的历史加权汇率 */
  cost_fx_rate: number
  /** 资产标的自身价格盈亏 (折合基准币 CNY): (current_value - cost_basis) * current_fx_rate */
  asset_pnl_base: number
  /** 纯外汇汇率波动汇兑损益 (折合基准币 CNY): cost_basis * (current_fx_rate - cost_fx_rate) */
  fx_pnl_base: number
  /** 综合总回报 (折合基准币 CNY): asset_pnl_base + fx_pnl_base */
  combined_pnl_base: number
  /** 汇率回报率百分比: ((current_fx_rate - cost_fx_rate) / cost_fx_rate) * 100 */
  fx_return_rate: number
}

/** 外汇与汇兑损益综合分析报表模型 */
export interface FxPnlReport {
  /** 基准币种 (默认 "CNY") */
  base_currency: string
  /** 境外外币资产按买入成本折算基准币总额 */
  total_foreign_cost_base: number
  /** 境外外币资产按当前市值折算基准币总额 */
  total_foreign_market_base: number
  /** 所有外币资产的标的价格总盈亏 (CNY) */
  total_asset_pnl_base: number
  /** 所有外币资产的纯汇率变动总汇兑损益 (CNY) */
  total_fx_pnl_base: number
  /** 境外资产综合总回报 (CNY) */
  total_combined_pnl_base: number
  /** 外币资产明细列表 */
  assets: FxPnlAssetItem[]
}
