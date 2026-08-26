// frontend/src/stores/chat.ts
import { defineStore } from 'pinia'
import { ref } from 'vue'
import {
  listSessions, createSession, deleteSession, getSession, updateSession,
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
    try {
      const r = (await getSettings()) as unknown
      const s = (r && typeof r === 'object' && 'data' in r ? (r as { data: AiSettings }).data : r) as AiSettings
      if (s) {
        settings.value = s
        if (!currentProvider.value && s.default_provider) {
          currentProvider.value = s.default_provider
        }
        if (!currentModel.value && s.default_model) {
          currentModel.value = s.default_model
        }
      }
    } catch {
      // ignore
    }
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

  async function renameSession(id: number, newTitle: string) {
    const numId = Number(id)
    const trimmed = newTitle.trim()
    if (!trimmed) return
    await updateSession(numId, { title: trimmed })
    const idx = sessions.value.findIndex(s => Number(s.id) === numId)
    if (idx >= 0) {
      const existing = sessions.value[idx]!
      sessions.value[idx] = {
        ...existing,
        title: trimmed,
      }
    }
  }

  async function regenerateLastMessage() {
    if (isStreaming.value || messages.value.length === 0) return
    let lastUserIdx = -1
    for (let i = messages.value.length - 1; i >= 0; i--) {
      if (messages.value[i]?.role === 'user') {
        lastUserIdx = i
        break
      }
    }
    if (lastUserIdx < 0) return
    const userMsg = messages.value[lastUserIdx]!

    // Remove trailing messages after user message
    messages.value = messages.value.slice(0, lastUserIdx + 1)

    const assistantId = Date.now() + 1
    let assistantMsg: AiMessage = {
      id: assistantId,
      session_id: currentSessionId.value!,
      role: 'assistant',
      content: '',
      created_at: new Date().toISOString(),
    }
    messages.value.push(assistantMsg)
    // Rebind to the reactive proxy stored in the array: mutating the raw
    // object bypasses Vue reactivity and breaks typewriter rendering.
    assistantMsg = messages.value[messages.value.length - 1]!

    isStreaming.value = true

    try {
      for await (const chunk of chatStream({
        session_id: currentSessionId.value ?? undefined,
        content: userMsg.content,
        model: currentModel.value,
        provider: currentProvider.value,
        regenerate: true,
      })) {
        if (chunk.type === 'delta' && chunk.content) {
          assistantMsg.content += chunk.content
        } else if (chunk.type === 'error') {
          assistantMsg.content = `⚠️ ${chunk.message}`
        }
      }
    } finally {
      isStreaming.value = false
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
    let assistantMsg: AiMessage = {
      id: assistantId,
      session_id: currentSessionId.value!,
      role: 'assistant',
      content: '',
      created_at: new Date().toISOString(),
    }
    messages.value.push(assistantMsg)
    // Rebind to the reactive proxy stored in the array: mutating the raw
    // object bypasses Vue reactivity and breaks typewriter rendering.
    assistantMsg = messages.value[messages.value.length - 1]!

    isStreaming.value = true

    try {
      for await (const chunk of chatStream({
        session_id: currentSessionId.value ?? undefined,
        content,
        model: currentModel.value,
        provider: currentProvider.value,
      })) {
        if (chunk.type === 'delta' && chunk.content) {
          assistantMsg.content += chunk.content
        } else if (chunk.type === 'error') {
          assistantMsg.content = `⚠️ ${chunk.message}`
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
    createNewSession, selectSession, deleteSessionById, renameSession,
    sendMessage, regenerateLastMessage, switchModel,
  }
})
