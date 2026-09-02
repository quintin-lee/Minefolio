/**
 * @file 日常收支记账 API 接口模块
 * @description 提供日常消费支出、收入流水的增删改查、月度聚合汇总统计、CSV 数据导入与导出等接口
 */

// frontend/src/api/daily_expenses.ts
import http from '@/utils/http'
import type { DailyExpense, ExpenseMonthly, PageResult } from '@/types'

/**
 * 日常收支记账 API 服务对象
 */
export const dailyExpensesApi = {
  /**
   * 多条件分页查询日常收支记录列表
   * @route GET /api/daily-expenses
   * @param params 过滤与分页参数
   * @param params.expense_type 收支类型 ('expense' 支出 | 'income' 收入)
   * @param params.category_id 分类 ID 过滤
   * @param params.tag_ids 关联标签 ID 列表 (逗号分隔)
   * @param params.start_date 起始日期 (YYYY-MM-DD)
   * @param params.end_date 截止日期 (YYYY-MM-DD)
   * @param params.page 当前页码
   * @param params.page_size 每页数量
   * @returns 分页日常收支记录
   */
  list: (params?: {
    expense_type?: string
    category_id?: string
    tag_ids?: string
    start_date?: string
    end_date?: string
    page?: number
    page_size?: number
  }) =>
    http.get<PageResult<DailyExpense>, PageResult<DailyExpense>>('/daily-expenses', {
      params,
    }),

  /**
   * 录入新增一笔日常收支记录
   * @route POST /api/daily-expenses
   * @param data 记账数据载荷
   * @param data.amount 金额
   * @param data.expense_type 类型 ('expense' | 'income')
   * @param data.category_id 分类 ID
   * @param data.asset_id 扣款/入账资产账户 ID (可选)
   * @param data.expense_date 发生日期 (YYYY-MM-DD)
   * @param data.currency 币种代码 (默认 CNY)
   * @param data.note 备注说明 (可选)
   * @param data.tags 关联标签列表 (可选)
   * @returns 空响应
   */
  create: (data: any) => http.post<void, void>('/daily-expenses', data),

  /**
   * 修改日常收支记录
   * @route PUT /api/daily-expenses/:id
   * @param id 记录 ID
   * @param data 更新的数据载荷
   * @returns 空响应
   */
  update: (id: number, data: any) => http.put<void, void>(`/daily-expenses/${id}`, data),

  /**
   * 删除指定的日常收支记录 (同步回滚关联资产账户余额)
   * @route DELETE /api/daily-expenses/:id
   * @param id 记录 ID
   * @returns 空响应
   */
  delete: (id: number) => http.delete<void, void>(`/daily-expenses/${id}`),

  /**
   * 获取指定年月的收支月度聚合统计 (包含总收支、日均支出、分类与标签占比)
   * @route GET /api/daily-expenses/monthly
   * @param year 年份 (如 2026)
   * @param month 月份 (1-12)
   * @returns 月度收支分析聚合对象
   */
  monthly: (year: number, month: number) =>
    http.get<ExpenseMonthly, ExpenseMonthly>(`/daily-expenses/monthly`, {
      params: { year, month },
    }),

  /**
   * 导出日常收支全部记录为 CSV 文件流
   * @route GET /api/export/daily-expenses
   * @returns CSV 文件的二进制 Blob 对象
   */
  exportCsv: () => http.get('/export/daily-expenses', { responseType: 'blob' }) as unknown as Promise<Blob>,

  /**
   * 批量导入 CSV 文本格式的日常收支数据
   * @route POST /api/import/daily-expenses
   * @param text CSV 格式纯文本内容
   * @returns 导入结果 (包含成功条数、失败条数与错误详情)
   */
  importCsv: (text: string) => http.post<{ imported: number; errors: number; errors_detail?: string }>(
    '/import/daily-expenses', text, { headers: { 'Content-Type': 'text/csv; charset=utf-8' } }
  ),
}

