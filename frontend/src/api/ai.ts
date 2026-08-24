// frontend/src/api/ai.ts
import http, { getCookie } from '@/utils/http'

export interface AiModelOption {
  provider_id: string
  provider_name: string
  models: string[]
}

export interface AiSession {
  id: number
  user_id: number
  title: string
  model: string
  provider: string
  created_at: string
  updated_at: string
}

export interface AiMessage {
  id: number
  session_id: number
  role: 'user' | 'assistant'
  content: string
  model?: string
  created_at: string
}

export interface AiProviderConfig {
  id: string
  name: string
  base_url: string
  api_key?: string
  api_key_enc?: string
  has_api_key?: boolean
  models: string[]
}
export interface AiSettings {
  providers: AiProviderConfig[]
  default_provider: string
  default_model: string
  context_size: number
  system_prompt: string
}

export async function getModels(): Promise<AiModelOption[]> {
  const r = (await http.get('/ai/models')) as unknown
  if (Array.isArray(r)) return r as AiModelOption[]
  if (r && typeof r === 'object' && 'data' in r && Array.isArray((r as { data: unknown }).data)) {
    return (r as { data: AiModelOption[] }).data
  }
  return []
}

export async function listSessions(params?: { page?: number; page_size?: number }) {
  return http.get('/ai/sessions', { params })
}

export async function createSession(model?: string, provider?: string) {
  return http.post('/ai/sessions', { model, provider })
}

export async function getSession(id: number) {
  return http.get(`/ai/sessions/${id}`)
}

export async function updateSession(id: number, data: { title?: string; model?: string }) {
  return http.put(`/ai/sessions/${id}`, data)
}

export async function deleteSession(id: number) {
  return http.delete(`/ai/sessions/${id}`)
}

export async function getMessages(sessionId: number, page = 1, page_size = 50) {
  return http.get(`/ai/sessions/${sessionId}/messages`, { params: { page, page_size } })
}

export interface ChatStreamChunk {
  type: 'delta' | 'done' | 'error'
  content?: string
  finish_reason?: string
  message?: string
}

export async function* chatStream(params: {
  session_id?: number
  content: string
  model?: string
  provider?: string
  regenerate?: boolean
}): AsyncIterable<ChatStreamChunk> {
  const token = localStorage.getItem('token') || ''
  const csrf = getCookie('csrf_token')
  const headers: Record<string, string> = {
    'Content-Type': 'application/json',
    'Authorization': `Bearer ${token}`,
  }
  if (csrf) {
    headers['X-CSRF-Token'] = csrf
  }
  const resp = await fetch('/api/ai/chat', {
    method: 'POST',
    headers,
    credentials: 'include',
    body: JSON.stringify(params),
  })
  if (!resp.ok || !resp.body) throw new Error(`HTTP ${resp.status}`)
  const reader = resp.body.getReader()
  const decoder = new TextDecoder()
  let buffer = ''
  let currentEvent = 'delta'
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
}

export async function getSettings(): Promise<AiSettings> {
  const r = (await http.get('/settings/ai')) as unknown
  if (r && typeof r === 'object' && 'data' in r && (r as { data: unknown }).data) {
    return (r as { data: AiSettings }).data
  }
  return r as AiSettings
}
export async function updateSettings(settings: Partial<AiSettings>) {
  return http.put('/settings/ai', settings)
}

export interface AiTestConnectionResult {
  success: boolean
  latency_ms: number
  message: string
}

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
