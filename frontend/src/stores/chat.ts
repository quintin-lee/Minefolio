// frontend/src/stores/chat.ts
import { defineStore } from 'pinia'
import { ref } from 'vue'
import {
  listSessions, createSession, deleteSession, getSession,
  getMessages, chatStream, getModels, getSettings,
} from '@/api/ai'
import type { AiMessage, AiSession, AiModelOption, AiSettings } from '@/api/ai'

export const useChatStore = defineStore('chat', () => {
  const sessions = ref<AiSession[]>([])
  const currentSessionId = ref<number | null>(null)
  const messages = ref<AiMessage[]>([])
  const isStreaming = ref(false)
  const currentModel = ref('')
  const currentProvider = ref('')
  const availableModels = ref<AiModelOption[]>([])
  const settings = ref<AiSettings | null>(null)

  async function fetchSessions() {
    const r = (await listSessions({ page_size: 50 })) as unknown
    const rawList = r && typeof r === 'object' && 'list' in r && Array.isArray((r as { list: unknown }).list) ? (r as { list: Record<string, unknown>[] }).list : []
    sessions.value = rawList.map((s) => ({
      id: Number(s.id),
      user_id: Number(s.user_id),
      title: String(s.title || '新对话'),
      model: String(s.model || ''),
      provider: String(s.provider || ''),
      created_at: String(s.created_at || ''),
      updated_at: String(s.updated_at || ''),
    }))
  }
  async function fetchModels() {
    const r = await getModels()
    availableModels.value = r
  }

  async function fetchSettings() {
    const r = await getSettings() as AiSettings
    settings.value = r
    if (!currentModel.value && r)
      currentModel.value = r.default_model
  }

  async function createNewSession() {
    const r = (await createSession(currentModel.value, currentProvider.value)) as unknown
    const raw = (r && typeof r === 'object' && 'data' in r ? (r as { data: Record<string, unknown> }).data : r) as Record<string, unknown>
    const session: AiSession = {
      id: Number(raw?.id || Date.now()),
      user_id: Number(raw?.user_id || 1),
      title: String(raw?.title || '新对话'),
      model: String(raw?.model || currentModel.value),
      provider: String(raw?.provider || currentProvider.value),
      created_at: String(raw?.created_at || new Date().toISOString()),
      updated_at: String(raw?.updated_at || new Date().toISOString()),
    }
    sessions.value.unshift(session)
    currentSessionId.value = session.id
    messages.value = []
    return session
  }

  async function selectSession(id: number) {
    const numId = Number(id)
    currentSessionId.value = numId
    const r = (await getMessages(numId)) as unknown
    const rawList = r && typeof r === 'object' && 'list' in r && Array.isArray((r as { list: unknown }).list) ? (r as { list: Record<string, unknown>[] }).list : []
    messages.value = rawList.map((m) => ({
      id: Number(m.id),
      session_id: Number(m.session_id),
      role: (m.role === 'user' ? 'user' : 'assistant') as 'user' | 'assistant',
      content: String(m.content || ''),
      created_at: String(m.created_at || ''),
    }))
  }

  async function deleteSessionById(id: number) {
    const numId = Number(id)
    await deleteSession(numId)
    sessions.value = sessions.value.filter(s => Number(s.id) !== numId)
    if (Number(currentSessionId.value) === numId) {
      currentSessionId.value = null
      messages.value = []
    }
  }

  async function sendMessage(content: string) {
    if (!content.trim() || isStreaming.value) return
    const sid = currentSessionId.value
    if (!sid) await createNewSession()

    const userMsg: AiMessage = {
      id: Date.now(),
      session_id: currentSessionId.value!,
      role: 'user',
      content,
      created_at: new Date().toISOString(),
    }
    messages.value.push(userMsg)

    const assistantId = Date.now() + 1
    messages.value.push({
      id: assistantId,
      session_id: currentSessionId.value!,
      role: 'assistant',
      content: '',
      created_at: new Date().toISOString(),
    })

    isStreaming.value = true
    let assembled = ''

    try {
      for await (const chunk of chatStream({
        session_id: currentSessionId.value ?? undefined,
        content,
        model: currentModel.value,
        provider: currentProvider.value,
      })) {
        if (chunk.type === 'delta' && chunk.content) {
          assembled += chunk.content
          const last = messages.value[messages.value.length - 1]
          if (last) last.content = assembled
        } else if (chunk.type === 'error') {
          const last = messages.value[messages.value.length - 1]
          if (last) last.content = `⚠️ ${chunk.message}`
        }
      }
      // Refresh session info
      if (currentSessionId.value) {
        const r = (await getSession(currentSessionId.value)) as unknown
        const raw = (r && typeof r === 'object' && 'data' in r ? (r as { data: Record<string, unknown> }).data : r) as Record<string, unknown>
        if (raw && raw.id) {
          const numId = Number(raw.id)
          const idx = sessions.value.findIndex(s => Number(s.id) === numId)
          const existing = idx >= 0 ? sessions.value[idx] : undefined
          if (existing) {
            sessions.value[idx] = {
              ...existing,
              title: String(raw.title || existing.title),
              updated_at: String(raw.updated_at || new Date().toISOString()),
            }
          }
        }
      }
    } finally {
      isStreaming.value = false
    }
  }
  function switchModel(model: string, provider: string) {
    currentModel.value = model
    currentProvider.value = provider
  }

  return {
    sessions, currentSessionId, messages, isStreaming,
    currentModel, currentProvider, availableModels, settings,
    fetchSessions, fetchModels, fetchSettings,
    createNewSession, selectSession, deleteSessionById,
    sendMessage, switchModel,
  }
})
