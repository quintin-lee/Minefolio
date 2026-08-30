// frontend/src/stores/chat.ts
import { defineStore } from 'pinia'
import { ref, reactive } from 'vue'
import {
  listSessions, createSession, deleteSession, getSession, updateSession,
  getMessages, chatStream, getModels, getSettings, getWorkflows, runWorkflowStream,
} from '@/api/ai'
import type { AiMessage, AiSession, AiModelOption, AiSettings, WorkflowStreamChunk } from '@/api/ai'
import type { WorkflowDef, WorkflowRunState, WorkflowStepState } from '@/types'

export const useChatStore = defineStore('chat', () => {
  const sessions = ref<AiSession[]>([])
  const currentSessionId = ref<number | null>(null)
  const messages = ref<AiMessage[]>([])
  const isStreaming = ref(false)
  const currentModel = ref('')
  const currentProvider = ref('')
  const availableModels = ref<AiModelOption[]>([])
  const settings = ref<AiSettings | null>(null)
  const workflows = ref<WorkflowDef[]>([])
  const activeWorkflow = ref<WorkflowRunState | null>(null)

  // Infinite scroll pagination state for messages
  const messageTotal = ref(0)
  const loadedMessagePage = ref(1)
  const loadingMoreMessages = ref(false)

  // Enable typewriter buffer: RAF drains chars at fixed 3/frame during streaming,
  // then LRU cache + debounce used for non-streaming final content to avoid
  // repeated marked.parse / DOMPurify on every reactive push.
  const enableTypewriterBuffer = ref(true)

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
    messageTotal.value = 0
    loadedMessagePage.value = 1
    const r = (await getMessages(numId, 1, 50)) as unknown
    const rawList = r && typeof r === 'object' && 'list' in r && Array.isArray((r as { list: unknown }).list) ? (r as { list: Record<string, unknown>[] }).list : []
    const total = (r && typeof r === 'object' && 'total' in r) ? Number((r as { total: unknown }).total) : (rawList.length ?? 0)
    messageTotal.value = total
    messages.value = rawList.map((m) => ({
      id: Number(m.id),
      session_id: Number(m.session_id),
      role: (m.role === 'user' ? 'user' : 'assistant') as 'user' | 'assistant',
      content: String(m.content || ''),
      created_at: String(m.created_at || ''),
    }))
  }

  async function loadMoreMessages() {
    if (loadingMoreMessages.value || !currentSessionId.value) return
    const nextPage = loadedMessagePage.value + 1
    if (messageTotal.value > 0 && nextPage * 50 >= messageTotal.value) return
    loadingMoreMessages.value = true
    try {
      const r = (await getMessages(currentSessionId.value, nextPage, 50)) as unknown
      const rawList = r && typeof r === 'object' && 'list' in r && Array.isArray((r as { list: unknown }).list) ? (r as { list: Record<string, unknown>[] }).list : []
      const total = (r && typeof r === 'object' && 'total' in r) ? Number((r as { total: unknown }).total) : (rawList.length ?? 0)
      messageTotal.value = total
      loadedMessagePage.value = nextPage
      const newMsgs = rawList.map((m) => ({
        id: Number(m.id),
        session_id: Number(m.session_id),
        role: (m.role === 'user' ? 'user' : 'assistant') as 'user' | 'assistant',
        content: String(m.content || ''),
        created_at: String(m.created_at || ''),
      }))
      // Prepend older messages (they come newest-first from backend)
      messages.value = [...newMsgs.reverse(), ...messages.value]
    } finally {
      loadingMoreMessages.value = false
    }
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

  // Renders streamed deltas at a steady cadence decoupled from the bursty
  // arrival pattern of SSE-over-TCP: network chunks enqueue here, a RAF
  // loop drains characters adaptively (faster when backlogged) into a
  // reactive message so the UI types smoothly instead of jumping per
  // network batch. RAF pauses in hidden tabs and aligns with vsync.
  class SmoothStreamWriter {
    private buf = ''
    private rafId: number | null = null
    private running = true
    private closed = false

    constructor(private readonly target: AiMessage) {
      this.schedule()
    }

    push(text: string) {
      if (this.closed) return
      this.buf += text
      if (this.rafId === null && this.running) this.schedule()
    }

    private schedule() {
      if (!this.running || this.closed) return
      if (this.rafId !== null) return
      this.rafId = requestAnimationFrame(() => this.tick())
    }

    private tick() {
      this.rafId = null
      if (!this.buf) return
      // Fixed step: ~5 chars/frame ≈ 300 chars/sec at 60fps — natural typing speed.
      // A capping ceiling prevents a single frame from jumping >16 chars even after
      // a large burst, keeping the visual feel consistently smooth.
      const step = Math.min(5, this.buf.length)
      this.target.content += this.buf.slice(0, step)
      this.buf = this.buf.slice(step)
      if (this.buf && this.running) this.schedule()
    }

    async finish(): Promise<void> {
      // Re-schedule draining if idle but buffer still pending
      if (this.buf && this.rafId === null && this.running) this.schedule()
      await new Promise<void>((resolve) => {
        const check = () => {
          if (!this.buf) resolve()
          else requestAnimationFrame(check)
        }
        check()
      })
      this.stop()
    }

    flushNow() {
      if (this.buf) {
        this.target.content += this.buf
        this.buf = ''
      }
      this.stop()
    }

    private stop() {
      this.closed = true
      this.running = false
      if (this.rafId !== null) {
        cancelAnimationFrame(this.rafId)
        this.rafId = null
      }
    }
  }

  let activeAbortController: AbortController | null = null

  function abortCurrentStream() {
    if (activeAbortController) {
      activeAbortController.abort()
      activeAbortController = null
    }
    isStreaming.value = false
  }

  function clearMessages() {
    abortCurrentStream()
    messages.value = []
  }

  function clearCurrentSession() {
    abortCurrentStream()
    currentSessionId.value = null
    messages.value = []
  }

  function resetState() {
    abortCurrentStream()
    sessions.value = []
    currentSessionId.value = null
    messages.value = []
    isStreaming.value = false
  }

  function setModel(model: string, provider?: string) {
    currentModel.value = model
    if (provider !== undefined) {
      currentProvider.value = provider
    }
  }

  async function editAndResend(targetIdx: number, newContent: string) {
    const trimmed = newContent.trim()
    if (!trimmed || isStreaming.value) return
    if (targetIdx < 0 || targetIdx >= messages.value.length) return
    const target = messages.value[targetIdx]
    if (!target || target.role !== 'user') return
    messages.value = messages.value.slice(0, targetIdx + 1)
    messages.value[targetIdx]!.content = trimmed

    const assistantId = Date.now() + 1
    let assistantMsg: AiMessage = {
      id: assistantId,
      session_id: currentSessionId.value!,
      role: 'assistant',
      content: '',
      created_at: new Date().toISOString(),
    }
    messages.value.push(assistantMsg)
    assistantMsg = messages.value[messages.value.length - 1]!
    abortCurrentStream()
    activeAbortController = new AbortController()
    const signal = activeAbortController.signal
    enableTypewriterBuffer.value = true
    isStreaming.value = true
    const writer = new SmoothStreamWriter(assistantMsg)
    try {
      for await (const chunk of chatStream({
        session_id: currentSessionId.value ?? undefined,
        content: trimmed,
        model: currentModel.value,
        provider: currentProvider.value,
      }, signal)) {
        if (chunk.type === 'delta' && chunk.content) writer.push(chunk.content)
        else if (chunk.type === 'error') { writer.flushNow(); assistantMsg.content = `⚠️ ${chunk.message}` }
      }
      await writer.finish()
    } catch (err: any) {
      if (err?.name !== 'AbortError') { writer.flushNow(); assistantMsg.content = `⚠️ 请求发生异常` }
    } finally { writer.flushNow(); enableTypewriterBuffer.value = false; isStreaming.value = false; activeAbortController = null }
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

    abortCurrentStream()
    activeAbortController = new AbortController()
    const signal = activeAbortController.signal

    enableTypewriterBuffer.value = true
    isStreaming.value = true
    const writer = new SmoothStreamWriter(assistantMsg)

    try {
      for await (const chunk of chatStream({
        session_id: currentSessionId.value ?? undefined,
        content: userMsg.content,
        model: currentModel.value,
        provider: currentProvider.value,
        regenerate: true,
      }, signal)) {
        if (chunk.type === 'delta' && chunk.content) {
          writer.push(chunk.content)
        } else if (chunk.type === 'error') {
          writer.flushNow()
          assistantMsg.content = `⚠️ ${chunk.message}`
        }
      }
      await writer.finish()
    } catch (err: any) {
      if (err?.name !== 'AbortError') {
        writer.flushNow()
        assistantMsg.content = `⚠️ 请求发生异常`
      }
    } finally {
      writer.flushNow()
      enableTypewriterBuffer.value = false
      isStreaming.value = false
      activeAbortController = null
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

    abortCurrentStream()
    activeAbortController = new AbortController()
    const signal = activeAbortController.signal

    enableTypewriterBuffer.value = true
    isStreaming.value = true
    const writer = new SmoothStreamWriter(assistantMsg)

    try {
      for await (const chunk of chatStream({
        session_id: currentSessionId.value ?? undefined,
        content,
        model: currentModel.value,
        provider: currentProvider.value,
      }, signal)) {
        if (chunk.type === 'delta' && chunk.content) {
          writer.push(chunk.content)
        } else if (chunk.type === 'error') {
          writer.flushNow()
          assistantMsg.content = `⚠️ ${chunk.message}`
        }
      }
      await writer.finish()

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
    } catch (err: any) {
      if (err?.name !== 'AbortError') {
        writer.flushNow()
        assistantMsg.content = `⚠️ 请求发生异常`
      }
    } finally {
      writer.flushNow()
      enableTypewriterBuffer.value = false
      isStreaming.value = false
      activeAbortController = null
    }
  }
  async function fetchWorkflowsList() {
    try {
      const list = await getWorkflows()
      workflows.value = list
    } catch {
      // ignore
    }
  }

  async function runWorkflow(workflowId: string, params?: Record<string, unknown>) {
    if (isStreaming.value) return
    const targetWf = workflows.value.find(w => w.id === workflowId)
    const wfTitle = targetWf ? targetWf.title : '智能财务工作流'

    const sid = currentSessionId.value
    if (!sid) await createNewSession()

    // 1. Add user prompt message
    const userMsg: AiMessage = {
      id: Date.now(),
      session_id: currentSessionId.value!,
      role: 'user',
      content: `🪄 启动工作流：【${wfTitle}】`,
      created_at: new Date().toISOString(),
    }
    messages.value.push(userMsg)

    // 2. Initialize workflow state
    const initialSteps: WorkflowStepState[] = targetWf
      ? targetWf.steps.map((st, idx) => ({
          step_index: idx,
          step_id: st.step_id,
          title: st.title,
          status: 'pending' as const,
        }))
      : []

    const wfState: WorkflowRunState = reactive({
      workflow_id: workflowId,
      title: wfTitle,
      total_steps: targetWf ? targetWf.step_count : 4,
      status: 'running',
      steps: initialSteps,
    })
    activeWorkflow.value = wfState

    // 3. Add assistant message with workflowData
    const assistantId = Date.now() + 1
    const assistantMsg: AiMessage = reactive({
      id: assistantId,
      session_id: currentSessionId.value!,
      role: 'assistant',
      content: '',
      workflowData: wfState,
      created_at: new Date().toISOString(),
    })
    messages.value.push(assistantMsg)
    const activeMsg = messages.value[messages.value.length - 1]!

    abortCurrentStream()
    activeAbortController = new AbortController()
    const signal = activeAbortController.signal

    enableTypewriterBuffer.value = true
    isStreaming.value = true
    const writer = new SmoothStreamWriter(activeMsg)

    try {
      for await (const chunk of runWorkflowStream(
        {
          workflow_id: workflowId,
          session_id: currentSessionId.value ?? undefined,
          params,
        },
        signal
      )) {
        if (chunk.type === 'workflow_start') {
          if (chunk.title) wfState.title = chunk.title
          if (chunk.total_steps) wfState.total_steps = chunk.total_steps
        } else if (chunk.type === 'step_start' && typeof chunk.step_index === 'number') {
          const idx = chunk.step_index
          while (wfState.steps.length <= idx) {
            wfState.steps.push({
              step_index: wfState.steps.length,
              step_id: chunk.step_id || `step_${wfState.steps.length}`,
              title: chunk.title || `步骤 ${wfState.steps.length + 1}`,
              status: 'pending',
            })
          }
          const curStep = wfState.steps[idx]
          if (curStep) {
            curStep.status = 'running'
            if (chunk.title) curStep.title = chunk.title
          }
        } else if (chunk.type === 'step_complete' && typeof chunk.step_index === 'number') {
          const idx = chunk.step_index
          const curStep = wfState.steps[idx]
          if (curStep) {
            curStep.status = 'completed'
            if (chunk.summary) curStep.summary = chunk.summary
          }
        } else if (chunk.type === 'delta' && chunk.content) {
          writer.push(chunk.content)
        } else if (chunk.type === 'workflow_complete') {
          wfState.status = 'completed'
          wfState.steps.forEach(st => {
            if (st.status !== 'completed' && st.status !== 'error') {
              st.status = 'completed'
            }
          })
        } else if (chunk.type === 'error') {
          wfState.status = 'error'
          writer.flushNow()
          activeMsg.content = `⚠️ ${chunk.message || '工作流执行失败'}`
        }
      }
      await writer.finish()
      wfState.status = 'completed'
      wfState.steps.forEach(st => {
        if (st.status !== 'completed' && st.status !== 'error') {
          st.status = 'completed'
        }
      })

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
    } catch (err: any) {
      if (err?.name !== 'AbortError') {
        wfState.status = 'error'
        writer.flushNow()
        activeMsg.content = `⚠️ 工作流执行发生异常`
      }
    } finally {
      writer.flushNow()
      if (wfState.status === 'running') {
        wfState.status = 'completed'
        wfState.steps.forEach(st => {
          if (st.status !== 'completed' && st.status !== 'error') {
            st.status = 'completed'
          }
        })
      }
      enableTypewriterBuffer.value = false
      isStreaming.value = false
      activeAbortController = null
    }
  }

  function switchModel(model: string, provider: string) {
    currentModel.value = model
    currentProvider.value = provider
  }

  return {
    sessions, currentSessionId, messages, isStreaming,
    currentModel, currentProvider, availableModels, settings,
    workflows, activeWorkflow,
    fetchSessions, fetchModels, fetchSettings, fetchWorkflowsList,
    createNewSession, selectSession, deleteSessionById, renameSession,
    sendMessage, regenerateLastMessage, editAndResend, runWorkflow, switchModel,
    abortCurrentStream, clearMessages, clearCurrentSession, resetState, setModel,
    loadMoreMessages,
    messageTotal, loadedMessagePage, loadingMoreMessages,
    enableTypewriterBuffer,
  }
})
