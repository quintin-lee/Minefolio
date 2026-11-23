/**
 * @file 系统初始化与运行状态 API 接口模块
 * @description 提供系统首次安装引导、管理员初始化与系统状态检测等接口
 */

import http from '@/utils/http'

/**
 * 系统初始化运行状态信息
 */
export interface SystemStatus {
  initialized: boolean
  user_count: number
  version: string
}

/**
 * 系统管理与初始化 API 服务对象
 */
export const systemApi = {
  /**
   * 检测系统是否完成初始化与系统用户数
   * @route GET /api/system/status
   * @returns 系统初始化状态对象
   */
  status: () => http.get<SystemStatus>('/system/status'),

  /**
   * 执行系统首次引导初始化 (创建超级管理员账号)。
   * 数据库连接由部署侧环境变量提供，此处不下发 db 配置。
   * @route POST /api/system/setup
   * @param data 初始化安装数据
   * @param data.username 管理员用户名
   * @param data.password_enc 经 RSA-OAEP 加密后的管理员密码
   * @returns 初始登录 Token 及过期时间
   */
  setup: (data: { username: string; password_enc: string }) =>
    http.post<{ token: string; expires_in: number }>('/system/setup', data),
}

