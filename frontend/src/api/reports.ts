/**
 * @file 财务报表与统计分析 API 接口模块
 * @description 提供月度/年度收支报表、趋势分析、资产分布与净值走势、投资持仓与盈亏 (PnL)、汇率损益等统计报表接口
 */

// frontend/src/api/reports.ts
import http from '@/utils/http'

/**
 * 月度收支综合报表数据
 */
export interface ExpenseMonthlyReport {
  /** 统计年份 */
  year: number
  /** 统计月份 (1-12) */
  month: number
  /** 月度总收入金额 */
  total_income: number
  /** 月度总支出金额 */
  total_expense: number
  /** 月度结余金额 (总收入 - 总支出) */
  balance: number
  /** 按分类聚合统计项列表 */
  by_category: { name: string; type: string; amount: number; pct: number }[]
  /** 按标签聚合统计项列表 */
  by_tag: { tag_name: string; amount: number; count: number }[]
  /** 每日收支明细分解 */
  daily_breakdown: { date: string; income: number; expense: number }[]
}

/**
 * 收支趋势折线图数据
 */
export interface ExpenseTrend {
  /** 横轴时间标签数组 (如 ['2026-04', '2026-05', ...]) */
  labels: string[]
  /** 各月份收入金额序列 */
  income: number[]
  /** 各月份支出金额序列 */
  expense: number[]
}

/**
 * 年度收支对比报表数据
 */
export interface ExpenseYearlyReport {
  /** 统计年份 */
  year: number
  /** 月份标签列表 (1月 - 12月) */
  labels: string[]
  /** 每月收入金额序列 */
  income: number[]
  /** 每月支出金额序列 */
  expense: number[]
}

/**
 * 支出分类占比分布数据
 */
export interface ExpenseCategoryBreakdown {
  /** 统计周期说明 (如 '2026-08' 或 '2026') */
  period: string
  /** 各分类占比明细项 */
  items: { name: string; amount: number; pct: number }[]
}

/**
 * 支出标签占比分布数据
 */
export interface ExpenseTagBreakdown {
  /** 统计周期说明 */
  period: string
  /** 各标签占比明细项 */
  items: { tag_name: string; amount: number; count: number; pct: number }[]
}

/**
 * 资产净值走势趋势数据
 */
export interface AssetTrend {
  /** 统计周期标识 (如 '30d', '90d', '1y') */
  period: string
  /** 时间轴点位标签 */
  labels: string[]
  /** 净资产数值序列 (资产总额 - 负债总额) */
  net_worth: number[]
  /** 资产总额数值序列 */
  assets: number[]
  /** 负债总额数值序列 */
  liabilities: number[]
}

/**
 * 资产与负债分类构成占比明细
 */
export interface AssetBreakdown {
  /** 资产类目分布列表 */
  assets: { name: string; value: number; pct: number }[]
  /** 负债类目分布列表 */
  liabilities: { name: string; value: number; pct: number }[]
  /** 资产总值 */
  total_assets: number
  /** 负债总值 */
  total_liabilities: number
  /** 净资产总额 */
  net_worth: number
}

/**
 * 投资交易表现与已实现盈亏表现
 */
