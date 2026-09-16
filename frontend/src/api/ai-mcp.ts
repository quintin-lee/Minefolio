/**
 * @file MCP 服务器管理 API 接口模块
 * @description 提供用户配置的外部 MCP 服务器 CRUD、工具缓存读取、探活与强制刷新接口。
 *
 * 后端统一响应信封为 { code, message, data }，axios 拦截器自动解包为 data。
 * 本模块所有函数均直接返回解包后的 data。
 */

import http from '@/utils/http'
import type {
  McpServer,
  McpServerTool,
  McpTestResult,
  McpServerInput,
} from '@/types'

/**
 * 获取当前用户配置的所有 MCP 服务器列表 (含工具缓存计数)
 * @route GET /api/ai/mcp/servers
 * @returns MCP 服务器数组
 */
export async function listMcpServers(): Promise<McpServer[]> {
  const r = (await http.get('/ai/mcp/servers')) as unknown
  if (Array.isArray(r)) return r as McpServer[]
  if (r && typeof r === 'object' && 'data' in r && Array.isArray((r as { data: unknown }).data)) {
    return (r as { data: McpServer[] }).data
  }
  return []
}

/**
 * 获取指定 MCP 服务器详情
 * @route GET /api/ai/mcp/servers/:id
 * @param id 服务器 ID
 * @returns MCP 服务器详情
 */
export async function getMcpServer(id: number): Promise<McpServer | null> {
  const r = (await http.get(`/ai/mcp/servers/${id}`)) as unknown
  if (r && typeof r === 'object' && 'data' in r && (r as { data: unknown }).data) {
    return (r as { data: McpServer }).data
  }
  return (r as McpServer) || null
}

/**
 * 创建 MCP 服务器
 * @route POST /api/ai/mcp/servers
 * @param input 服务器配置 (name/transport/url|command/args/env/headers/secret_ref/enabled/timeout_ms)
 * @returns 新创建的服务器 ID
 */
export async function createMcpServer(input: McpServerInput): Promise<{ id: number }> {
  const r = (await http.post('/ai/mcp/servers', input)) as unknown
  if (r && typeof r === 'object' && 'id' in r) {
    return r as { id: number }
  }
  const d = (r && typeof r === 'object' && 'data' in r ? (r as { data: unknown }).data : r) as
    | { id: number }
    | undefined
  return { id: d?.id ?? 0 }
}

/**
 * 更新 MCP 服务器配置
 * @route PUT /api/ai/mcp/servers/:id
 * @param id 服务器 ID
 * @param input 更新内容 (部分字段可选)
 * @returns 更新操作结果
 */
export async function updateMcpServer(id: number, input: McpServerInput): Promise<void> {
  await http.put(`/ai/mcp/servers/${id}`, input)
}

/**
 * 删除 MCP 服务器 (级联删除其工具缓存)
 * @route DELETE /api/ai/mcp/servers/:id
 * @param id 服务器 ID
 * @returns 删除操作结果
 */
export async function deleteMcpServer(id: number): Promise<void> {
  await http.delete(`/ai/mcp/servers/${id}`)
}

/**
 * 读取指定 MCP 服务器的工具缓存 (不触发远端拉取)
 * @route GET /api/ai/mcp/servers/:id/tools
 * @param id 服务器 ID
 * @returns 工具缓存数组
 */
export async function getMcpServerTools(id: number): Promise<McpServerTool[]> {
  const r = (await http.get(`/ai/mcp/servers/${id}/tools`)) as unknown
  if (Array.isArray(r)) return r as McpServerTool[]
  if (r && typeof r === 'object' && 'data' in r && Array.isArray((r as { data: unknown }).data)) {
    return (r as { data: McpServerTool[] }).data
  }
  return []
}

/**
 * 探活指定 MCP 服务器 (initialize + tools/list, 并回写 discovered_at)
 * @route POST /api/ai/mcp/servers/:id/test
 * @param id 服务器 ID
 * @returns 探活结果 (status/tools 或错误信息)
 * @note M1 阶段后端返回 1002 占位，M2 实装
 */
export async function testMcpServer(id: number): Promise<McpTestResult> {
  const r = (await http.post(`/ai/mcp/servers/${id}/test`)) as unknown
  if (r && typeof r === 'object' && 'data' in r && (r as { data: unknown }).data) {
    return (r as { data: McpTestResult }).data
  }
  return r as McpTestResult
}

/**
 * 强制重拉指定 MCP 服务器的工具列表
 * @route POST /api/ai/mcp/servers/:id/refresh
 * @param id 服务器 ID
 * @returns 刷新后的工具数量
 * @note M1 阶段后端返回 1002 占位，M2 实装
 */
export async function refreshMcpServer(id: number): Promise<{ tool_count: number }> {
  const r = (await http.post(`/ai/mcp/servers/${id}/refresh`)) as unknown
  if (r && typeof r === 'object' && 'data' in r && (r as { data: unknown }).data) {
    return (r as { data: { tool_count: number } }).data
  }
  return (r as { tool_count: number }) || { tool_count: 0 }
}
