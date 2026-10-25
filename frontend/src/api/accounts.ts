/**
 * @file 账户管理 API 接口模块
 * @description 提供资金账户 (accounts) 的增删改查接口
 */
import http from '@/utils/http'
import type { Account, AccountInput, AccountUpdate } from '@/types'

/**
 * 账户 API 服务对象
 */
export const accountsApi = {
  /**
   * 查询当前账本下的全部账户（含 asset_count / total_value 聚合）
   * @route GET /api/accounts
   * @param params.include_archived 是否包含已归档账户 (默认不含)
   * @returns 账户列表（无分页，全集单次返回）
   */
  list: (params?: { include_archived?: number }) =>
    http.get<Account[]>('/accounts', { params }),

  /**
   * 创建新账户
   * @route POST /api/accounts
   * @param data 账户创建载荷
   * @returns 新建账户 ID
   */
  create: (data: AccountInput) => http.post<{ id: number }>('/accounts', data),

  /**
   * 更新账户字段（省略的字段保持不变）；archived 归档/恢复
   * @route PUT /api/accounts/:id
   * @param id 账户 ID
   * @param data 更新载荷
   */
  update: (id: number, data: AccountUpdate) => http.put<void>(`/accounts/${id}`, data),

  /**
   * 删除账户（仍挂有资产时返回 1004 冲突）
   * @route DELETE /api/accounts/:id
   * @param id 账户 ID
   */
  delete: (id: number) => http.delete<void>(`/accounts/${id}`),
}
