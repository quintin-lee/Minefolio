/**
 * @file 分类体系管理 API 接口模块
 * @description 提供资产类别、支出类别、收入类别与交易类型的层级树形分类维护接口
 */

// frontend/src/api/categories.ts
import http from '@/utils/http'
import type { AxiosRequestConfig } from 'axios'
import type { Category, CategoryInput, PageResult } from '@/types'

/**
 * 分类管理 API 服务对象
 */
export const categoriesApi = {
  /**
   * 分页获取分类树列表 (按顶级根分类分页，携带完整子分类树)
   * @route GET /api/categories
   * @param params 筛选与分页参数
   */
  list: (params?: { type?: string; keyword?: string; page?: number; page_size?: number }) =>
    http.get<PageResult<Category>>('/categories', { params }),

  /**
   * 按父分类 ID 懒加载获取直接子分类列表
   * @route GET /api/categories/:id/children
   * @param id 父分类 ID
   * @returns 子分类实体对象数组
   */
  children: (id: number) => http.get<Category[]>(`/categories/${id}/children`),

  /**
   * 创建新的分类节点
   * @route POST /api/categories
   * @param data 分类创建数据载荷
   * @param data.name 分类名称
   * @param data.type 分类类型 ('asset' | 'expense' | 'income' | 'transaction')
   * @param data.parent_id 父分类 ID (可选，未指定则为根分类)
   * @param data.icon 图标名称 (可选)
   * @param data.sort_order 排序序号 (可选)
   * @param config Axios 请求配置 (可选，用于转发如 X-Ledger-Id 等请求头)
   * @returns 空响应
   */
   create: (data: CategoryInput, config?: AxiosRequestConfig) => http.post<void>('/categories', data, config),

  /**
   * 更新指定分类信息
   * @route PUT /api/categories/:id
   * @param id 分类 ID
   * @param data 更新的数据载荷
   * @param config Axios 请求配置 (可选，用于转发如 X-Ledger-Id 等请求头)
   * @returns 空响应
   */
   update: (id: number, data: CategoryInput, config?: AxiosRequestConfig) => http.put<void>(`/categories/${id}`, data, config),

  /**
   * 删除指定分类节点 (注意：若有子节点或已被资产/交易关联则可能受限)
   * @route DELETE /api/categories/:id
   * @param id 分类 ID
   * @param config Axios 请求配置 (可选，用于转发如 X-Ledger-Id 等请求头)
   * @returns 空响应
   */
  delete: (id: number, config?: AxiosRequestConfig) => http.delete<void>(`/categories/${id}`, config),
}

