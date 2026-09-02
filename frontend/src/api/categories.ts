/**
 * @file 分类体系管理 API 接口模块
 * @description 提供资产类别、支出类别、收入类别与交易类型的层级树形分类维护接口
 */

// frontend/src/api/categories.ts
import http from '@/utils/http'
import type { Category } from '@/types'

/**
 * 分类管理 API 服务对象
 */
export const categoriesApi = {
  /**
   * 获取分类列表 (默认获取所有顶级根分类，或按类型筛选)
   * @route GET /api/categories
   * @param params 筛选参数
   * @param params.type 分类用途类型 ('asset' 资产 | 'expense' 支出 | 'income' 收入 | 'transaction' 交易)
   * @returns 分类实体对象数组
   */
  list: (params?: { type?: string }) => http.get<Category[], Category[]>('/categories', { params }),

  /**
   * 按父分类 ID 懒加载获取直接子分类列表
   * @route GET /api/categories/:id/children
   * @param id 父分类 ID
   * @returns 子分类实体对象数组
   */
  children: (id: number) => http.get<Category[], Category[]>(`/categories/${id}/children`),

  /**
   * 创建新的分类节点
   * @route POST /api/categories
   * @param data 分类创建数据载荷
   * @param data.name 分类名称
   * @param data.type 分类类型 ('asset' | 'expense' | 'income' | 'transaction')
   * @param data.parent_id 父分类 ID (可选，未指定则为根分类)
   * @param data.icon 图标名称 (可选)
   * @param data.sort_order 排序序号 (可选)
   * @returns 空响应
   */
  create: (data: any) => http.post<void, void>('/categories', data),

  /**
   * 更新指定分类信息
   * @route PUT /api/categories/:id
   * @param id 分类 ID
   * @param data 更新的数据载荷
   * @returns 空响应
   */
  update: (id: number, data: any) => http.put<void, void>(`/categories/${id}`, data),

  /**
   * 删除指定分类节点 (注意：若有子节点或已被资产/交易关联则可能受限)
   * @route DELETE /api/categories/:id
   * @param id 分类 ID
   * @returns 空响应
   */
  delete: (id: number) => http.delete<void, void>(`/categories/${id}`),
}

