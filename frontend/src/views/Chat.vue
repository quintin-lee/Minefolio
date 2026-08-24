<template>
  <div class="chat-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>AI 财务助手</h2>
        <span class="header-badge" v-if="chat.currentModel">
          {{ chat.currentModel }}
        </span>
      </div>
      <div class="header-actions">
        <el-button type="primary" plain class="action-btn" @click="handleNewChat">
          <Icon icon="ph:plus-bold" class="btn-icon" />
          <span>新对话</span>
        </el-button>
        <el-button
          v-if="chat.messages.length > 0"
          text
          class="action-btn"
          @click="handleClearChat"
        >
          <Icon icon="ph:trash" class="btn-icon" />
          <span>清空记录</span>
        </el-button>
      </div>
    </div>

    <div class="chat-layout">
      <!-- Sidebar: session list -->
      <aside class="chat-sidebar" :class="{ 'is-collapsed': !showSidebar }">
        <div class="sidebar-header">
          <div class="sidebar-title">
            <Icon icon="ph:chat-circle-text-bold" />
            <span>历史会话</span>
            <span class="session-count" v-if="chat.sessions.length > 0">({{ chat.sessions.length }})</span>
          </div>
          <button class="icon-close-btn" @click="showSidebar = false" title="收起侧边栏">
            <Icon icon="ph:x" />
          </button>
        </div>
        <div class="session-list" v-loading="loadingSessions">
          <div
            v-for="s in chat.sessions"
            :key="s.id"
            class="session-item"
            :class="{ active: chat.currentSessionId === s.id }"
            @click="selectSession(s.id)"
          >
            <div v-if="editingSessionId === s.id" class="session-rename-box" @click.stop>
              <input
                v-focus
                v-model="renameTitle"
                class="session-rename-input"
                @keydown.enter="saveRename(s.id)"
                @keydown.esc="cancelRename"
                @blur="saveRename(s.id)"
              />
            </div>
            <template v-else>
              <div class="session-item-content">
                <div class="session-title" :title="s.title" @dblclick.stop="startRename(s)">
                  {{ s.title }}
                </div>
                <div class="session-meta">{{ formatTime(s.updated_at) }}</div>
              </div>
              <div class="session-item-actions" @click.stop>
                <button class="item-action-btn" title="重命名" @click.stop="startRename(s)">
                  <Icon icon="ph:pencil-simple" />
                </button>
                <button class="item-action-btn delete-btn" title="删除对话" @click.stop="handleDeleteSession(s.id)">
                  <Icon icon="ph:trash" />
                </button>
              </div>
            </template>
          </div>
          <div v-if="chat.sessions.length === 0 && !loadingSessions" class="empty-sessions">
            <Icon icon="ph:chats" class="empty-sessions-icon" />
            <span>暂无历史会话</span>
          </div>
        </div>
      </aside>

      <!-- Toggle sidebar button -->
      <div class="toggle-sidebar" @click="showSidebar = !showSidebar" title="展开/收起会话列表">
        <Icon :icon="showSidebar ? 'ph:caret-left-bold' : 'ph:caret-right-bold'" />
      </div>

      <!-- Main chat area -->
      <main class="chat-main">
        <!-- Model selector bar -->
        <div class="model-bar">
          <div class="model-selector-wrap">
            <Icon icon="ph:cpu-bold" class="model-icon" />
            <el-select
              v-model="chat.currentModel"
              size="small"
              class="model-select"
              placeholder="选择 AI 模型"
              @change="handleModelChange"
            >
              <el-option
                v-for="m in chat.availableModels"
                :key="m.provider_id + '/' + m.models[0]"
                :value="`${m.provider_id}/${m.models[0]}`"
                :label="`${m.provider_name} (${m.models[0]})`"
              >
                <div class="model-option-item">
                  <span class="opt-name">{{ m.provider_name }}</span>
                  <span class="opt-model">{{ m.models[0] }}</span>
                </div>
              </el-option>
            </el-select>
          </div>
          <div class="chat-status" v-if="chat.isStreaming">
            <span class="status-dot"></span>
            <span>正在深度思考与生成...</span>
          </div>
        </div>

        <!-- Messages stream -->
        <div class="messages" ref="messagesRef" v-loading="loadingMessages">
          <!-- Empty state: Quick prompts -->
          <div v-if="chat.messages.length === 0" class="empty-state">
            <div class="empty-header">
              <div class="empty-icon-wrap">
                <Icon icon="ph:sparkle-fill" class="empty-sparkle" />
              </div>
              <h3>Minefolio AI 智能财务助手</h3>
              <p>基于大语言模型与专业理财知识库，为您提供资产规划、收支诊断与投资分析。</p>
            </div>
            <div class="quick-prompts-grid">
              <div
                v-for="(item, idx) in quickPrompts"
                :key="idx"
                class="prompt-card"
                @click="handleQuickPrompt(item.prompt)"
              >
                <div class="prompt-card-icon">
                  <Icon :icon="item.icon" />
                </div>
                <div class="prompt-card-body">
                  <h4>{{ item.title }}</h4>
                  <p>{{ item.desc }}</p>
                </div>
                <Icon icon="ph:arrow-right" class="prompt-card-arrow" />
              </div>
            </div>
          </div>

          <!-- Message list -->
          <template v-else>
            <div
              v-for="(msg, idx) in chat.messages"
              :key="msg.id"
              class="message-row"
              :class="msg.role"
            >
              <div class="message-avatar">
                <Icon :icon="msg.role === 'user' ? 'ph:user-bold' : 'ph:robot-bold'" />
              </div>
              <div class="message-bubble-wrap">
                <div class="message-content" v-html="renderMarkdown(msg.content)"></div>
                <!-- Assistant Action Toolbar -->
                <div v-if="msg.role === 'assistant' && msg.content && !chat.isStreaming" class="message-actions">
                  <button class="msg-action-btn" title="复制回答" @click="copyText(msg.content)">
                    <Icon icon="ph:copy" />
                    <span>复制</span>
                  </button>
                  <button
                    v-if="idx === chat.messages.length - 1"
                    class="msg-action-btn"
                    title="重新生成"
                    @click="handleRegenerate"
                  >
                    <Icon icon="ph:arrows-counter-clockwise" />
                    <span>重新生成</span>
                  </button>
                </div>
              </div>
            </div>

            <!-- Thinking stream bubble -->
            <div v-if="chat.isStreaming" class="message-row assistant">
              <div class="message-avatar"><Icon icon="ph:robot-bold" /></div>
              <div class="message-bubble-wrap">
                <div class="message-content thinking">
                  <span class="pulse-dot"></span>
                  <span class="pulse-dot"></span>
                  <span class="pulse-dot"></span>
                  <span class="thinking-text">思考中...</span>
                </div>
              </div>
            </div>
          </template>
        </div>

        <!-- Input Area -->
        <div class="chat-input-area">
          <div class="chat-input-box">
            <textarea
              ref="textareaRef"
              v-model="inputText"
              :disabled="chat.isStreaming"
              placeholder="输入财务咨询、收支分析或投资问题... (Enter 发送，Shift+Enter 换行)"
              rows="1"
              @input="adjustTextareaHeight"
              @keydown.enter.exact.prevent="handleSend"
            ></textarea>
            <div class="input-bottom-bar">
              <span class="char-count">{{ inputText.length }} / 2000</span>
              <el-button
                type="primary"
                class="send-btn"
                :disabled="!inputText.trim() || chat.isStreaming"
                :loading="chat.isStreaming"
                @click="handleSend"
              >
                <Icon icon="ph:paper-plane-tilt-fill" class="send-icon" />
                <span>发送</span>
              </el-button>
            </div>
          </div>
        </div>
      </main>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, nextTick, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Icon } from '@iconify/vue'
