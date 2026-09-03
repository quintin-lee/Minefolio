/**
 * @file AI 智能财务助理与工作流对话状态管理 Store
 * @description 管理多轮 AI 对话会话、流式打字机渲染 (SSE Smooth Typing Buffer)、历史消息分页加载、智能财务工作流调度与卡片交互
 */

// frontend/src/stores/chat.ts
import { defineStore } from 'pinia'
import { ref, reactive } from 'vue'
import {
  listSessions, createSession, deleteSession, getSession, updateSession,
  getMessages, chatStream, getModels, getSettings, getWorkflows, runWorkflowStream,
} from '@/api/ai'
import type { AiMessage, AiSession, AiModelOption, AiSettings, WorkflowStreamChunk } from '@/api/ai'
import type { WorkflowDef, WorkflowRunState, WorkflowStepState } from '@/types'
import { SmoothStreamWriter } from '@/utils/typewriter'

/**
 * AI 对话与智能工作流 Pinia Store
 */
export const useChatStore = defineStore('chat', () => {
  /** 历史对话会话列表 */
  const sessions = ref<AiSession[]>([])
  /** 当前选中的会话 ID */
  const currentSessionId = ref<number | null>(null)
  /** 当前会话的消息列表 */
  const messages = ref<AiMessage[]>([])
  /** 是否正在处于 AI 流式响应生成中 */
  const isStreaming = ref(false)
  /** 当前选中的模型名称 (如 'gpt-4o', 'claude-3-5-sonnet') */
  const currentModel = ref('')
  /** 当前选中的模型提供商 (如 'openai', 'anthropic') */
  const currentProvider = ref('')
  /** 系统可用的模型提供商及模型列表 */
  const availableModels = ref<AiModelOption[]>([])
  /** AI 系统全局配置 */
  const settings = ref<AiSettings | null>(null)
  /** 系统中已注册的智能工作流定义列表 */
  const workflows = ref<WorkflowDef[]>([])
  /** 当前正在执行的工作流运行时状态数据 */
  const activeWorkflow = ref<WorkflowRunState | null>(null)

  /** LocalStorage 存储置顶固定的工作流 ID 键名 */
  const PINNED_STORAGE_KEY = 'minefolio_pinned_workflows'
  /** 默认置顶的工作流 ID 列表 */
  const DEFAULT_PINNED_IDS = ['wf_monthly_review', 'wf_portfolio_rebalance', 'wf_payday_split', 'wf_budget_guard']
  /** 用户置顶固定的工作流 ID 响应式列表 */
  const pinnedWorkflowIds = ref<string[]>(
    (() => {
      try {
        const raw = localStorage.getItem(PINNED_STORAGE_KEY)
        return raw ? JSON.parse(raw) : DEFAULT_PINNED_IDS
      } catch {
        return DEFAULT_PINNED_IDS
      }
    })()
  )

  /**
   * 切换指定工作流的置顶固定状态
   * @param id 工作流 ID
   */
  function togglePinWorkflow(id: string) {
    const idx = pinnedWorkflowIds.value.indexOf(id)
    if (idx >= 0) {
      pinnedWorkflowIds.value.splice(idx, 1)
    } else {
      pinnedWorkflowIds.value.push(id)
    }
    localStorage.setItem(PINNED_STORAGE_KEY, JSON.stringify(pinnedWorkflowIds.value))
  }

  /** 当前会话的历史消息总条数 */
  const messageTotal = ref(0)
  /** 已加载的历史消息分页页码 */
  const loadedMessagePage = ref(1)
  /** 是否正在向上滚动加载更多历史消息 */
  const loadingMoreMessages = ref(false)

  /**
   * 打字机平滑渲染缓冲区开关：
   * 在流式输出期间通过 requestAnimationFrame (RAF) 以固定速率输出文字，
   * 避免网络突发数据包造成画面跳跃并大幅减少频繁 Markdown 解析的性能损耗
   */
  const enableTypewriterBuffer = ref(true)

  /**
   * 从服务端获取会话历史列表 (默认最新 50 条)
   */
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

  /**
   * 从服务端拉取可用模型选项列表
   */
  async function fetchModels() {
    const r = await getModels()
    availableModels.value = r
  }

  /**
   * 从服务端拉取 AI 全局设置并初始化默认模型/提供商
   */
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

  /**
   * 新建并激活一个空的对话会话
   * @returns 新建的会话对象
   */
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

  /**
   * 选中并切换到指定的对话会话，加载其首屏消息列表
   * @param id 会话 ID
   */
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

  /**
   * 向上无限滚动加载更多历史消息 (下一页)
   */
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

  /**
   * 根据 ID 删除指定的对话会话
   * @param id 会话 ID
   */
  async function deleteSessionById(id: number) {
    const numId = Number(id)
    await deleteSession(numId)
    sessions.value = sessions.value.filter(s => Number(s.id) !== numId)
    if (Number(currentSessionId.value) === numId) {
      currentSessionId.value = null
      messages.value = []
    }
  }

  /**
   * 重命名指定会话的标题
   * @param id 会话 ID
   * @param newTitle 新标题
   */
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



  /** 当前活动的流式请求中断控制器 */
  let activeAbortController: AbortController | null = null

  /**
   * 中断并终止当前正在进行的流式 AI 生成
   */
  function abortCurrentStream() {
    if (activeAbortController) {
      activeAbortController.abort()
      activeAbortController = null
    }
    isStreaming.value = false
  }

  /**
   * 清空当前会话显示的所有消息
   */
  function clearMessages() {
    abortCurrentStream()
    messages.value = []
  }

  /**
   * 清除当前选中的会话上下文
   */
  function clearCurrentSession() {
    abortCurrentStream()
    currentSessionId.value = null
    messages.value = []
  }

  /**
   * 完全重置 Store 所有状态 (如退出登录时调用)
   */
  function resetState() {
    abortCurrentStream()
    sessions.value = []
    currentSessionId.value = null
    messages.value = []
    isStreaming.value = false
  }

  /**
   * 设置当前选中的模型与提供商
   * @param model 模型名称
   * @param provider 提供商标识 (可选)
   */
  function setModel(model: string, provider?: string) {
    currentModel.value = model
    if (provider !== undefined) {
      currentProvider.value = provider
    }
  }

  /**
   * 编辑历史某一条用户提问并重新发起对话 (截断该条之后的所有后续消息)
   * @param targetIdx 目标用户消息在数组中的索引位置
   * @param newContent 编辑修改后的新提示词文本
   */
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

  /**
   * 重新生成最近一次 AI 回答
   */
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

    // 移除用户提问之后的所有后续消息
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

  /**
   * 发送新的用户消息并开启流式打字机接收 AI 回答
   * @param content 用户提问内容文本
   */
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

      // 刷新会话信息 (后端可能自动根据内容生成标题)
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

  /**
   * 从服务端获取所有预定义的智能工作流列表
   */
  async function fetchWorkflowsList() {
    try {
      const list = await getWorkflows()
      workflows.value = list
    } catch {
      // ignore
    }
  }

  /**
   * 直接启动并执行一个指定的智能财务工作流
   * @param workflowId 工作流 ID
   * @param params 附加参数
   */
  async function runWorkflow(workflowId: string, params?: Record<string, unknown>) {
    if (isStreaming.value) return
    const targetWf = workflows.value.find(w => w.id === workflowId)
    const wfTitle = targetWf ? targetWf.title : '智能财务工作流'

    const sid = currentSessionId.value
    if (!sid) await createNewSession()

    // 1. 添加用户提示消息
    const userMsg: AiMessage = {
      id: Date.now(),
      session_id: currentSessionId.value!,
      role: 'user',
      content: `🪄 启动工作流：【${wfTitle}】`,
      created_at: new Date().toISOString(),
    }
    messages.value.push(userMsg)

    // 2. 初始化工作流步骤状态
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

    // 3. 添加带有 workflowData 的 AI 消息
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

      // 刷新会话信息
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

  /**
   * 在消息流中暂存/插入一个待配置的工作流交互卡片
   * @param workflowId 工作流 ID
   * @param initialParams 初始默认入参
   */
  async function stageWorkflow(workflowId: string, initialParams?: Record<string, unknown>) {
    if (isStreaming.value) return
    const targetWf = workflows.value.find(w => w.id === workflowId)
    if (!targetWf) return
    const sid = currentSessionId.value
    if (!sid) await createNewSession()

    const configMsg: AiMessage = reactive({
      id: Date.now(),
      session_id: currentSessionId.value!,
      role: 'assistant',
      content: '',
      workflowConfig: {
        workflow_id: targetWf.id,
        title: targetWf.title,
        icon: targetWf.icon || 'ph:sparkle',
        description: targetWf.description,
        initialParams,
      },
      created_at: new Date().toISOString(),
    })
    messages.value.push(configMsg)
  }

  /**
   * 取消并移除暂存的工作流卡片消息
   * @param messageId 消息 ID
   */
  function cancelStagedWorkflow(messageId: number) {
    const idx = messages.value.findIndex(m => m.id === messageId)
    if (idx !== -1) {
      messages.value.splice(idx, 1)
    }
  }

  /**
   * 从消息流中的内联工作流卡片启动执行流水线
   * @param messageId 含有 workflowConfig 的消息 ID
   * @param params 用户在卡片中填写的表单参数
   */
  async function startStagedWorkflow(messageId: number, params?: Record<string, unknown>) {
    const msg = messages.value.find(m => m.id === messageId)
    if (!msg || !msg.workflowConfig || isStreaming.value) return
    const workflowId = msg.workflowConfig.workflow_id
    const targetWf = workflows.value.find(w => w.id === workflowId)
    const wfTitle = targetWf ? targetWf.title : msg.workflowConfig.title

    // 1. 初始化步骤并就地替换卡片消息
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

    delete msg.workflowConfig
    msg.workflowData = wfState
    activeWorkflow.value = wfState

    abortCurrentStream()
    activeAbortController = new AbortController()
    const signal = activeAbortController.signal

    enableTypewriterBuffer.value = true
    isStreaming.value = true
    const writer = new SmoothStreamWriter(msg)

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
          msg.content = `⚠️ ${chunk.message || '工作流执行失败'}`
        }
      }
      await writer.finish()
      wfState.status = 'completed'
      wfState.steps.forEach(st => {
        if (st.status !== 'completed' && st.status !== 'error') {
          st.status = 'completed'
        }
      })

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
        msg.content = `⚠️ 工作流执行发生异常`
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

  /**
   * 快捷切换当前使用的模型与提供商
   * @param model 模型名称
   * @param provider 提供商标识
   */
  function switchModel(model: string, provider: string) {
    currentModel.value = model
    currentProvider.value = provider
  }

  return {
    sessions,
    currentSessionId,
    messages,
    isStreaming,
    currentModel,
    currentProvider,
    availableModels,
    settings,
    workflows,
    activeWorkflow,
    pinnedWorkflowIds,
    fetchSessions,
    fetchModels,
    fetchSettings,
    fetchWorkflowsList,
    createNewSession,
    selectSession,
    deleteSessionById,
    renameSession,
    sendMessage,
    regenerateLastMessage,
    editAndResend,
    runWorkflow,
    switchModel,
    abortCurrentStream,
    clearMessages,
    clearCurrentSession,
    resetState,
    setModel,
    loadMoreMessages,
    togglePinWorkflow,
    stageWorkflow,
    cancelStagedWorkflow,
    startStagedWorkflow,
    messageTotal,
    loadedMessagePage,
    loadingMoreMessages,
    enableTypewriterBuffer,
  }
})

