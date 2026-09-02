/**
 * @file 多账本与家庭协同空间 API 接口模块
 * @description 提供独立账本空间管理、多成员协作权限分配 (Owner/Editor/Viewer) 及邀请码入组等功能
 */

import http from '@/utils/http'
import type { Ledger, LedgerMember, LedgerInviteResult } from '@/types'

/**
 * 账本与成员协作 API 服务对象
 */
export const ledgerApi = {
  /**
   * 获取当前用户参与的所有账本空间列表 (包含个人主账本与协作账本)
   * @route GET /api/ledgers
   * @returns 账本对象数组
   */
  list: () =>
    http.get<Ledger[], Ledger[]>('/ledgers'),

  /**
   * 创建新的账本空间 (如家庭账本、公司差旅、装修专项账本等)
   * @route POST /api/ledgers
   * @param data 账本基础配置
   * @param data.name 账本名称
   * @param data.description 账本描述说明 (可选)
   * @param data.currency 默认基准币种 (默认 CNY)
   * @param data.icon 账本图标 (可选)
   * @returns 创建成功后的新账本 ID
   */
  create: (data: Partial<Ledger>) =>
    http.post<{ id: number }, { id: number }>('/ledgers', data),

  /**
   * 获取指定账本空间的详细信息
   * @route GET /api/ledgers/:id
   * @param id 账本 ID
   * @returns 账本详情数组
   */
  get: (id: number) =>
    http.get<Ledger[], Ledger[]>(`/ledgers/${id}`),

  /**
   * 更新账本基本信息 (名称、描述、图标等)
   * @route PUT /api/ledgers/:id
   * @param id 账本 ID
   * @param data 更新的数据载荷
   * @returns 空响应
   */
  update: (id: number, data: Partial<Ledger>) =>
    http.put<void, void>(`/ledgers/${id}`, data),

  /**
   * 解散或删除指定账本空间 (仅 Owner 拥有此权限)
   * @route DELETE /api/ledgers/:id
   * @param id 账本 ID
   * @returns 空响应
   */
  delete: (id: number) =>
    http.delete<void, void>(`/ledgers/${id}`),

  /**
   * 获取账本成员列表与各自角色
   * @route GET /api/ledgers/:id/members
   * @param id 账本 ID
   * @returns 账本成员对象数组
   */
  listMembers: (id: number) =>
    http.get<LedgerMember[], LedgerMember[]>(`/ledgers/${id}/members`),

  /**
   * 向账本添加/邀请协作成员
   * @route POST /api/ledgers/:id/members
   * @param id 账本 ID
   * @param username 被邀请用户的用户名
   * @param role 赋予的角色 ('editor': 编辑者可记账, 'viewer': 访客仅可查看，默认 'editor')
   * @returns 空响应
   */
  addMember: (id: number, username: string, role: 'editor' | 'viewer' = 'editor') =>
    http.post<void, void>(`/ledgers/${id}/members`, { username, role }),

  /**
   * 修改协作成员的权限角色
   * @route PUT /api/ledgers/:id/members/:userId
   * @param id 账本 ID
   * @param userId 成员用户 ID
   * @param role 新的角色 ('editor' | 'viewer')
   * @returns 空响应
   */
  updateMember: (id: number, userId: number, role: 'editor' | 'viewer') =>
    http.put<void, void>(`/ledgers/${id}/members/${userId}`, { role }),

  /**
   * 移除账本协作成员
   * @route DELETE /api/ledgers/:id/members/:userId
   * @param id 账本 ID
   * @param userId 目标用户 ID
   * @returns 空响应
   */
  removeMember: (id: number, userId: number) =>
    http.delete<void, void>(`/ledgers/${id}/members/${userId}`),

  /**
   * 生成账本邀请码 (用于快速分享给好友或家庭成员)
   * @route POST /api/ledgers/:id/invite-code
   * @param id 账本 ID
   * @returns 邀请码生成结果 (含 invite_code 与过期时间)
   */
  createInviteCode: (id: number) =>
    http.post<LedgerInviteResult, LedgerInviteResult>(`/ledgers/${id}/invite-code`),

  /**
   * 通过他人分享的邀请码加入指定账本
   * @route POST /api/ledgers/join
   * @param inviteCode 账本邀请码字符串
   * @returns 加入的账本 ID 与名称
   */
  joinByInvite: (inviteCode: string) =>
    http.post<{ id: number; name: string }, { id: number; name: string }>('/ledgers/join', {
      invite_code: inviteCode
    })
}