import { useChatStore } from '@/stores/chat'
import { marked } from 'marked'

const chat = useChatStore()
const inputText = ref('')
const messagesRef = ref<HTMLElement | null>(null)
const textareaRef = ref<HTMLTextAreaElement | null>(null)
const showSidebar = ref(true)
const loadingSessions = ref(false)
const loadingMessages = ref(false)
const editingSessionId = ref<number | null>(null)
const renameTitle = ref('')
const vFocus = {
  mounted: (el: HTMLElement) => el.focus(),
}
const quickPrompts = [
  {
    icon: 'ph:chart-pie-slice',
    title: '月度收支诊断',
    desc: '分析收入与支出结构，评估储蓄率',
    prompt: '请根据我当前的财务状况，帮我系统性分析月度收支结构与现金流健康度。',
  },
  {
    icon: 'ph:shield-check',
    title: '应急储备金规划',
    desc: '测算 3-6 个月必要支出与存放策略',
    prompt: '根据个人财务规划原则，家庭应急储备金应该如何测算规模、存放与管理？',
  },
  {
    icon: 'ph:trend-up',
    title: '标准普尔资产配置',
    desc: '梳理要花的钱、保命的钱与生钱的钱',
    prompt: '请用专业财务顾问的口吻，详细拆解标准普尔家庭资产象限模型的配置方法。',
  },
  {
    icon: 'ph:coins',
    title: '指数基金定投策略',
    desc: '学习微笑曲线、止盈止损与定投纪律',
    prompt: '请为我系统介绍指数基金定投的核心策略，包括定投周期与止盈纪律。',
  },
]

