/**
 * @file 资产余额变更日志 API 接口模块
 * @description 提供资产历史余额变动流水记录查询相关接口
 */

// frontend/src/api/asset_logs.ts
import http from '@/utils/http'
import type { AssetBalanceLog, PageResult } from '@/types'

/**
 * 资产余额日志 API 服务对象
 */
export const assetLogsApi = {
  /**
   * 分页查询资产余额变动日志
   * @route GET /api/asset-balance-logs
   * @param params 查询参数
   * @param params.asset_id 按指定资产 ID 筛选 (可选)
   * @param params.page 当前页码 (默认 1)
   * @param params.page_size 每页条数 (默认 20)
   * @returns 分页余额日志列表数据
   */
  list: (params?: { asset_id?: string; page?: number; page_size?: number }) =>
    http.get<PageResult<AssetBalanceLog>, PageResult<AssetBalanceLog>>('/asset-balance-logs', {
      params,
    }),
}

