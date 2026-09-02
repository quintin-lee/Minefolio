/**
 * @file 行情与多币种外汇 API 接口模块
 * @description 提供金融标的行情搜索 (A股/港股/美股/基金/加密货币)、实时行情同步、历史净值走势、汇率查询与外汇折算等接口
 */

import http from '@/utils/http'
import type { MarketSearchItem, MarketQuote, PriceHistoryItem, MarketSettings, TestProxyResult } from '@/types'

/**
 * 金融行情与汇率 API 服务对象
 */
export const marketApi = {
  /**
   * 搜索股票/基金/加密货币标的
   * @route GET /api/market/search
   * @param keyword 搜索关键字 (如 '腾讯', '0700.HK', 'AAPL', 'BTC', '005827')
   * @returns 匹配的行情标的列表
   */
  search: (keyword: string) =>
    http.get<MarketSearchItem[], MarketSearchItem[]>('/market/search', { params: { keyword } }),

  /**
   * 获取指定标的的最新实时行情报价
   * @route GET /api/market/quote
   * @param symbol 标的代码 (如 'sh600519', 'AAPL', 'BTC-USD')
   * @param source 指定行情数据源 (如 'eastmoney', 'yahoo', 'binance' 等，可选)
   * @returns 实时报价数据对象
   */
  getQuote: (symbol: string, source?: string) =>
    http.get<MarketQuote, MarketQuote>('/market/quote', { params: { symbol, source } }),

  /**
   * 批量同步并更新所有持仓投资品的最新市价与净值
   * @route POST /api/market/sync
   * @returns 同步统计结果 (成功数量与失败数量)
   */
  syncAll: () =>
    http.post<{ synced_count: number; failed_count: number }, { synced_count: number; failed_count: number }>('/market/sync'),

  /**
   * 单独同步更新指定资产的最新行情市价与净值
   * @route POST /api/market/sync/:assetId
   * @param assetId 资产 ID
   * @returns 最新的行情报价数据
   */
  syncSingle: (assetId: number) =>
    http.post<MarketQuote, MarketQuote>(`/market/sync/${assetId}`),

  /**
   * 获取指定资产的历史价格/净值走势序列
   * @route GET /api/market/history/:assetId
   * @param assetId 资产 ID
   * @param limit 获取的历史点位天数上限 (默认 90 天)
   * @returns 历史价格时间序列数组
   */
  getHistory: (assetId: number, limit = 90) =>
    http.get<PriceHistoryItem[], PriceHistoryItem[]>(`/market/history/${assetId}`, { params: { limit } }),

  /**
   * 获取行情与数据源配置 (包含代理设置、定时同步频率、各市场默认数据源)
   * @route GET /api/market/settings
   * @returns 行情设置对象
   */
  getSettings: () =>
    http.get<MarketSettings, MarketSettings>('/market/settings'),

  /**
   * 更新行情与数据源配置
   * @route PUT /api/market/settings
   * @param data 更新的数据载荷
   * @returns 空响应
   */
  updateSettings: (data: Partial<MarketSettings>) =>
    http.put<void, void>('/market/settings', data),

  /**
   * 测试行情 HTTP 代理服务器的连通性与响应速度
   * @route POST /api/market/test-proxy
   * @param data 待测试的代理地址参数 (如 { market_proxy: 'http://127.0.0.1:7890' })
   * @returns 代理连通性测试结果
   */
  testProxy: (data?: { market_proxy?: string }) =>
    http.post<TestProxyResult, TestProxyResult>('/market/test-proxy', data),

  /**
   * 获取最新实时法币汇率映射表 (以 CNY 为基准)
   * @route GET /api/market/exchange-rates
   * @returns 币种代码与汇率的键值映射字典 (如 { 'USD': 7.23, 'EUR': 7.85, 'HKD': 0.92 })
   */
  getExchangeRates: () =>
    http.get<Record<string, number>, Record<string, number>>('/market/exchange-rates'),

  /**
   * 手动更新或覆盖指定币种的兑换汇率
   * @route POST /api/market/exchange-rates
   * @param currency 币种代码 (如 'USD', 'JPY')
   * @param rate 对基准币种的汇率数值
   * @returns 空响应
   */
  updateExchangeRate: (currency: string, rate: number) =>
    http.post<void, void>('/market/exchange-rates', { currency, rate }),

  /**
   * 获取按指定基准币种折算后的多币种资产分布与总值汇总
   * @route GET /api/reports/multi-currency-summary
   * @param baseCurrency 基准币种 (默认 'CNY')
   * @returns 多币种汇总报告数据
   */
  getMultiCurrencySummary: (baseCurrency = 'CNY') =>
    http.get<import('@/types').MultiCurrencySummary, import('@/types').MultiCurrencySummary>('/reports/multi-currency-summary', {
      params: { base_currency: baseCurrency },
    }),

  /**
   * 获取指定外币兑基准币的历史汇率走势序列
   * @route GET /api/market/fx-history
   * @param currency 目标外币代码 (默认 'USD')
   * @param days 查询天数跨度 (默认 30 天)
   * @returns 历史汇率时间点数组
   */
  getFxHistory: (currency = 'USD', days = 30) =>
    http.get<import('@/types').FxHistoryPoint[], import('@/types').FxHistoryPoint[]>('/market/fx-history', {
      params: { currency, days },
    }),
}