function formatTime(dateStr?: string): string {
  if (!dateStr) return ''
  try {
    const d = new Date(dateStr)
    if (isNaN(d.getTime())) return ''
    const now = new Date()
    const diff = now.getTime() - d.getTime()
    if (diff < 60000) return '刚刚'
    if (diff < 3600000) return `${Math.floor(diff / 60000)}分钟前`
    if (diff < 86400000) return `${Math.floor(diff / 3600000)}小时前`
    return d.toLocaleDateString()
  } catch { return '' }
}

function renderMarkdown(text?: string): string {
  if (!text) return ''
  try {
    return marked.parse(text, { async: false, breaks: true }) as string
  } catch {
    return text.replace(/</g, '&lt;').replace(/>/g, '&gt;')
  }
}

function parseModel(model: string) {
  const parts = model.split('/')
  if (parts.length === 2) return { provider: parts[0], model: parts[1] }
  return { provider: '', model }
}

function scrollToBottom() {
  if (messagesRef.value) {
    messagesRef.value.scrollTo({
      top: messagesRef.value.scrollHeight,
      behavior: 'smooth',
    })
  }
}

function adjustTextareaHeight() {
  if (textareaRef.value) {
    textareaRef.value.style.height = 'auto'
    const nextH = Math.min(Math.max(textareaRef.value.scrollHeight, 44), 160)
    textareaRef.value.style.height = `${nextH}px`
  }
}

async function copyText(text: string) {
  try {
    await navigator.clipboard.writeText(text)
    ElMessage.success('已复制到剪贴板')
  } catch {
    ElMessage.error('复制失败，请手动复制')
  }
}

async function fetchSessions() {
  loadingSessions.value = true
  try { await chat.fetchSessions() }
  catch { ElMessage.error('加载对话列表失败') }
  finally { loadingSessions.value = false }
}

async function fetchModels() {
  try {
    await chat.fetchModels()
    if (!chat.currentModel && chat.availableModels.length > 0) {
      const first = chat.availableModels[0]
      if (first && first.provider_id && first.models && first.models[0]) {
        chat.currentModel = `${first.provider_id}/${first.models[0]}`
      }
    }
  } catch { /* optional */ }
}

async function fetchSettings() {
  try { await chat.fetchSettings() } catch { /* optional */ }
}

async function selectSession(id: number) {
  loadingMessages.value = true
  chat.currentSessionId = id
  await chat.selectSession(id)
  loadingMessages.value = false
  nextTick(() => scrollToBottom())
}

function handleNewChat() {
  chat.messages = []
  chat.currentSessionId = null
  inputText.value = ''
  adjustTextareaHeight()
}

async function handleClearChat() {
  try {
    await ElMessageBox.confirm('确定清空当前对话中的消息？', '提示', { type: 'warning' })
    chat.messages = []
  } catch { /* cancelled */ }
}

