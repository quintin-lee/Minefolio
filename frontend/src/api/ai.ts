/**
 * @file AI 助手与智能工作流 API 接口模块
 * @description 提供 AI 模型获取、会话管理、流式对话 (SSE)、设置配置、工作流执行与连通性测试等功能
 */

// frontend/src/api/ai.ts
import http, { getCookie, buildApiUrl } from '@/utils/http'
import { useAuthStore } from '@/stores/auth'
import type { WorkflowDef, WorkflowRunState, WorkflowConfigState } from '@/types'

/**
 * AI 模型配置选项 (按模型提供商分组)
 */
export interface AiModelOption {
  /** 模型提供商唯一标识 (如 openai, anthropic, ollama 等) */
  provider_id: string
  /** 模型提供商可读名称 */
  provider_name: string
  /** 该提供商支持的模型名称列表 */
  models: string[]
}

/**
 * AI 对话会话信息
 */
export interface AiSession {
  /** 会话唯一标识 ID */
  id: number
  /** 所属用户 ID */
  user_id: number
  /** 会话标题 */
  title: string
  /** 当前会话使用的模型名称 */
  model: string
  /** 当前会话使用的模型提供商 */
  provider: string
  /** 创建时间 (ISO 8601 字符串) */
  created_at: string
  /** 更新时间 (ISO 8601 字符串) */
  updated_at: string
}

/**
 * AI 对话单条消息结构
 */
export interface AiMessage {
  /** 消息唯一标识 ID */
  id: number
  /** 所属会话 ID */
  session_id: number
  /** 发送者角色 ('user': 用户, 'assistant': AI 助手) */
  role: 'user' | 'assistant'
  /** 消息正文 Markdown 文本 */
  content: string
  /** 生成该回答的模型名称 (可选) */
  model?: string
  /** 消息生成时间 (ISO 8601 字符串) */
  created_at: string
  /** 关联的工作流运行时状态数据 (若该消息触发了工作流执行) */
  workflowData?: WorkflowRunState
  /** 关联的工作流配置就绪状态数据 (若该消息内嵌工作流配置卡片) */
  workflowConfig?: WorkflowConfigState
}

/**
 * AI 提供商配置项
 */
export interface AiProviderConfig {
  /** 提供商标识 (如 'openai', 'anthropic', 'custom' 等) */
  id: string
  /** 提供商显示名称 */
  name: string
  /** API 基础服务地址 (Base URL) */
  base_url: string
  /** 明文 API Key (仅前端提交时携带) */
  api_key?: string
  /** 加密存储的 API Key (后端返回脱敏或加密后的值) */
  api_key_enc?: string
  /** 是否已配置 API Key */
  has_api_key?: boolean
  /** 该提供商下启用的模型列表 */
  models: string[]
}

/**
 * AI 系统全局设置
 */
export interface AiSettings {
  /** 各模型提供商配置列表 */
  providers: AiProviderConfig[]
  /** 默认选中的提供商 ID */
  default_provider: string
  /** 默认选中的模型名称 */
  default_model: string
  /** 携带的历史上下文消息轮数 */
  context_size: number
  /** 全局系统提示词 (System Prompt) */
  system_prompt: string
}

/**
 * 获取系统所有已配置的 AI 模型列表 (按提供商分组)
 * @route GET /api/ai/models
 * @returns 模型配置选项数组
 */
export async function getModels(): Promise<AiModelOption[]> {
  const r = (await http.get('/ai/models')) as unknown
  if (Array.isArray(r)) return r as AiModelOption[]
  if (r && typeof r === 'object' && 'data' in r && Array.isArray((r as { data: unknown }).data)) {
    return (r as { data: AiModelOption[] }).data
  }
  return []
}

/**
 * 分页获取用户的 AI 对话会话列表
 * @route GET /api/ai/sessions
 * @param params 分页参数
 * @param params.page 当前页码
 * @param params.page_size 每页数量
 * @returns 分页会话列表响应
 */
export async function listSessions(params?: { page?: number; page_size?: number }) {
  return http.get('/ai/sessions', { params })
}

/**
 * 创建新的 AI 对话会话
 * @route POST /api/ai/sessions
 * @param model 初始指定的模型名称 (可选)
 * @param provider 初始指定的提供商标识 (可选)
 * @returns 创建成功后的会话信息
 */
