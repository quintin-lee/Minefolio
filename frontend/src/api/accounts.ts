/**
 * @file 账户管理 API 接口模块
 * @description 提供资金账户 (accounts) 的增删改查接口
 */
import http from '@/utils/http'
import type { AxiosRequestConfig } from 'axios'
import type {
  Account,
  AccountInput,
  AccountUpdate,
  AccountPermission,
  AccountPermissionBatchItem,
} from '@/types'

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
   * 获取指定账户详情
   * @route GET /api/accounts/:id
   * @param id 账户 ID
   */
  get: (id: number) => http.get<Account>(`/accounts/${id}`),

  /**
   * 创建新账户
   * @route POST /api/accounts
   * @param data 账户创建载荷
   * @returns 新建账户 ID
   */
  create: (data: AccountInput, config?: AxiosRequestConfig) => http.post<{ id: number }>('/accounts', data, config),

  /**
   * 更新账户字段（省略的字段保持不变）；archived 归档/恢复
   * @route PUT /api/accounts/:id
   * @param id 账户 ID
   * @param data 更新载荷
   */
  update: (id: number, data: AccountUpdate, config?: AxiosRequestConfig) => http.put<void>(`/accounts/${id}`, data, config),

  /**
   * 删除账户（仍挂有资产时返回 1004 冲突）
   * @route DELETE /api/accounts/:id
   * @param id 账户 ID
   */
  delete: (id: number, config?: AxiosRequestConfig) => http.delete<void>(`/accounts/${id}`, config),

  /**
   * 查询指定账户的所有成员细粒度权限配置 (仅账本所有者)
   * @route GET /api/ledgers/:ledgerId/accounts/:accountId/permissions
   */
  listPermissions: (ledgerId: number, accountId: number) =>
    http.get<AccountPermission[]>(`/ledgers/${ledgerId}/accounts/${accountId}/permissions`),

  /**
   * 批量配置指定账户的成员细粒度权限 (仅账本所有者)
   * @route PUT /api/ledgers/:ledgerId/accounts/:accountId/permissions
   */
  batchSetPermissions: (
    ledgerId: number,
    accountId: number,
    permissions: AccountPermissionBatchItem[],
    config?: AxiosRequestConfig
  ) =>
    http.put<void>(
      `/ledgers/${ledgerId}/accounts/${accountId}/permissions`,
      { permissions },
      config
    ),

  /**
   * 查询指定成员在账本中所有账户的细粒度权限配置 (仅账本所有者)
   * @route GET /api/ledgers/:ledgerId/members/:userId/account-permissions
   */
  listMemberPermissions: (ledgerId: number, userId: number) =>
    http.get<AccountPermission[]>(`/ledgers/${ledgerId}/members/${userId}/account-permissions`),

  /**
   * 批量配置指定成员在各账户上的细粒度权限 (仅账本所有者)
   * @route PUT /api/ledgers/:ledgerId/members/:userId/account-permissions
   */
  batchSetMemberPermissions: (
    ledgerId: number,
    userId: number,
    permissions: AccountPermissionBatchItem[],
    config?: AxiosRequestConfig
  ) =>
    http.put<void>(
      `/ledgers/${ledgerId}/members/${userId}/account-permissions`,
      { permissions },
      config
    ),

  /**
   * 删除账户成员细粒度权限覆盖，重置为继承账本角色权限 (仅账本所有者)
   * @route DELETE /api/ledgers/:ledgerId/accounts/:accountId/permissions/:userId
   */
  deletePermission: (
    ledgerId: number,
    accountId: number,
    userId: number,
    config?: AxiosRequestConfig
  ) =>
    http.delete<void>(
      `/ledgers/${ledgerId}/accounts/${accountId}/permissions/${userId}`,
      config
    ),
}
