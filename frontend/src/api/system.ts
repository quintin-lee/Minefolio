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
  status: () => http.get<SystemStatus, any, void>('/system/status'),

  /**
   * 执行系统首次引导初始化 (创建超级管理员账号及可选数据库设置)
   * @route POST /api/system/setup
   * @param data 初始化安装数据
   * @param data.username 管理员用户名
   * @param data.password_enc 经 RSA-OAEP 加密后的管理员密码
   * @param data.db_driver 数据库驱动 ('sqlite' | 'postgres'，可选)
   * @param data.db_dsn 数据库连接串 (可选)
   * @returns 初始登录 Token 及过期时间
   */
  setup: (data: { username: string; password_enc: string; db_driver?: string; db_dsn?: string }) =>
    http.post<{ token: string; expires_in: number }, any, typeof data>('/system/setup', data),
}