function startRename(s: { id: number; title: string }) {
  editingSessionId.value = s.id
  renameTitle.value = s.title
}
async function saveRename(id: number) {
  if (editingSessionId.value === null) return
  const title = renameTitle.value.trim()
  editingSessionId.value = null
  if (title) {
    await chat.renameSession(id, title)
    ElMessage.success('会话重命名成功')
  }
}

function cancelRename() {
  editingSessionId.value = null
}

async function handleDeleteSession(id: number) {
  try {
    await ElMessageBox.confirm('确定删除此会话记录？', '提示', { type: 'warning' })
    await chat.deleteSessionById(id)
    ElMessage.success('会话已删除')
  } catch { /* cancelled */ }
}

function handleModelChange() {
  const parsed = parseModel(chat.currentModel)
  chat.currentModel = parsed.model || ''
  if (parsed.provider) chat.currentProvider = parsed.provider
}

function handleQuickPrompt(promptText: string) {
  inputText.value = promptText
  handleSend()
}

async function handleSend() {
  const text = inputText.value.trim()
  if (!text || chat.isStreaming) return
  handleModelChange()
  inputText.value = ''
  adjustTextareaHeight()
  await chat.sendMessage(text)
  nextTick(() => scrollToBottom())
  fetchSessions()
}

async function handleRegenerate() {
  if (chat.isStreaming) return
  handleModelChange()
  await chat.regenerateLastMessage()
  nextTick(() => scrollToBottom())
}

watch(() => chat.messages.length, () => {
  nextTick(() => scrollToBottom())
})

onMounted(async () => {
  await Promise.allSettled([fetchSessions(), fetchModels(), fetchSettings()])
  if (!chat.currentSessionId) handleNewChat()
  nextTick(() => scrollToBottom())
})
</script>

<style scoped>
.chat-page {
  height: 100%;
  display: flex;
  flex-direction: column;
  background: var(--mf-background);
  color: var(--mf-text-main);
  overflow: hidden;
}

.page-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px 24px;
  border-bottom: 1px solid var(--mf-border);
  background: var(--mf-surface);
  backdrop-filter: blur(16px);
  flex-shrink: 0;
}

.header-title {
  display: flex;
  align-items: center;
  gap: 12px;
}

.title-accent {
  width: 4px;
  height: 22px;
  background: linear-gradient(180deg, var(--mf-primary), var(--mf-accent));
  border-radius: 2px;
  box-shadow: var(--mf-shadow-glow);
}

.header-title h2 {
  margin: 0;
  font-size: 18px;
  font-weight: 700;
  color: var(--mf-text-main);
  letter-spacing: -0.3px;
}

.header-badge {
  font-size: 11px;
  font-family: var(--mf-font-mono, monospace);
  padding: 2px 8px;
  border-radius: 12px;
  background: var(--mf-primary-light);
  color: var(--mf-primary);
  border: 1px solid var(--mf-border);
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}

.action-btn {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.btn-icon {
  font-size: 14px;
}

.chat-layout {
  flex: 1;
  display: flex;
  overflow: hidden;
  position: relative;
}

/* Sidebar */
.chat-sidebar {
  width: 280px;
  min-width: 280px;
  background: var(--mf-surface);
  backdrop-filter: blur(16px);
  border-right: 1px solid var(--mf-border);
  display: flex;
  flex-direction: column;
  transition: width 0.25s cubic-bezier(0.4, 0, 0.2, 1), min-width 0.25s cubic-bezier(0.4, 0, 0.2, 1);
  z-index: 5;
}

.chat-sidebar.is-collapsed {
  width: 0;
  min-width: 0;
  overflow: hidden;
  border-right: none;
}

.sidebar-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 16px;
  border-bottom: 1px solid var(--mf-border);
}

.sidebar-title {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  font-weight: 600;
  color: var(--mf-text-main);
}

.session-count {
  font-size: 11px;
  color: var(--mf-text-muted);
}

.icon-close-btn {
  background: transparent;
  border: none;
  color: var(--mf-text-muted);
  cursor: pointer;
  padding: 4px;
  border-radius: 4px;
  display: flex;
  align-items: center;
  transition: color 0.15s, background 0.15s;
}

