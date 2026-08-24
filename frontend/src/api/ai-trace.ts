import http from '@/utils/http'

export interface AiTrace {
  id: number
  user_id: number
  session_id: number
  provider: string
  model: string
  prompt_tokens: number
  completion_tokens: number
  total_tokens: number
  latency_ms: number
  first_token_ms: number
  tokens_per_sec: number
  cost_usd: number
  temperature: number
  max_tokens: number
  top_p: number
  status: string
  error_message: string
  created_at: string
}

export interface AiTraceDetail extends AiTrace {
  input_messages: string
  output_content: string
  system_prompt: string
  metadata: string
}

export interface AiTraceStats {
  total_traces: number
  total_tokens: number
  avg_latency_ms: number
  avg_first_token_ms: number
  avg_tokens_per_sec: number
  total_cost_usd: number
}

export async function listTraces(params?: {
  page?: number
  page_size?: number
  provider?: string
  model?: string
}) {
  return http.get('/ai/traces', { params })
}

export async function getTrace(id: number): Promise<AiTraceDetail> {
  const r = (await http.get(`/ai/traces/${id}`)) as unknown
  if (r && typeof r === 'object' && 'data' in r) {
    return (r as { data: AiTraceDetail }).data
  }
  return r as AiTraceDetail
}

export async function getTraceStats(): Promise<AiTraceStats> {
  const r = (await http.get('/ai/traces/stats')) as unknown
  if (r && typeof r === 'object' && 'data' in r) {
    return (r as { data: AiTraceStats }).data
  }
  return r as AiTraceStats
}
