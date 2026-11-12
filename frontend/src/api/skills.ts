/**
 * @file Agent Skills 技能管理 API 客户端模块
 * @description 提供符合 Anthropic Agent Skills 规范的技能 CRUD、启闭切换、L2/L3 测试执行、SKILL.md 导入导出与默认技能重置。
 */

import http from '@/utils/http'
import type {
  AiSkill,
  SkillCreatePayload,
  SkillUpdatePayload,
  SkillTestResult,
} from '@/types'

/**
 * 获取技能列表 (支持按分类或启用状态过滤)
 * @route GET /api/ai/skills
 */
export async function listSkills(params?: {
  category?: string
  enabled_only?: number
}): Promise<AiSkill[]> {
  const r = (await http.get('/ai/skills', { params })) as unknown
  if (Array.isArray(r)) return r as AiSkill[]
  if (r && typeof r === 'object' && 'data' in r && Array.isArray((r as { data: unknown }).data)) {
    return (r as { data: AiSkill[] }).data
  }
  return []
}

/**
 * 获取单个技能详情
 * @route GET /api/ai/skills/:id
 */
export async function getSkill(id: number): Promise<AiSkill | null> {
  const r = (await http.get(`/ai/skills/${id}`)) as unknown
  if (r && typeof r === 'object' && 'data' in r && (r as { data: unknown }).data) {
    return (r as { data: AiSkill }).data
  }
  return (r as AiSkill) || null
}

/**
 * 创建自定义技能
 * @route POST /api/ai/skills
 */
export async function createSkill(payload: SkillCreatePayload): Promise<{ id: number }> {
  const r = (await http.post('/ai/skills', payload)) as unknown
  if (r && typeof r === 'object' && 'id' in r) {
    return r as { id: number }
  }
  const d = (r && typeof r === 'object' && 'data' in r ? (r as { data: unknown }).data : r) as
    | { id: number }
    | undefined
  return { id: d?.id ?? 0 }
}

/**
 * 更新技能元数据或指令
 * @route PUT /api/ai/skills/:id
 */
export async function updateSkill(id: number, payload: SkillUpdatePayload): Promise<void> {
  await http.put(`/ai/skills/${id}`, payload)
}

/**
 * 删除技能
 * @route DELETE /api/ai/skills/:id
 */
export async function deleteSkill(id: number): Promise<void> {
  await http.delete(`/ai/skills/${id}`)
}

/**
 * 启用或禁用技能
 * @route POST /api/ai/skills/:id/toggle
 */
export async function toggleSkill(id: number, enabled: boolean): Promise<void> {
  await http.post(`/ai/skills/${id}/toggle`, { enabled: enabled ? 1 : 0 })
}

/**
 * 技能测试执行 (L2 规则检查或 L3 脚本沙箱试跑)
 * @route POST /api/ai/skills/:id/test
 */
export async function testSkill(id: number, args?: string): Promise<SkillTestResult> {
  const r = (await http.post(`/ai/skills/${id}/test`, { arguments: args || '' })) as unknown
  if (r && typeof r === 'object' && 'data' in r && (r as { data: unknown }).data) {
    return (r as { data: SkillTestResult }).data
  }
  return r as SkillTestResult
}

/**
 * 从 Markdown (SKILL.md) 导入技能
 * @route POST /api/ai/skills/import
 */
export async function importSkillMarkdown(content: string): Promise<{ id: number }> {
  const r = (await http.post('/ai/skills/import', { content })) as unknown
  if (r && typeof r === 'object' && 'id' in r) {
    return r as { id: number }
  }
  const d = (r && typeof r === 'object' && 'data' in r ? (r as { data: unknown }).data : r) as
    | { id: number }
    | undefined
  return { id: d?.id ?? 0 }
}

/**
 * 导出技能为 Markdown 文本
 * @route GET /api/ai/skills/:id/export
 */
export async function exportSkillMarkdown(id: number): Promise<string> {
  const r = (await http.get(`/ai/skills/${id}/export`)) as unknown
  if (r && typeof r === 'object' && 'content' in r) {
    return (r as { content: string }).content
  }
  const d = (r && typeof r === 'object' && 'data' in r ? (r as { data: unknown }).data : r) as
    | { content: string }
    | undefined
  return d?.content || ''
}

/**
 * 重置为默认内置预置技能
 * @route POST /api/ai/skills/reset-defaults
 */
export async function resetDefaultSkills(): Promise<void> {
  await http.post('/ai/skills/reset-defaults')
}
