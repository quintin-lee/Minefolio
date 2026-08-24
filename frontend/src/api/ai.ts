// frontend/src/api/ai.ts
import http from '@/utils/http'

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
  models: string[]
  api_key?: string
}

export interface AiSettings {
  providers: AiProviderConfig[]
  default_provider: string
  default_model: string
  context_size: number
  system_prompt: string
}

export async function getModels(): Promise<AiModelOption[]> {
  const r = await http.get('/ai/models') as unknown
  return (r as any)?.data ?? []
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

export async function* chatStream(params: {
  session_id?: number
  content: string
  model?: string
  provider?: string
}): AsyncIterable<{ type: 'delta' | 'done' | 'error'; content?: string; finish_reason?: string; usage?: any; message?: string }> {
  const token = localStorage.getItem('token') || ''
  const resp = await fetch('/api/ai/chat', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'Authorization': `Bearer ${token}`,
    },
    body: JSON.stringify(params),
  })
  if (!resp.ok || !resp.body) throw new Error(`HTTP ${resp.status}`)
  const reader = resp.body.getReader()
  const decoder = new TextDecoder()
  let buffer = ''
  while (true) {
    const { done, value } = await reader.read()
    if (done) break
    buffer += decoder.decode(value, { stream: true })
    const lines = buffer.split('\n')
    buffer = lines.pop() ?? ''
    let event = ''
    let data = ''
    for (const line of lines) {
      if (line.startsWith('event: ')) event = line.slice(7).trim()
      else if (line.startsWith('data: ')) {
        data = line.slice(6).trim()
        if (event && data) {
          try { yield JSON.parse(data) as any } catch { /* ignore */ }
          event = ''
          data = ''
        }
      }
    }
  }
}

export async function getSettings(): Promise<AiSettings> {
  const r = await http.get('/settings/ai')
  return (r as any)?.data as AiSettings
}

export async function updateSettings(settings: Partial<AiSettings>) {
  return http.put('/settings/ai', settings)
}