export async function createSession(model?: string, provider?: string) {
  return http.post('/ai/sessions', { model, provider })
}

/**
 * 获取指定 ID 的会话详情
 * @route GET /api/ai/sessions/:id
 * @param id 会话 ID
 * @returns 会话详情数据
 */
export async function getSession(id: number) {
  return http.get(`/ai/sessions/${id}`)
}

/**
 * 更新指定会话的属性 (如重命名标题、切换模型)
 * @route PUT /api/ai/sessions/:id
 * @param id 会话 ID
 * @param data 更新内容
 * @param data.title 新的会话标题
 * @param data.model 新的模型名称
 * @returns 更新结果
 */
export async function updateSession(id: number, data: { title?: string; model?: string }) {
  return http.put(`/ai/sessions/${id}`, data)
}

/**
 * 删除指定的会话及所有历史消息
 * @route DELETE /api/ai/sessions/:id
 * @param id 会话 ID
 * @returns 删除操作结果
 */
export async function deleteSession(id: number) {
  return http.delete(`/ai/sessions/${id}`)
}

/**
 * 分页获取指定会话下的历史消息记录
 * @route GET /api/ai/sessions/:sessionId/messages
 * @param sessionId 会话 ID
 * @param page 当前页码 (默认 1)
 * @param page_size 每页条数 (默认 50)
 * @returns 分页消息列表
 */
export async function getMessages(sessionId: number, page = 1, page_size = 50) {
  return http.get(`/ai/sessions/${sessionId}/messages`, { params: { page, page_size } })
}

/**
 * 流式对话返回的数据分块 (SSE Chunk)
 */
export interface ChatStreamChunk {
  /** 消息分块类型: 'delta' (增量文本), 'done' (生成结束), 'error' (发生错误) */
  type: 'delta' | 'done' | 'error'
  /** 增量文本内容 (当 type 为 'delta' 时) */
  content?: string
  /** 生成结束原因 (如 'stop', 'length' 等) */
  finish_reason?: string
  /** 错误信息 (当 type 为 'error' 时) */
  message?: string
}

/**
 * 发起流式 AI 对话请求 (Server-Sent Events)
 * @route POST /api/ai/chat
 * @param params 对话请求参数
 * @param params.session_id 会话 ID (若未传则在后端新建)
 * @param params.content 用户输入的提示词文本
 * @param params.model 使用的模型名称 (可选)
 * @param params.provider 使用的模型提供商 (可选)
 * @param params.regenerate 是否为重新生成上一轮回答 (可选)
 * @param signal 用于中断流式请求的 AbortSignal
 * @returns 异步可迭代的数据分块生成器 AsyncIterable<ChatStreamChunk>
 */
export async function* chatStream(
  params: {
    session_id?: number
    content: string
    model?: string
    provider?: string
    regenerate?: boolean
  },
  signal?: AbortSignal
): AsyncIterable<ChatStreamChunk> {
  let token = ''
  try {
    const auth = useAuthStore()
    token = auth.token || ''
  } catch {
    // outside reactive context
  }
  if (!token) {
    token = localStorage.getItem('token') || ''
  }
  const csrf = getCookie('csrf_token')
  const headers: Record<string, string> = {
    'Content-Type': 'application/json',
    'Authorization': `Bearer ${token}`,
  }
  if (csrf) {
    headers['X-CSRF-Token'] = csrf
  }
  const url = buildApiUrl('/ai/chat')
  const resp = await fetch(url, {
    method: 'POST',
    headers,
    credentials: 'include',
    body: JSON.stringify(params),
    signal,
  })
  if (!resp.ok || !resp.body) throw new Error(`HTTP ${resp.status}`)
  const reader = resp.body.getReader()
  const decoder = new TextDecoder()
  let buffer = ''
  let currentEvent = 'delta'
  try {
    while (true) {
      const { done, value } = await reader.read()
      if (done) break
      buffer += decoder.decode(value, { stream: true })
      const lines = buffer.split('\n')
      buffer = lines.pop() ?? ''
      for (const line of lines) {
        const trimmed = line.trim()
        if (trimmed.startsWith('event:')) {
          currentEvent = trimmed.slice(6).trim()
        } else if (trimmed.startsWith('data:')) {
          const dataStr = trimmed.slice(5).trim()
          if (dataStr) {
            try {
              const parsed = JSON.parse(dataStr) as Record<string, unknown>
              yield {
                type: (currentEvent || 'delta') as 'delta' | 'done' | 'error',
                content: typeof parsed.content === 'string' ? parsed.content : undefined,
                finish_reason: typeof parsed.finish_reason === 'string' ? parsed.finish_reason : undefined,
                message: typeof parsed.message === 'string' ? parsed.message : undefined,
              }
            } catch {
              // ignore
            }
          }
        }
      }
    }
  } finally {
    reader.releaseLock()
  }
}

