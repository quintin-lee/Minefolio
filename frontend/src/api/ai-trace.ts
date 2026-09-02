/**
 * @file AI 追踪与可观测性 API 接口模块
 * @description 提供 AI 对话请求追踪、Token 消耗统计、延迟分析及 Trace 详情查询相关接口
 */

import http from '@/utils/http'

/**
 * AI 追踪记录摘要信息
 */
export interface AiTrace {
  /** 追踪记录唯一标识 ID */
  id: number
  /** 所属用户 ID */
  user_id: number
  /** 关联的会话 ID */
  session_id: number
  /** 模型提供商标识 (如 openai, anthropic, ollama 等) */
  provider: string
  /** 使用的模型名称 (如 gpt-4o, claude-3-5-sonnet 等) */
  model: string
  /** Prompt 输入消耗的 Token 数量 */
  prompt_tokens: number
  /** Completion 输出生成的 Token 数量 */
  completion_tokens: number
  /** 消耗的总 Token 数量 (prompt + completion) */
  total_tokens: number
  /** 请求总耗时/延迟 (毫秒) */
  latency_ms: number
  /** 首字生成耗时 (毫秒, TTFT - Time To First Token) */
  first_token_ms: number
  /** 生成速率 (Token/秒) */
  tokens_per_sec: number
  /** 本次调用预估消耗费用 (美元 USD) */
  cost_usd: number
  /** 生成温度参数 Temperature */
  temperature: number
  /** 最大生成 Token 数上限 Max Tokens */
  max_tokens: number
  /** 核采样参数 Top P */
  top_p: number
  /** 追踪状态 ('success' | 'error' | 'interrupted' 等) */
  status: string
  /** 异常错误信息 (若调用失败) */
  error_message: string
  /** 记录创建时间 (ISO 8601 格式字符串) */
  created_at: string
}

/**
 * AI 追踪记录详情 (包含完整消息上下文与元数据)
 */
export interface AiTraceDetail extends AiTrace {
  /** 完整的输入消息列表 JSON 字符串 */
  input_messages: string
  /** 完整的模型输出内容文本 */
  output_content: string
  /** 系统提示词 (System Prompt) */
  system_prompt: string
  /** 扩展元数据 JSON 字符串 (如客户端信息、调用标签等) */
  metadata: string
}

/**
 * AI 追踪全局与聚合统计数据
 */
export interface AiTraceStats {
  /** 追踪记录总数 */
  total_traces: number
  /** 累计消耗的 Token 总量 */
  total_tokens: number
  /** 平均请求延迟 (毫秒) */
  avg_latency_ms: number
  /** 平均首字生成耗时 (毫秒) */
  avg_first_token_ms: number
  /** 平均生成速率 (Token/秒) */
  avg_tokens_per_sec: number
  /** 累计调用总成本 (美元 USD) */
  total_cost_usd: number
}

/**
 * 分页查询 AI 追踪调用记录列表
 * @route GET /api/ai/traces
 * @param params 分页与筛选参数
 * @param params.page 当前页码 (从 1 开始)
 * @param params.page_size 每页数量 (默认 20)
 * @param params.provider 按提供商筛选 (可选)
 * @param params.model 按模型名称筛选 (可选)
 * @returns 分页追踪记录列表响应
 */
export async function listTraces(params?: {
  page?: number
  page_size?: number
  provider?: string
  model?: string
}) {
  return http.get('/ai/traces', { params })
}

/**
 * 获取指定 ID 的 AI 追踪记录详情 (包含完整 Prompt、上下文与输出)
 * @route GET /api/ai/traces/:id
 * @param id 追踪记录 ID
 * @returns 追踪详情对象
 */
export async function getTrace(id: number): Promise<AiTraceDetail> {
  return (await http.get(`/ai/traces/${id}`)) as AiTraceDetail
}

/**
 * 获取 AI 调用的全局统计指标 (总调用次数、Token 消耗、平均耗时与成本)
 * @route GET /api/ai/traces/stats
 * @returns AI 追踪聚合统计指标对象
 */
export async function getTraceStats(): Promise<AiTraceStats> {
  return (await http.get('/ai/traces/stats')) as AiTraceStats
}