.icon-close-btn:hover {
  color: var(--mf-text-main);
  background: var(--mf-surface-hover);
}

.session-list {
  flex: 1;
  overflow-y: auto;
  padding: 8px;
}

.session-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 12px;
  border-radius: var(--mf-radius-md);
  cursor: pointer;
  margin-bottom: 4px;
  background: transparent;
  border: 1px solid transparent;
  transition: background 0.15s, border-color 0.15s;
  position: relative;
}

.session-item:hover {
  background: var(--mf-surface-hover);
  border-color: var(--mf-border);
}

.session-item.active {
  background: var(--mf-primary-light);
  border-color: var(--mf-border-hover);
  box-shadow: 0 0 12px rgba(0, 212, 255, 0.1);
}

.session-item-content {
  flex: 1;
  min-width: 0;
}

.session-title {
  font-size: 13px;
  color: var(--mf-text-main);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  font-weight: 500;
}

.session-meta {
  font-size: 11px;
  color: var(--mf-text-muted);
  margin-top: 3px;
}

.session-item-actions {
  display: flex;
  align-items: center;
  gap: 4px;
  opacity: 0;
  transition: opacity 0.15s;
}

.session-item:hover .session-item-actions {
  opacity: 1;
}

.item-action-btn {
  background: transparent;
  border: none;
  color: var(--mf-text-muted);
  cursor: pointer;
  padding: 4px;
  border-radius: 4px;
  display: flex;
  align-items: center;
  font-size: 13px;
  transition: color 0.15s, background 0.15s;
}

.item-action-btn:hover {
  color: var(--mf-primary);
  background: rgba(0, 212, 255, 0.1);
}

.item-action-btn.delete-btn:hover {
  color: var(--mf-danger);
  background: var(--mf-danger-light);
}

.session-rename-box {
  width: 100%;
}

.session-rename-input {
  width: 100%;
  padding: 4px 8px;
  background: var(--mf-background);
  border: 1px solid var(--mf-primary);
  border-radius: 4px;
  color: var(--mf-text-main);
  font-size: 12px;
  outline: none;
  box-sizing: border-box;
}

.empty-sessions {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 32px 16px;
  color: var(--mf-text-placeholder);
  gap: 8px;
  font-size: 12px;
}

.empty-sessions-icon {
  font-size: 32px;
  opacity: 0.4;
}

.toggle-sidebar {
  position: absolute;
  top: 12px;
  left: 12px;
  z-index: 10;
  width: 28px;
  height: 28px;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: var(--mf-radius-sm);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  color: var(--mf-text-muted);
  transition: color 0.15s, border-color 0.15s;
}

.toggle-sidebar:hover {
  color: var(--mf-primary);
  border-color: var(--mf-border-hover);
}

/* Main Area */
.chat-main {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  background: var(--mf-background);
}

.model-bar {
  padding: 10px 20px 10px 48px;
  border-bottom: 1px solid var(--mf-border);
  background: var(--mf-surface);
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.model-selector-wrap {
  display: flex;
  align-items: center;
  gap: 8px;
}

.model-icon {
  font-size: 16px;
  color: var(--mf-primary);
}

.model-select {
  width: 240px;
}

.model-option-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  width: 100%;
}

.opt-name {
  font-weight: 500;
}

.opt-model {
  font-size: 11px;
  color: var(--mf-text-muted);
  font-family: var(--mf-font-mono, monospace);
}

.chat-status {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: var(--mf-primary);
}

.status-dot {
  width: 8px;
  height: 8px;
  background: var(--mf-primary);
  border-radius: 50%;
  box-shadow: 0 0 8px var(--mf-primary);
  animation: pulse 1.5s infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 1; transform: scale(1); }
  50% { opacity: 0.4; transform: scale(0.85); }
}

/* Messages */
.messages {
  flex: 1;
  overflow-y: auto;
  padding: 24px;
  display: flex;
  flex-direction: column;
  gap: 20px;
}

/* Empty State & Prompts Grid */
.empty-state {
  margin: auto;
  max-width: 760px;
  width: 100%;
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 32px 16px;
}