/**
 * 获取系统 AI 配置设置 (提供商、API Key、默认模型、上下文轮数等)
 * @route GET /api/settings/ai
 * @returns AI 系统配置设置对象
 */
export async function getSettings(): Promise<AiSettings> {
  const r = (await http.get('/settings/ai')) as unknown
  if (r && typeof r === 'object' && 'data' in r && (r as { data: unknown }).data) {
    return (r as { data: AiSettings }).data
  }
  return r as AiSettings
}

/**
 * 更新系统 AI 配置设置
 * @route PUT /api/settings/ai
 * @param settings 部分或全部更新的 AI 设置
 * @returns 更新操作结果
 */
export async function updateSettings(settings: Partial<AiSettings>) {
  return http.put('/settings/ai', settings)
}

/**
 * AI 服务连通性测试返回结果
 */
export interface AiTestConnectionResult {
  /** 连通测试是否成功 */
  success: boolean
  /** 响应延迟耗时 (毫秒) */
  latency_ms: number
  /** 结果描述或错误提示信息 */
  message: string
}

/**
 * 测试指定 AI 提供商的 API 连通性
 * @route POST /api/settings/ai/test
 * @param data 提供商测试参数
 * @param data.id 提供商标识
 * @param data.base_url 自定义基础地址 (可选)
 * @param data.api_key 明文 API Key (可选)
 * @param data.api_key_enc 加密后的 API Key (可选)
 * @param data.model 测试用的模型名称 (可选)
 * @returns 连通性测试结果对象
 */
export async function testAiConnection(data: {
  id: string
  base_url?: string
  api_key?: string
  api_key_enc?: string
  model?: string
}): Promise<AiTestConnectionResult> {
  const r = (await http.post('/settings/ai/test', data)) as unknown
  if (r && typeof r === 'object' && 'data' in r && (r as { data: unknown }).data) {
    return (r as { data: AiTestConnectionResult }).data
  }
  return (r || { success: false, latency_ms: 0, message: '测试失败' }) as AiTestConnectionResult
}

/**
 * 远程从提供商服务拉取其支持的模型列表 (如从 Ollama 或 OpenAI 拉取)
 * @route POST /api/settings/ai/fetch-models
 * @param data 请求参数 (包含提供商地址与 Key)
 * @returns 该提供商支持的模型名称列表
 */
export async function fetchAiModels(data: {
  id: string
  base_url?: string
  api_key?: string
  api_key_enc?: string
}): Promise<string[]> {
  const r = (await http.post('/settings/ai/fetch-models', data)) as unknown
  const res = (r && typeof r === 'object' && 'data' in r ? (r as { data: { models: unknown } }).data : r) as { models?: unknown }
  return Array.isArray(res?.models) ? (res.models as string[]) : []
}

/**
 * 获取可用的预定义智能财务工作流列表
 * @route GET /api/ai/workflows
 * @returns 工作流定义对象数组
 */
export async function getWorkflows(): Promise<any[]> {
  const r = (await http.get('/ai/workflows')) as unknown
  if (Array.isArray(r)) return r
  if (r && typeof r === 'object' && 'data' in r && Array.isArray((r as { data: unknown }).data)) {
    return (r as { data: any[] }).data
  }
  return []
}

/**
 * 工作流流式执行返回的数据分块 (SSE Chunk)
 */
