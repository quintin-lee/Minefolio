/**
 * @file IM 聊天机器人 (QQ / 微信) 绑定管理 API 模块
 * @description 提供 IM 绑定列表查询、一次性绑定码签发及平台账号解绑功能
 */

import http from '@/utils/http'

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
}

export default imApi
