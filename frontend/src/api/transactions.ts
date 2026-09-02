/**
 * @file 资产账户交易流水与资金变动 API 接口模块
 * @description 提供投资买卖、账户转账、余额调整等全场景交易流水的增删改查、月度聚合、CSV 导入与导出接口
 */

// frontend/src/api/transactions.ts
import http from '@/utils/http'
import type { PageResult, Transaction, TransactionMonthly } from '@/types'

/**
 * 资产交易流水 API 服务对象
 */
export const transactionsApi = {
  /**
   * 多条件分页查询资产交易流水列表
   * @route GET /api/transactions
   * @param params 过滤与分页参数
   * @param params.asset_id 关联主资产 ID
   * @param params.category_id 分类 ID
   * @param params.type 交易类型 ('buy' 买入 | 'sell' 卖出 | 'transfer' 转账 | 'dividend' 分红 | 'fee' 手续费 等)
   * @param params.start_date 起始交易日期 (YYYY-MM-DD)
   * @param params.end_date 截止交易日期 (YYYY-MM-DD)
   * @param params.page 当前页码
   * @param params.page_size 每页条数
   * @returns 分页交易记录数据
   */
  list: (params?: {
    asset_id?: string
    category_id?: string
    type?: string
    start_date?: string
    end_date?: string
    page?: number
    page_size?: number
  }) => http.get<PageResult<Transaction>, PageResult<Transaction>>('/transactions', { params }),

  /**
   * 获取指定月份的交易统计概览
   * @route GET /api/transactions/monthly
   * @param month 查询月份 (格式 'YYYY-MM'，如 '2026-08')
   * @returns 月度交易聚合统计数据
   */
  monthly: (month: string) =>
    http.get<TransactionMonthly, TransactionMonthly>('/transactions/monthly', {
      params: { month },
    }),

  /**
   * 创建新的交易流水记录 (自动联动变更关联账户的余额、持仓数量与成本价)
   * @route POST /api/transactions
   * @param data 交易数据载荷
   * @param data.asset_id 主资产 ID
   * @param data.linked_asset_id 关联出资/目标资产 ID (用于转账或买入出资)
   * @param data.transaction_type 交易类型 ('buy' | 'sell' | 'transfer' 等)
   * @param data.amount 交易总金额
   * @param data.price_per_unit 成交单价 (可选)
   * @param data.quantity 交易份额/数量 (可选)
   * @param data.fee 手续费金额 (可选，若大于 0 则自动插入子关联交易并扣减资金账户)
   * @param data.transaction_date 交易发生时间 (YYYY-MM-DD)
   * @param data.note 备注说明 (可选)
   * @returns 空响应
   */
  create: (data: any) => http.post<void, void>('/transactions', data),

  /**
   * 修改交易流水记录 (自动计算并回滚旧影响后再应用新影响)
   * @route PUT /api/transactions/:id
   * @param id 交易 ID
   * @param data 更新的数据载荷
   * @returns 空响应
   */
  update: (id: number, data: any) => http.put<void, void>(`/transactions/${id}`, data),

  /**
   * 删除交易流水 (自动级联回滚关联的手续费子交易及资产余额/持仓变动)
   * @route DELETE /api/transactions/:id
   * @param id 交易 ID
   * @returns 空响应
   */
  delete: (id: number) => http.delete<void, void>(`/transactions/${id}`),

  /**
   * 导出交易流水全量数据为 CSV 文件流
   * @route GET /api/export/transactions
   * @returns CSV 二进制 Blob
   */
  exportCsv: () => http.get('/export/transactions', { responseType: 'blob' }) as unknown as Promise<Blob>,

  /**
   * 批量导入 CSV 文本格式的交易记录
   * @route POST /api/import/transactions
   * @param text CSV 格式文本
   * @returns 导入结果 (包含成功数量、失败数量与错误信息)
   */
  importCsv: (text: string) => http.post<{ imported: number; errors: number; errors_detail?: string }>('/import/transactions', text, {
    headers: { 'Content-Type': 'text/csv; charset=utf-8' },
  }),
}