export interface TransactionPerformance {
  /** 累计交易次数 */
  total_trades: number
  /** 盈利交易总金额 */
  total_gain: number
  /** 亏损交易总金额 */
  total_loss: number
  /** 净收益金额 (gain - loss) */
  net_gain: number
  /** 剩余持仓成本基准 */
  total_cost_basis_remaining?: number
  /** 当前持仓总市值 */
  total_market_value?: number
  /** 浮动盈亏金额 */
  floating_pnl?: number
  /** 已实现平仓盈亏金额 */
  realized_pnl?: number
  /** 详细交易记录表现列表 */
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

/**
 * 资产概览与近 30 天变动汇总
 */
export interface AssetSummary {
  /** 当前净资产总值 */
  current_value: number
  /** 资产总额 */
  total_assets: number
  /** 负债总额 */
  total_liabilities: number
  /** 近 30 天净资产变动金额 */
  change_30d: number
  /** 近 30 天净资产变动百分比 */
  change_30d_pct: number
  /** 按分类划分的分布 */
  by_category: { name: string; value: number; is_liability: boolean }[]
}

/**
 * 单个投资持仓品项明细
 */
export interface HoldingsItem {
  /** 资产 ID */
  asset_id: number
  /** 资产名称 (如 '贵州茅台', '纳指ETF') */
  name: string
  /** 资产类型 ('stock', 'fund', 'crypto', 'bond' 等) */
  asset_type: string
  /** 币种代码 */
  currency: string
  /** 持仓份额/数量 */
  quantity: number
  /** 最新单位净值/市价 */
  net_value: number
  /** 持仓总成本 (Cost Basis) */
  cost_basis: number
  /** 当前最新总市值 (quantity * net_value) */
  current_value: number
  /** 浮动盈亏金额 (current_value - cost_basis) */
  floating_pnl: number
  /** 浮动盈亏比例 (%) */
  floating_pct: number
  /** 历史累计已实现平仓盈亏 */
  realized_pnl: number
}

/**
 * 持仓整体汇总统计
 */
export interface HoldingsSummary {
  /** 全部持仓总市值 */
  total_market_value: number
  /** 全部持仓总成本 */
  total_cost_basis: number
  /** 累计浮动盈亏总金额 */
  total_floating_pnl: number
  /** 历史累计已实现盈亏总金额 */
  total_realized_pnl: number
  /** 综合浮动收益率 (%) */
  floating_pct: number
}

/**
 * 完整持仓分析报告
 */
export interface HoldingsReport {
  /** 整体持仓汇总统计 */
  summary: HoldingsSummary
  /** 各投资品详细持仓列表 */
  holdings: HoldingsItem[]
}

/**
 * 报表统计 API 服务对象
 */
export const reportsApi = {
  /**
   * 获取指定年月的收支月报
   * @route GET /api/reports/expense/monthly
   * @param year 年份
   * @param month 月份
   * @returns 月度收支综合报表
   */
  expenseMonthly: (year: number, month: number) =>
    http.get<ExpenseMonthlyReport, ExpenseMonthlyReport>('/reports/expense/monthly', {
      params: { year, month },
    }),

  /**
   * 获取近期收支趋势折线数据
   * @route GET /api/reports/expense/trend
   * @param months 统计最近几个月 (默认 6 个月)
   * @returns 收支趋势数据
   */
  expenseTrend: (months = 6) =>
    http.get<ExpenseTrend, ExpenseTrend>('/reports/expense/trend', {
      params: { months },
    }),

  /**
   * 获取指定年份的年度收支汇总报表
   * @route GET /api/reports/expense/yearly
   * @param year 统计年份 (可选，默认当前年)
   * @returns 年度收支对比数据
   */
  expenseYearly: (year?: number) =>
    http.get<ExpenseYearlyReport, ExpenseYearlyReport>('/reports/expense/yearly', {
      params: { year },
    }),

  /**
   * 获取指定周期的支出分类占比分解
   * @route GET /api/reports/expense/category
   * @param year 年份 (可选)
   * @param month 月份 (可选)
   * @returns 分类占比数据
   */
  expenseCategory: (year?: number, month?: number) =>
    http.get<ExpenseCategoryBreakdown, ExpenseCategoryBreakdown>('/reports/expense/category', {
      params: { year, month },
    }),

  /**
   * 获取指定周期的支出标签占比分解
   * @route GET /api/reports/expense/tag
   * @param year 年份 (可选)
   * @param month 月份 (可选)
   * @returns 标签占比数据
   */
  expenseTag: (year?: number, month?: number) =>
    http.get<ExpenseTagBreakdown, ExpenseTagBreakdown>('/reports/expense/tag', {
      params: { year, month },
    }),

  /**
   * 获取资产净值历史趋势折线数据
   * @route GET /api/reports/asset/trend
   * @param period 时间周期 ('30d', '90d', '1y', 'all'，默认 '30d')
   * @returns 资产净值走势数据
   */
  assetTrend: (period = '30d') =>
    http.get<AssetTrend, AssetTrend>('/reports/asset/trend', {
      params: { period },
    }),

  /**
   * 获取资产与负债分类构成比例
   * @route GET /api/reports/asset/breakdown
   * @returns 资产与负债分解数据
   */
  assetBreakdown: () => http.get<AssetBreakdown, AssetBreakdown>('/reports/asset/breakdown'),

  /**
   * 获取投资交易表现与盈亏统计
   * @route GET /api/reports/transaction/performance
   * @returns 交易表现统计数据
   */
  transactionPerformance: () =>
    http.get<TransactionPerformance, TransactionPerformance>('/reports/transaction/performance'),

  /**
   * 获取资产总览看板指标 (当前总值、负债、30天变动)
   * @route GET /api/reports/asset/summary
   * @returns 资产总览数据
   */
  assetSummary: () => http.get<AssetSummary, AssetSummary>('/reports/asset/summary'),

  /**
   * 获取投资持仓、浮动盈亏与已实现盈亏全量报告
   * @route GET /api/reports/holdings
   * @returns 持仓分析报告
   */
  holdings: () => http.get<HoldingsReport, HoldingsReport>('/reports/holdings'),

  /**
   * 获取多币种外汇汇率波动损益 (FX Gain/Loss) 报告
   * @route GET /api/reports/fx-pnl
   * @param baseCurrency 基准币种 (默认 'CNY')
   * @returns 汇率损益分析报告
   */
  fxPnl: (baseCurrency = 'CNY') =>
    http.get<import('@/types').FxPnlReport, import('@/types').FxPnlReport>('/reports/fx-pnl', {
      params: { base_currency: baseCurrency },
    }),
}