export interface WorkflowStreamChunk {
  /** 分块事件类型 (工作流开始/步骤开始/进度更新/步骤完成/增量输出/执行完毕/错误) */
  type: 'workflow_start' | 'step_start' | 'step_progress' | 'step_complete' | 'delta' | 'workflow_complete' | 'error'
  /** 工作流唯一标识 ID */
  workflow_id?: string
  /** 工作流标题或步骤标题 */
  title?: string
  /** 工作流总步骤数量 */
  total_steps?: number
  /** 当前正在执行的步骤索引 (从 0 开始) */
  step_index?: number
  /** 当前步骤标识 ID */
  step_id?: string
  /** 步骤或工作流状态 ('pending' | 'running' | 'completed' | 'error') */
  status?: string
  /** 步骤完成后的执行摘要 */
  summary?: string
  /** 增量文本输出内容 */
  content?: string
  /** 关联的会话 ID */
  session_id?: number
  /** 异常错误描述信息 */
  message?: string
}

/**
 * 启动并流式执行指定的智能财务工作流 (Server-Sent Events)
 * @route POST /api/ai/workflows/run
 * @param params 运行工作流参数
 * @param params.workflow_id 工作流标识 (如 wf_monthly_review, wf_portfolio_rebalance)
 * @param params.session_id 绑定的对话会话 ID (可选)
 * @param params.params 工作流自定义入参 (如月份、资产类别、预警阈值等)
 * @param signal 中断请求的 AbortSignal
 * @returns 异步可迭代的工作流流式分块生成器 AsyncIterable<WorkflowStreamChunk>
 */
export async function* runWorkflowStream(
  params: {
    workflow_id: string
    session_id?: number
    params?: Record<string, unknown>
  },
  signal?: AbortSignal
): AsyncIterable<WorkflowStreamChunk> {
  let token = ''
  try {
    const auth = useAuthStore()
    token = auth.token || ''
  } catch {
    // outside reactive context
  }
  if (!token) {
    token = localStorage.getItem('token') || ''
  }
  const csrf = getCookie('csrf_token')
  const headers: Record<string, string> = {
    'Content-Type': 'application/json',
    'Authorization': `Bearer ${token}`,
  }
  if (csrf) {
    headers['X-CSRF-Token'] = csrf
  }
  const url = buildApiUrl('/ai/workflows/run')
  const resp = await fetch(url, {
    method: 'POST',
    headers,
    credentials: 'include',
    body: JSON.stringify(params),
    signal,
  })
  if (!resp.ok || !resp.body) throw new Error(`HTTP ${resp.status}`)
  const reader = resp.body.getReader()
  const decoder = new TextDecoder()
  let buffer = ''
  let currentEvent = 'delta'
  try {
    while (true) {
      const { done, value } = await reader.read()
      if (done) break
      buffer += decoder.decode(value, { stream: true })
      const lines = buffer.split('\n')
      buffer = lines.pop() ?? ''
      for (const line of lines) {
        const trimmed = line.trim()
        if (trimmed.startsWith('event:')) {
          currentEvent = trimmed.slice(6).trim()
        } else if (trimmed.startsWith('data:')) {
          const dataStr = trimmed.slice(5).trim()
          if (dataStr) {
            try {
              const parsed = JSON.parse(dataStr) as Record<string, unknown>
              yield {
                type: (currentEvent || 'delta') as WorkflowStreamChunk['type'],
                workflow_id: typeof parsed.workflow_id === 'string' ? parsed.workflow_id : undefined,
                title: typeof parsed.title === 'string' ? parsed.title : undefined,
                total_steps: typeof parsed.total_steps === 'number' ? parsed.total_steps : undefined,
                step_index: typeof parsed.step_index === 'number' ? parsed.step_index : undefined,
                step_id: typeof parsed.step_id === 'string' ? parsed.step_id : undefined,
                status: typeof parsed.status === 'string' ? parsed.status : undefined,
                summary: typeof parsed.summary === 'string' ? parsed.summary : undefined,
                content: typeof parsed.content === 'string' ? parsed.content : undefined,
                session_id: typeof parsed.session_id === 'number' ? parsed.session_id : undefined,
                message: typeof parsed.message === 'string' ? parsed.message : undefined,
              }
            } catch {
              // ignore
            }
          }
        }
      }
    }
  } finally {
    reader.releaseLock()
  }
}
