/**
 * @file 管理员用户管理 API 接口模块
 * @description 提供管理员查看用户列表、创建用户、调整角色、启用/禁用账号、重置密码、关闭 2FA 及删除用户接口
 */

import http from '@/utils/http'

export type SystemUserRole = 'admin' | 'user'

export interface AdminUserItem {
  id: number
  username: string
  role: SystemUserRole
  is_admin: boolean
  disabled: boolean
  totp_enabled: boolean
  oauth_provider: string
  created_at: string
}

export interface AdminUserStats {
  total_users: number
  active_users: number
  admin_users: number
  active_admin_users: number
  disabled_users: number
}

export interface AdminUsersListResponse {
  list: AdminUserItem[]
  total: number
  page: number
  page_size: number
  stats: AdminUserStats
}

export interface AdminUsersQuery {
  page?: number
  page_size?: number
  keyword?: string
  role?: 'all' | 'admin' | 'user'
  status?: 'all' | 'active' | 'disabled'
}

export const adminUsersApi = {
  /** 分页查询系统用户列表与统计数据 */
  list: (params?: AdminUsersQuery) =>
    http.get<AdminUsersListResponse>('/admin/users', { params }),

  /** 管理员新建用户 */
  create: (data: { username: string; password_enc: string; role: SystemUserRole }) =>
    http.post<AdminUserItem>('/admin/users', data),

  /** 管理员修改用户角色或启用/禁用状态 */
  update: (id: number, data: { role?: SystemUserRole; disabled?: boolean }) =>
    http.put<AdminUserItem>(`/admin/users/${id}`, data),

  /** 管理员重置用户登录密码 */
  resetPassword: (id: number, new_password_enc: string) =>
    http.post<void>(`/admin/users/${id}/reset-password`, { new_password_enc }),

  /** 管理员关闭指定用户的两步验证 */
  disable2Fa: (id: number) =>
    http.post<void>(`/admin/users/${id}/disable-2fa`),

  /** 管理员删除用户（级联清理业务数据） */
  remove: (id: number) =>
    http.delete<void>(`/admin/users/${id}`),
}
