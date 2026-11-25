/**
 * @file IM 聊天机器人 (QQ / 微信) 绑定管理与 iLink 实例 API 模块
 * @description 提供 IM 绑定列表查询、一次性绑定码签发、平台账号解绑，以及用户级 iLink 实例生命周期接口
 */

import QRCode from 'qrcode'
import http from '@/utils/http'
import type { ImBotInstance } from '@/types'

export interface ImBindingItem {
  platform: 'qq' | 'wechat' | string
  im_user_id: string
  scope: string
  enabled: number | boolean
  created_at: string
}

export interface ImBindTokenResult {
  bind_token: string
  ttl_sec: number
}

export async function renderQrContentToImage(content: string): Promise<string> {
  if (/^(https?:|data:image\/)/.test(content)) {
    return content
  }
  return QRCode.toDataURL(content, { width: 260, margin: 1 })
}

export const imApi = {
  /**
   * 获取当前用户的 IM 账号绑定列表
   */
  getBindings: () => http.get<ImBindingItem[]>('/im/bindings'),

  /**
   * 生成 300 秒有效期的受限 IM 绑定码
   */
  generateBindToken: () => http.post<ImBindTokenResult>('/im/bindings/token'),

  /**
   * 解绑指定的 IM 账号
   */
  unbind: (platform: string, im_user_id: string) =>
    http.delete<{ code: number; message: string }>('/im/bindings', {
      data: { platform, im_user_id },
    }),

  /**
   * 列出当前用户的 iLink 微信机器人实例
   */
  listInstances: () => http.get<ImBotInstance[]>('/im/instances'),

  /**
   * 获取单个 iLink 微信机器人实例详情
   */
  getInstance: (id: number) => http.get<ImBotInstance>(`/im/instances/${id}`),

  /**
   * 创建当前用户的 iLink 微信机器人实例
   */
  createInstance: (payload: { platform?: string; adapter?: string } = { platform: 'wechat', adapter: 'ilink' }) =>
    http.post<ImBotInstance>('/im/instances', payload),

  /**
   * 请求重新扫码登录
   */
  reloginInstance: (id: number) => http.post<null>(`/im/instances/${id}/relogin`),

  /**
   * 提交手机登录验证码（4-8 位数字）
   */
  verifyInstanceCode: (id: number, code: string) =>
    http.post<null>(`/im/instances/${id}/verify-code`, { code }),

  /**
   * 启用实例
   */
  enableInstance: (id: number) => http.post<null>(`/im/instances/${id}/enable`),

  /**
   * 停用实例
   */
  disableInstance: (id: number) => http.post<null>(`/im/instances/${id}/disable`),

  /**
   * 删除实例并解绑
   */
  deleteInstance: (id: number) => http.delete<null>(`/im/instances/${id}`),
}

export default imApi
