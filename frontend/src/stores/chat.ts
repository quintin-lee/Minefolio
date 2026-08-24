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
    const r = await listSessions({ page_size: 50 }) as any
    sessions.value = r?.list ?? []
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
    const r = await createSession(currentModel.value, currentProvider.value) as any
    const session: AiSession = r.data
    sessions.value.unshift(session)
    currentSessionId.value = session.id
    messages.value = []
    return session
  }

  async function selectSession(id: number) {
    currentSessionId.value = id
    const r = await getMessages(id) as any
    messages.value = r?.list ?? []
  }

  async function deleteSessionById(id: number) {
    await deleteSession(id)
    sessions.value = sessions.value.filter(s => s.id !== id)
    if (currentSessionId.value === id) {
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
        const r = await getSession(currentSessionId.value) as any
        if (r.data) {
          const idx = sessions.value.findIndex(s => s.id === r.data.id)
          if (idx >= 0) sessions.value[idx] = r.data
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
