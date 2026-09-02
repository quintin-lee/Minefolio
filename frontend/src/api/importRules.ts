/**
 * @file 账单导入自动分类规则 API 接口模块
 * @description 提供账单导入时的关键字匹配、分类映射规则的增删改查及默认规则重置等功能
 */

// frontend/src/api/importRules.ts
import http from '@/utils/http'

/**
 * 账单导入规则实体
 */
export interface ImportRule {
  /** 规则唯一标识 ID */
  id: number
  /** 所属用户 ID */
  user_id: number
  /** 匹配关键字 (如 '美团', '滴滴', '星巴克') */
  keyword: string
  /** 匹配目标字段 ('all': 全部字段, 'description': 交易描述, 'counterparty': 交易对手/商户名, 'note': 备注) */
  match_field: 'all' | 'description' | 'counterparty' | 'note'
  /** 匹配模式 ('contains': 包含, 'exact': 精确相等, 'regex': 正则表达式) */
  match_type: 'contains' | 'exact' | 'regex'
  /** 映射目标分类 ID */
  category_id: number | null
  /** 映射目标分类名称 */
  category_name?: string
  /** 目标记录类型 ('expense': 支出, 'income': 收入, 'transaction': 交易流水) */
  target_type: 'expense' | 'income' | 'transaction'
  /** 规则执行优先级 (数字越大优先级越高) */
  priority: number
  /** 规则是否启用 */
  is_active: boolean | number
  /** 创建时间 (ISO 8601 字符串) */
  created_at: string
}

/**
 * 导入规则创建/更新数据载荷
 */
export interface ImportRuleCreatePayload {
  /** 匹配关键字 */
  keyword: string
  /** 匹配字段 (可选) */
  match_field?: string
  /** 匹配模式 (可选) */
  match_type?: string
  /** 映射分类 ID (可选) */
  category_id?: number
  /** 目标类型 (可选) */
  target_type?: string
  /** 优先级 (可选) */
  priority?: number
  /** 是否启用 (可选) */
  is_active?: boolean
}

/**
 * 导入规则 API 服务对象
 */
export const importRulesApi = {
  /**
   * 获取当前用户的所有导入分类规则列表
   * @route GET /api/import-rules
   * @returns 导入规则对象数组
   */
  list: () => http.get<ImportRule[], any, void>('/import-rules'),

  /**
   * 获取指定 ID 的导入规则详情
   * @route GET /api/import-rules/:id
   * @param id 规则 ID
   * @returns 导入规则实体
   */
  get: (id: number) => http.get<ImportRule, any, void>(`/import-rules/${id}`),

  /**
   * 创建新的导入分类规则
   * @route POST /api/import-rules
   * @param data 规则创建载荷
   * @returns 创建成功后的规则 ID
   */
  create: (data: ImportRuleCreatePayload) =>
    http.post<{ id: number }, any, ImportRuleCreatePayload>('/import-rules', data),

  /**
   * 更新指定导入分类规则
   * @route PUT /api/import-rules/:id
   * @param id 规则 ID
   * @param data 更新的数据载荷
   * @returns 操作结果
   */
  update: (id: number, data: ImportRuleCreatePayload) =>
    http.put<void, any, ImportRuleCreatePayload>(`/import-rules/${id}`, data),

  /**
   * 删除指定的导入分类规则
   * @route DELETE /api/import-rules/:id
   * @param id 规则 ID
   * @returns 操作结果
   */
  delete: (id: number) => http.delete<void, any, void>(`/import-rules/${id}`),

  /**
   * 重置恢复系统内置的默认分类规则集 (如常见餐饮、交通、购物关键字)
   * @route POST /api/import-rules/reset-defaults
   * @returns 重置后的规则列表
   */
  resetDefaults: () => http.post<ImportRule[], any, void>('/import-rules/reset-defaults'),
}

