/**
 * @file 自定义交易标签 API 接口模块
 * @description 提供账单与交易的多维度标签分类、增删改查及输入联想推荐接口
 */

// frontend/src/api/tags.ts
import http from '@/utils/http'
import type { Tag } from '@/types'

/**
 * 交易标签 API 服务对象
 */
export const tagsApi = {
  /**
   * 获取当前用户的所有标签列表
   * @route GET /api/tags
   * @returns 标签实体对象数组
   */
  list: () => http.get<Tag[], Tag[]>('/tags'),

  /**
   * 创建新的自定义标签
   * @route POST /api/tags
   * @param data 标签数据
   * @param data.name 标签名称 (如 '差旅报销', '外卖', '副业')
   * @param data.color 标签颜色 HEX 字符串 (如 '#ff5722'，可选)
   * @returns 空响应
   */
  create: (data: { name: string; color?: string }) =>
    http.post<void, void>('/tags', data),

  /**
   * 修改指定标签的名称或颜色
   * @route PUT /api/tags/:id
   * @param id 标签 ID
   * @param data 更新的数据载荷
   * @returns 空响应
   */
  update: (id: number, data: { name?: string; color?: string }) =>
    http.put<void, void>(`/tags/${id}`, data),

  /**
   * 删除指定标签
   * @route DELETE /api/tags/:id
   * @param id 标签 ID
   * @returns 空响应
   */
  delete: (id: number) => http.delete<void, void>(`/tags/${id}`),

  /**
   * 根据关键字联想匹配标签建议列表 (用于记账时快速补全)
   * @route GET /api/tags/suggestions
   * @param q 搜索输入前缀
   * @returns 匹配的标签建议列表
   */
  suggestions: (q?: string) =>
    http.get<Tag[], Tag[]>('/tags/suggestions', { params: { q } }),
}