.empty-header {
  text-align: center;
  margin-bottom: 32px;
}

.empty-icon-wrap {
  width: 56px;
  height: 56px;
  border-radius: 16px;
  background: linear-gradient(135deg, rgba(0, 212, 255, 0.15), rgba(124, 58, 237, 0.15));
  border: 1px solid var(--mf-border-hover);
  display: flex;
  align-items: center;
  justify-content: center;
  margin: 0 auto 16px;
  box-shadow: var(--mf-shadow-glow);
}

.empty-sparkle {
  font-size: 28px;
  color: var(--mf-primary);
}

.empty-header h3 {
  margin: 0 0 8px;
  font-size: 20px;
  font-weight: 700;
  color: var(--mf-text-main);
}

.empty-header p {
  margin: 0;
  font-size: 13px;
  color: var(--mf-text-muted);
}

.quick-prompts-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 16px;
  width: 100%;
}

.prompt-card {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 16px 18px;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: var(--mf-radius-lg);
  cursor: pointer;
  transition: transform 0.2s, border-color 0.2s, box-shadow 0.2s;
  backdrop-filter: blur(12px);
}

.prompt-card:hover {
  transform: translateY(-2px);
  border-color: var(--mf-border-hover);
  box-shadow: var(--mf-shadow-glow);
}

.prompt-card-icon {
  width: 40px;
  height: 40px;
  border-radius: 10px;
  background: var(--mf-primary-light);
  color: var(--mf-primary);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 20px;
  flex-shrink: 0;
}

.prompt-card-body {
  flex: 1;
  min-width: 0;
}

.prompt-card-body h4 {
  margin: 0 0 4px;
  font-size: 14px;
  font-weight: 600;
  color: var(--mf-text-main);
}

.prompt-card-body p {
  margin: 0;
  font-size: 12px;
  color: var(--mf-text-muted);
  line-height: 1.4;
}

.prompt-card-arrow {
  color: var(--mf-text-placeholder);
  font-size: 16px;
  transition: transform 0.2s, color 0.2s;
}

.prompt-card:hover .prompt-card-arrow {
  color: var(--mf-primary);
  transform: translateX(3px);
}

/* Message Rows */
.message-row {
  display: flex;
  gap: 14px;
  max-width: 85%;
  position: relative;
}

.message-row.user {
  align-self: flex-end;
  flex-direction: row-reverse;
}

.message-row.assistant {
  align-self: flex-start;
}

.message-avatar {
  width: 36px;
  height: 36px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  font-size: 18px;
}

