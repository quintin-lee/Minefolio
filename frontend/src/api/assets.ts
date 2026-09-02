/**
 * @file 资产与账户管理 API 接口模块
 * @description 提供资产/账户的增删改查、分类筛选与详情获取相关接口
 */

// frontend/src/api/assets.ts
import http from '@/utils/http'
import type { Asset, PageResult } from '@/types'

/**
 * 资产与账户 API 服务对象
 */
export const assetsApi = {
  /**
   * 分页查询资产与账户列表
   * @route GET /api/assets
   * @param params 筛选与分页参数
   * @param params.category_id 按资产分类 ID 筛选 (可选)
   * @param params.page 当前页码 (默认 1)
   * @param params.page_size 每页数量 (默认 20)
   * @returns 分页资产列表数据
   */
  list: (params?: { category_id?: string; page?: number; page_size?: number }) =>
    http.get<PageResult<Asset>, PageResult<Asset>>('/assets', { params }),

  /**
   * 创建新的资产或账户 (如银行卡、现金、证券账户、信用卡等)
   * @route POST /api/assets
   * @param data 资产创建数据载荷
   * @param data.name 资产名称
   * @param data.category_id 所属分类 ID
   * @param data.account_no 账号/卡号 (可选)
   * @param data.current_value 当前总价值/余额
   * @param data.quantity 持有份额/数量 (可选，用于投资品)
   * @param data.cost_basis 成本金额 (可选，用于投资品)
   * @param data.net_value 最新单位净值 (可选，用于投资品)
   * @param data.currency 币种代码 (如 'CNY', 'USD')
   * @param data.note 备注信息 (可选)
   * @returns 空响应
   */
  create: (data: any) => http.post<void, void>('/assets', data),

  /**
   * 更新指定资产或账户信息
   * @route PUT /api/assets/:id
   * @param id 资产 ID
   * @param data 更新的数据载荷 (属性与创建类似)
   * @returns 空响应
   */
  update: (id: number, data: any) => http.put<void, void>(`/assets/${id}`, data),

  /**
   * 删除指定资产或账户
   * @route DELETE /api/assets/:id
   * @param id 资产 ID
   * @returns 空响应
   */
  delete: (id: number) => http.delete<void, void>(`/assets/${id}`),

  /**
   * 获取指定资产的详细信息
   * @route GET /api/assets/:id
   * @param id 资产 ID
   * @returns 资产详情对象
   */
  detail: (id: number) => http.get<Asset, Asset>(`/assets/${id}`),
}