.message-row.user .message-avatar {
  background: linear-gradient(135deg, var(--mf-primary), #0284c7);
  color: #060b18;
  box-shadow: 0 0 10px rgba(0, 212, 255, 0.3);
}

.message-row.assistant .message-avatar {
  background: linear-gradient(135deg, #6366f1, #8b5cf6);
  color: white;
  box-shadow: 0 0 10px rgba(99, 102, 241, 0.3);
}

.message-bubble-wrap {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.message-content {
  padding: 12px 18px;
  border-radius: var(--mf-radius-lg);
  font-size: 14px;
  line-height: 1.65;
  word-break: break-word;
}

.message-row.user .message-content {
  background: linear-gradient(135deg, var(--mf-primary), #0ea5e9);
  color: #04101e;
  font-weight: 500;
  border-bottom-right-radius: 2px;
}

.message-row.assistant .message-content {
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  color: var(--mf-text-main);
  border-bottom-left-radius: 2px;
  backdrop-filter: blur(12px);
}

/* Markdown Specifics inside Assistant Messages */
.message-content :deep(p) {
  margin: 0 0 8px;
}
.message-content :deep(p:last-child) {
  margin-bottom: 0;
}
.message-content :deep(h1),
.message-content :deep(h2),
.message-content :deep(h3),
.message-content :deep(h4) {
  color: #fff;
  margin: 12px 0 6px;
  font-weight: 600;
}
.message-content :deep(ul),
.message-content :deep(ol) {
  margin: 6px 0 10px;
  padding-left: 20px;
}
.message-content :deep(li) {
  margin-bottom: 4px;
}
.message-content :deep(code) {
  font-family: var(--mf-font-mono, monospace);
  background: rgba(0, 212, 255, 0.08);
  color: #38bdf8;
  padding: 2px 6px;
  border-radius: 4px;
  font-size: 12px;
  border: 1px solid rgba(0, 212, 255, 0.15);
}
.message-content :deep(pre) {
  background: #020617;
  border: 1px solid var(--mf-border);
  border-radius: 8px;
  padding: 12px 14px;
  overflow-x: auto;
  margin: 10px 0;
}
.message-content :deep(pre code) {
  background: transparent;
  padding: 0;
  border: none;
  color: #e2e8f0;
}
.message-content :deep(blockquote) {
  margin: 10px 0;
  padding: 8px 14px;
  border-left: 3px solid var(--mf-primary);
  background: rgba(0, 212, 255, 0.04);
  border-radius: 0 6px 6px 0;
  color: var(--mf-text-muted);
}
.message-content :deep(table) {
  width: 100%;
  border-collapse: collapse;
  margin: 12px 0;
  font-size: 13px;
}
.message-content :deep(th),
.message-content :deep(td) {
  border: 1px solid var(--mf-border);
  padding: 8px 12px;
}
.message-content :deep(th) {
  background: rgba(0, 212, 255, 0.08);
  font-weight: 600;
  color: var(--mf-primary);
}

.message-content.thinking {
  display: flex;
  align-items: center;
  gap: 8px;
  color: var(--mf-text-muted);
}

.pulse-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--mf-primary);
  animation: pulse 1.2s infinite ease-in-out;
}
.pulse-dot:nth-child(2) { animation-delay: 0.2s; }
.pulse-dot:nth-child(3) { animation-delay: 0.4s; }
.thinking-text { font-size: 12px; margin-left: 4px; }

/* Message Actions */
.message-actions {
  display: flex;
  align-items: center;
  gap: 8px;
  padding-left: 4px;
}

.msg-action-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  color: var(--mf-text-muted);
  background: transparent;
  border: 1px solid transparent;
  border-radius: 4px;
  padding: 2px 8px;
  cursor: pointer;
  transition: color 0.15s, background 0.15s, border-color 0.15s;
}

.msg-action-btn:hover {
  color: var(--mf-primary);
  background: var(--mf-surface-hover);
  border-color: var(--mf-border);
}

/* Input Area */
.chat-input-area {
  padding: 16px 24px 20px;
  background: var(--mf-surface);
  border-top: 1px solid var(--mf-border);
  backdrop-filter: blur(16px);
}

.chat-input-box {
  background: var(--mf-background);
  border: 1px solid var(--mf-border);
  border-radius: var(--mf-radius-lg);
  padding: 10px 14px;
  transition: border-color 0.2s, box-shadow 0.2s;
  display: flex;
  flex-direction: column;
}

.chat-input-box:focus-within {
  border-color: var(--mf-primary);
  box-shadow: 0 0 16px rgba(0, 212, 255, 0.2);
}

.chat-input-box textarea {
  width: 100%;
  background: transparent;
  border: none;
  color: var(--mf-text-main);
  font-size: 14px;
  line-height: 1.5;
  resize: none;
  outline: none;
  box-sizing: border-box;
  font-family: inherit;
}

.chat-input-box textarea:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.input-bottom-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-top: 8px;
  padding-top: 6px;
  border-top: 1px solid rgba(255, 255, 255, 0.05);
}

.char-count {
  font-size: 11px;
  color: var(--mf-text-placeholder);
  font-family: var(--mf-font-mono, monospace);
}

.send-btn {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-weight: 600;
  border-radius: 8px;
  padding: 6px 16px;
}

.send-icon {
  font-size: 14px;
}

@media (max-width: 768px) {
  .chat-sidebar {
    position: absolute;
    height: 100%;
    box-shadow: 4px 0 20px rgba(0, 0, 0, 0.5);
  }
  .quick-prompts-grid {
    grid-template-columns: 1fr;
  }
  .message-row {
    max-width: 95%;
  }
  .model-bar {
    padding-left: 48px;
  }
}
</style>
