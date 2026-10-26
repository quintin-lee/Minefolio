<template>
  <div class="chat-page">
    <div class="chat-layout">
      <!-- Mobile overlay backdrop -->
      <div class="sidebar-overlay" v-if="showSidebar && isMobile" @click="showSidebar = false"></div>

      <!-- Sidebar: session list -->
      <aside class="chat-sidebar" :class="{ 'is-collapsed': !showSidebar }">
        <div class="sidebar-header">
          <div class="sidebar-title">
            <Icon icon="ph:chat-circle-text-bold" />
            <span>历史会话</span>
            <span class="session-count" v-if="chat.sessions.length > 0">({{ chat.sessions.length }})</span>
          </div>
          <div class="sidebar-header-actions">
            <el-tooltip content="新建会话" placement="bottom" :show-after="300">
              <button class="icon-header-btn" @click="handleNewChat" aria-label="新建会话">
                <Icon icon="ph:plus-bold" />
              </button>
            </el-tooltip>
            <el-tooltip content="收起会话列表" placement="bottom" :show-after="300">
              <button class="icon-close-btn" @click="showSidebar = false" title="收起会话列表" aria-label="收起会话列表">
                <Icon icon="ph:sidebar-simple" />
              </button>
            </el-tooltip>
          </div>
        </div>
        <div class="session-search-wrap" v-if="chat.sessions.length > 3">
          <Icon icon="ph:magnifying-glass" class="session-search-icon" />
          <input v-model="sessionSearch" class="session-search-input" placeholder="搜索会话..." aria-label="搜索会话" />
          <button v-if="sessionSearch" class="session-search-clear" @click="sessionSearch = ''" title="清空">
            <Icon icon="ph:x" />
          </button>
        </div>
        <div class="session-list" v-loading="loadingSessions">
          <template v-if="groupedSessions.length > 0">
            <template v-for="group in groupedSessions" :key="group.label">
              <div class="session-group-label">{{ group.label }}</div>
              <div
                v-for="s in group.items"
                :key="s.id"
                class="session-item"
                :class="{ active: chat.currentSessionId === s.id }"
                @click="selectSession(s.id)"
                role="listitem"
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
            </template>
          </template>
          <div v-if="filteredSessions.length === 0 && !loadingSessions" class="empty-sessions">
            <Icon icon="ph:chats" class="empty-sessions-icon" />
            <span>{{ sessionSearch ? '无匹配会话' : '暂无历史会话' }}</span>
          </div>
        </div>
      </aside>

      <!-- Main chat area -->
      <main class="chat-main">
        <!-- Integrated Chat Toolbar -->
        <div class="chat-toolbar" :class="{ 'with-toggle': !showSidebar }">
          <div class="toolbar-left">
            <!-- Toggle sidebar button (when sidebar is collapsed) -->
            <el-tooltip v-if="!showSidebar" content="展开会话列表" placement="bottom" :show-after="300">
              <button class="toggle-sidebar-btn" @click="showSidebar = true" aria-label="展开会话列表">
                <Icon icon="ph:sidebar-simple" />
              </button>
            </el-tooltip>

            <div class="model-selector-wrap">
              <Icon icon="ph:cpu-bold" class="model-icon" />
              <el-select
                v-model="selectedModelKey"
                size="small"
                class="model-select"
                placeholder="选择 AI 模型"
              >
                <el-option
                  v-for="opt in modelOptions"
                  :key="opt.value"
                  :value="opt.value"
                  :label="opt.label"
                >
                  <div class="model-option-item">
                    <span class="opt-name">{{ opt.provider_name }}</span>
                    <span class="opt-model">{{ opt.model }}</span>
                  </div>
                </el-option>
              </el-select>
            </div>

            <span class="current-model-badge" v-if="chat.currentModel && !chat.isStreaming">
              {{ chat.currentModel }}
            </span>

            <!-- Thinking Mode Toggle in Toolbar -->
            <button
              type="button"
              class="toolbar-thinking-btn"
              :class="{ active: chat.enableThinking }"
              @click="chat.toggleThinking()"
              :title="chat.enableThinking ? '深度思考模式已开启 (点击关闭)' : '深度思考模式已关闭 (点击开启)'"
              aria-label="切换深度思考模式"
            >
              <Icon :icon="chat.enableThinking ? 'ph:brain-fill' : 'ph:brain'" class="thinking-btn-icon" />
              <span class="thinking-btn-text">深度思考</span>
              <span class="thinking-status-tag" v-if="chat.enableThinking">开</span>
            </button>

            <div class="chat-status" v-if="chat.isStreaming">
              <span class="status-dot"></span>
              <span>正在深度思考与生成...</span>
              <button class="stop-gen-btn" @click="handleStop" title="停止生成 (Esc)">
                <Icon icon="ph:stop-bold" />
                <span>停止</span>
              </button>
            </div>
          </div>

          <div class="toolbar-right">
            <el-button
              v-if="!showSidebar || isMobile"
              type="primary"
              size="small"
              plain
              class="toolbar-action-btn new-chat-btn"
              @click="handleNewChat"
            >
              <Icon icon="ph:plus-bold" class="btn-icon" />
              <span>新对话</span>
            </el-button>

            <template v-if="chat.messages.length > 0 && !chat.isStreaming">
              <el-button
                size="small"
                text
                class="toolbar-action-btn"
                @click="exportToMarkdown"
                title="导出当前会话为 Markdown 文件"
              >
                <Icon icon="ph:download-simple" class="btn-icon" />
                <span>导出</span>
              </el-button>
              <el-button
                size="small"
                text
                class="toolbar-action-btn"
                @click="copyAllChat"
                title="复制当前会话全文"
              >
                <Icon icon="ph:copy" class="btn-icon" />
                <span>复制</span>
              </el-button>
              <el-button
                size="small"
                text
                class="toolbar-action-btn"
                @click="handleClearChat"
                title="清空记录"
              >
                <Icon icon="ph:trash" class="btn-icon" />
                <span>清空</span>
              </el-button>
            </template>
          </div>
        </div>

        <!-- Messages stream container -->
        <div class="messages-container">
          <div class="messages" ref="messagesRef" v-loading="loadingMessages" @scroll.passive="handleScroll" role="log" aria-live="polite" aria-label="对话消息">
            <!-- Load more trigger (invisible sentinel at top) -->
            <div ref="loadMoreTrigger" class="load-more-sentinel"></div>
            <div v-if="chat.loadingMoreMessages" class="load-more-spinner">
              <span class="spinner-dot"></span><span class="spinner-dot"></span><span class="spinner-dot"></span>
            </div>
            <!-- Empty state: Quick prompts -->
            <PromptStarters v-if="chat.messages.length === 0" @select="handleQuickPrompt" />

            <!-- Message list -->
            <template v-else>
              <div
                v-for="(msg, idx) in chat.messages"
                :key="msg.id"
                v-memo="[msg.content, msg.thinking_content || '', msg.role, chat.isStreaming && idx === chat.messages.length - 1 ? 1 : 0, msg.workflowData?.status || '', msg.tool_calls?.length || 0, msg.tool_calls?.[msg.tool_calls.length - 1]?.result?.length || 0, msg.workflowConfig ? 1 : 0, msg.error || '', msg.usage ? JSON.stringify(msg.usage) : '']"
                class="message-row"
                :class="msg.role"
              >
                <div class="message-avatar">
                  <Icon :icon="msg.role === 'user' ? 'ph:user-bold' : 'ph:robot-bold'" />
                </div>
                <div class="message-bubble-wrap">
                  <template v-if="msg.role === 'user'">
                    <div v-if="editingUserIdx === idx" class="user-edit-box">
                      <textarea v-model="editDraft" class="user-edit-input" rows="2" @keydown.enter.exact.prevent="confirmEdit(idx)" @keydown.esc="cancelEdit" placeholder="编辑后按 Enter 发送，Esc 取消"></textarea>
                      <div class="user-edit-actions">
                        <el-button size="small" @click="cancelEdit">取消</el-button>
                        <el-button size="small" type="primary" :disabled="!editDraft.trim()" @click="confirmEdit(idx)">发送</el-button>
                      </div>
                    </div>
                    <template v-else>
                      <div class="message-content">{{ msg.content }}</div>
                      <div v-if="!chat.isStreaming" class="message-actions user-actions">
                      <button class="msg-action-btn" title="复制" @click="copyText(msg.content)" aria-label="复制消息">
                        <Icon icon="ph:copy" />
                        <span>复制</span>
                      </button>
                      <button class="msg-action-btn" title="编辑并重发" @click="startEdit(idx, msg.content)" aria-label="编辑并重发">
                        <Icon icon="ph:pencil-simple" />
                        <span>编辑</span>
                      </button>
                      </div>
                    </template>
                  </template>
                  <template v-else>
                    <div v-if="!msg.content && chat.isStreaming && !msg.workflowData && !msg.workflowConfig && !msg.thinking_content" class="message-content thinking">
                      <span class="pulse-dot"></span>
                      <span class="pulse-dot"></span>
                      <span class="pulse-dot"></span>
                      <span class="thinking-text">思考中...</span>
                    </div>
                    <div v-else class="message-content" :class="{ 'streaming-active': chat.isStreaming && idx === chat.messages.length - 1 && msg.role === 'assistant' }">
                      <ChatMessageContent
                        :message-id="msg.id"
                        :content="msg.content"
                        :thinking-content="msg.thinking_content"
                        :tool-calls="msg.tool_calls"
                        :workflow-data="msg.workflowData"
                        :workflow-config="msg.workflowConfig"
                        :is-streaming="chat.isStreaming && idx === chat.messages.length - 1 && msg.role === 'assistant'"
                        :enable-buffer="chat.enableTypewriterBuffer"
                        :extracted-item-ids="extractedSegmentIdMap[msg.id] || []"
                        @open-panel-item="handleOpenPanelItem"
                      />
                    </div>
                    <div v-if="msg.usage" class="message-usage">
                      <span v-if="msg.usage.total_tokens">⚡ {{ msg.usage.total_tokens.toLocaleString() }} tokens · ${{ msg.usage.cost_usd.toFixed(4) }}</span>
                      <span v-if="msg.usage.elapsed_ms"> · {{ (msg.usage.elapsed_ms / 1000).toFixed(1) }}s</span>
                      <span v-if="msg.usage.finish_reason && msg.usage.finish_reason !== 'stop'" class="budget-tag" :title="'结束原因: ' + msg.usage.finish_reason">{{ msg.usage.finish_reason }}</span>
                    </div>
                    <div v-if="msg.error" class="message-error">
                      <Icon icon="ph:warning-circle-fill" />
                      <span>{{ msg.error }}</span>
                    </div>
                    <div v-if="(msg.content || msg.thinking_content || msg.error) && !chat.isStreaming" class="message-actions">
                      <button v-if="msg.content || msg.thinking_content" class="msg-action-btn" title="复制回答" @click="copyText(msg.content || msg.thinking_content || '')" aria-label="复制回答">
                        <Icon icon="ph:copy" />
                        <span>复制</span>
                      </button>
                      <button v-if="idx === chat.messages.length - 1" class="msg-action-btn" title="重新生成" @click="handleRegenerate" aria-label="重新生成">
                        <Icon icon="ph:arrows-counter-clockwise" />
                        <span>重新生成</span>
                      </button>
                      <button v-if="(msg.content && msg.content.startsWith('⚠️')) || msg.error" class="msg-action-btn" title="重试" @click="handleRetryMessage(msg.id)" aria-label="重试">
                        <Icon icon="ph:arrow-clockwise" />
                        <span>重试</span>
                      </button>
                    </div>
                  </template>
                </div>
              </div>
            </template>
          </div>

          <button
            v-show="chat.messages.length > 0 && showScrollBottomBtn"
            class="scroll-bottom-btn"
            ref="scrollBtnRef"
            title="回到底部"
            @click="forceScrollToBottom"
          >
            <Icon icon="ph:arrow-down-bold" />
            <span>回到底部</span>
          </button>
        </div>

        <!-- Input Area -->
        <div class="chat-input-area" ref="inputAreaRef">
          <!-- Financial AI Workflows Trigger Bar -->
          <WorkflowBar />

          <div class="chat-input-box" style="position: relative;">
            <WorkflowSlashMenu
              ref="slashMenuRef"
              :visible="showSlashMenu"
              :query="slashQuery"
              @select="handleSelectWorkflow"
              @close="showSlashMenu = false"
            />
            <textarea
              ref="textareaRef"
              v-model="inputText"
              :disabled="chat.isStreaming"
              placeholder="输入财务咨询、收支分析，或键入 / 快速唤起工作流... (Enter 发送，Shift+Enter 换行)"
              rows="1"
              @input="handleInput"
              @keydown="handleTextareaKeyDown"
            ></textarea>
            <div class="input-bottom-bar">
              <div class="input-bottom-left">
                <button
                  type="button"
                  class="input-thinking-btn"
                  :class="{ active: chat.enableThinking }"
                  @click="chat.toggleThinking()"
                  :title="chat.enableThinking ? '深度思考模式已开启 (点击关闭)' : '深度思考模式已关闭 (点击开启)'"
                  aria-label="切换深度思考模式"
                >
                  <Icon :icon="chat.enableThinking ? 'ph:brain-fill' : 'ph:brain'" class="btn-icon" />
                  <span>深度思考</span>
                  <span class="thinking-dot" v-if="chat.enableThinking"></span>
                </button>
              </div>
              <div class="input-bottom-right">
                <span class="char-count" :class="{ near: inputText.length > 1800, over: inputText.length > 2000 }">{{ inputText.length }} / 2000</span>
                <span class="input-hint" v-if="!chat.isStreaming">↵ 发送 · ⇧↵ 换行 · Esc 停止</span>
                <el-button v-if="chat.isStreaming" type="danger" class="send-btn stop-btn" @click="handleStop">
                  <Icon icon="ph:stop-circle-bold" class="send-icon" />
                  <span>停止生成</span>
                </el-button>
                <el-button v-else type="primary" class="send-btn" :disabled="!inputText.trim()" @click="handleSend">
                  <Icon icon="ph:paper-plane-tilt-fill" class="send-icon" />
                  <span>发送</span>
                </el-button>
              </div>
            </div>
          </div>
        </div>
      </main>

      <!-- Panel Overlay Backdrop (Mobile/Tablet drawer mode) -->
      <div
        v-if="panelOverlay && panelVisible && !isMobile"
        class="panel-overlay-backdrop"
        @click="panelVisible = false"
      ></div>

      <!-- Analysis Result Panel -->
      <AnalysisPanel
        v-if="!isMobile"
        :visible="panelVisible"
        :items="panelItems"
        :active-tab-id="activeTabId"
        :overlay="panelOverlay"
        :width="panelWidth"
        @close="panelVisible = false"
        @update:active-tab-id="id => activeTabId = id"
        @remove-tab="handleRemovePanelTab"
        @resize-start="startPanelResize"
      />
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, nextTick, watch, computed } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Icon } from '@iconify/vue'
import { useChatStore } from '@/stores/chat'
import ChatMessageContent from '@/components/ChatMessageContent.vue'
import PromptStarters from '@/components/PromptStarters.vue'
import WorkflowBar from '@/components/WorkflowBar.vue'
import WorkflowSlashMenu from '@/components/WorkflowSlashMenu.vue'
import AnalysisPanel from '@/components/AnalysisPanel.vue'
import { extractPanelItems } from '@/utils/chat-panel-extractor'
import type { PanelItem } from '@/types/chat-panel'
import { localToday } from '@/utils/format'
import type { WorkflowDef } from '@/types'

const chat = useChatStore()
const inputText = ref('')
const messagesRef = ref<HTMLElement | null>(null)
const textareaRef = ref<HTMLTextAreaElement | null>(null)
const slashMenuRef = ref<InstanceType<typeof WorkflowSlashMenu> | null>(null)
const showSlashMenu = ref(false)
const slashQuery = ref('')
const showSidebar = ref(true)
const loadingSessions = ref(false)
const loadingMessages = ref(false)
const editingSessionId = ref<number | null>(null)
const renameTitle = ref('')
const sessionSearch = ref('')
const editingUserIdx = ref<number | null>(null)
const editDraft = ref('')
const DRAFT_PREFIX = 'minefolio:chat:draft:'
const loadMoreTrigger = ref<HTMLElement | null>(null)
let scrollObserver: IntersectionObserver | null = null
const isMobile = ref(typeof window !== 'undefined' ? window.innerWidth < 768 : false)
const inputAreaRef = ref<HTMLElement | null>(null)
const scrollBtnRef = ref<HTMLButtonElement | null>(null)

// --- 分析面板 (Analysis Panel) 状态与拖拽 ---
const panelVisible = ref(false)
const panelItems = ref<PanelItem[]>([])
const activeTabId = ref('')
const panelOverlay = ref(false)
const PANEL_WIDTH_KEY = 'minefolio:analysis-panel-width'
const DEFAULT_PANEL_WIDTH = 420
const panelWidth = ref(DEFAULT_PANEL_WIDTH)
const MAX_PANEL_ITEMS = 20

function loadPanelWidth() {
  try {
    const saved = localStorage.getItem(PANEL_WIDTH_KEY)
    if (saved) {
      const w = parseInt(saved, 10)
      if (!isNaN(w) && w >= 300 && w <= 600) {
        panelWidth.value = w
      }
    }
  } catch { /* ignore */ }
}

let isResizing = false
let startX = 0
let startWidth = 0

function startPanelResize(e: MouseEvent) {
  if (panelOverlay.value || isMobile.value) return
  isResizing = true
  startX = e.clientX
  startWidth = panelWidth.value
  document.body.style.cursor = 'col-resize'
  document.body.style.userSelect = 'none'
  window.addEventListener('mousemove', onPanelMouseMove)
  window.addEventListener('mouseup', stopPanelResize)
}

function onPanelMouseMove(e: MouseEvent) {
  if (!isResizing) return
  const delta = startX - e.clientX
  const nextW = Math.min(Math.max(startWidth + delta, 300), 600)
  panelWidth.value = nextW
}

function stopPanelResize() {
  if (!isResizing) return
  isResizing = false
  document.body.style.cursor = ''
  document.body.style.userSelect = ''
  window.removeEventListener('mousemove', onPanelMouseMove)
  window.removeEventListener('mouseup', stopPanelResize)
  try {
    localStorage.setItem(PANEL_WIDTH_KEY, String(panelWidth.value))
  } catch { /* ignore */ }
}

function checkPanelMode() {
  panelOverlay.value = !isMobile.value && window.innerWidth < 1200
}

function checkMobile() {
  isMobile.value = window.innerWidth < 768
  updateMessagesBottomPadding()
  checkPanelMode()
}

// 抽取卡片映射表：messageId -> segmentId[]
// 在移动端模式下强制返回空映射，使所有结构化卡片直接全宽内联渲染
const extractedSegmentIdMap = computed(() => {
  if (isMobile.value) return {}
  const map: Record<number, string[]> = {}
  for (const item of panelItems.value) {
    if (item.messageId !== undefined && item.segmentId) {
      if (!map[item.messageId]) map[item.messageId] = []
      map[item.messageId]!.push(item.segmentId)
    }
  }
  return map
})

function scanAllMessagesToPanel() {
  const idSet = new Set(panelItems.value.map(i => i.id))
  for (const msg of chat.messages) {
    if (msg.role !== 'assistant') continue
    const res = extractPanelItems(msg.id, msg.content, msg.workflowData, msg.workflowConfig)
    for (const item of res.panelItems) {
      if (!idSet.has(item.id)) {
        if (panelItems.value.length >= MAX_PANEL_ITEMS) {
          panelItems.value.shift()
        }
        panelItems.value.push(item)
        idSet.add(item.id)
      }
    }
  }
}

function handleOpenPanelItem(itemId: string) {
  const existing = panelItems.value.find(i => i.id === itemId)
  if (existing) {
    activeTabId.value = existing.id
    panelVisible.value = true
    return
  }
  scanAllMessagesToPanel()
  activeTabId.value = itemId
  panelVisible.value = true
}

function handleRemovePanelTab(tabId: string) {
  const idx = panelItems.value.findIndex(i => i.id === tabId)
  if (idx >= 0) {
    panelItems.value.splice(idx, 1)
  }
  if (panelItems.value.length === 0) {
    panelVisible.value = false
    activeTabId.value = ''
  } else if (activeTabId.value === tabId) {
    const nextIdx = Math.min(idx, panelItems.value.length - 1)
    activeTabId.value = panelItems.value[nextIdx]?.id || ''
  }
}

const vFocus = {
  mounted: (el: HTMLElement) => el.focus(),
}

const filteredSessions = computed(() => {
  const q = sessionSearch.value.trim().toLowerCase()
  if (!q) return chat.sessions
  return chat.sessions.filter(s => s.title.toLowerCase().includes(q))
})

// Group sessions by time period
interface SessionGroup { label: string; items: typeof chat.sessions }
const groupedSessions = computed<SessionGroup[]>(() => {
  const now = Date.now()
  const DAY = 86400000
  const groups: Record<string, typeof chat.sessions> = {}
  const order: string[] = []
  for (const s of filteredSessions.value) {
    const t = new Date(s.updated_at).getTime()
    const diff = now - t
    let label: string
    if (diff < DAY) label = '今天'
    else if (diff < 2 * DAY) label = '昨天'
    else if (diff < 7 * DAY) label = '近 7 天'
    else if (diff < 30 * DAY) label = '近 30 天'
    else label = '更早'
    if (!groups[label]) { groups[label] = []; order.push(label) }
    groups[label]!.push(s)
  }
  return order.map(label => ({ label, items: groups[label]! }))
})

function draftKey(): string {
  return `${DRAFT_PREFIX}${chat.currentSessionId ?? 'new'}`
}
function loadDraft() {
  try {
    const v = localStorage.getItem(draftKey())
    if (v !== null) inputText.value = v
    else inputText.value = ''
  } catch { /* ignore */ }
  nextTick(() => adjustTextareaHeight())
}
function persistDraft() {
  try { localStorage.setItem(draftKey(), inputText.value) } catch { /* ignore */ }
}

function formatChatMarkdown(): string {
  const session = chat.sessions.find(s => s.id === chat.currentSessionId)
  const title = session?.title || 'Minefolio AI 财务对话记录'
  const dateStr = new Date().toLocaleString()
  const modelStr = chat.currentModel ? `\n- **AI 模型**: ${chat.currentModel} (${chat.currentProvider || '默认'})` : ''

  let md = `# ${title}\n\n- **导出时间**: ${dateStr}${modelStr}\n- **会话 ID**: ${chat.currentSessionId || '未命名'}\n\n---\n\n`

  for (const m of chat.messages) {
    const roleName = m.role === 'user' ? '👤 **用户 (User)**' : '🤖 **AI 财务助手 (Minefolio AI)**'
    md += `### ${roleName}\n\n${m.content}\n\n---\n\n`
  }
  return md
}

function exportToMarkdown() {
  if (chat.messages.length === 0) {
    ElMessage.info('当前没有可导出的对话内容')
    return
  }
  const md = formatChatMarkdown()
  const blob = new Blob([md], { type: 'text/markdown;charset=utf-8' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  const dateKey = localToday()
  a.href = url
  a.download = `Minefolio-AI-Chat-${dateKey}.md`
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
  URL.revokeObjectURL(url)
  ElMessage.success('对话记录已导出为 Markdown 文件！')
}

async function copyAllChat() {
  if (chat.messages.length === 0) {
    ElMessage.info('当前没有可复制的内容')
    return
  }
  const md = formatChatMarkdown()
  try {
    await navigator.clipboard.writeText(md)
    ElMessage.success('已复制整场对话记录至剪贴板')
  } catch {
    ElMessage.error('复制失败，请手动选择复制')
  }
}

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

interface FlatModelOption {
  provider_id: string
  provider_name: string
  model: string
  value: string
  label: string
}

const modelOptions = computed<FlatModelOption[]>(() => {
  const list: FlatModelOption[] = []
  chat.availableModels.forEach((p) => {
    const models = p.models && p.models.length > 0 ? p.models : []
    models.forEach((m) => {
      list.push({
        provider_id: p.provider_id,
        provider_name: p.provider_name || p.provider_id,
        model: m,
        value: `${p.provider_id}/${m}`,
        label: `${p.provider_name || p.provider_id} (${m})`,
      })
    })
  })
  if (chat.settings?.default_provider && chat.settings?.default_model) {
    const defKey = `${chat.settings.default_provider}/${chat.settings.default_model}`
    if (!list.some(item => item.value === defKey)) {
      list.unshift({
        provider_id: chat.settings.default_provider,
        provider_name: chat.settings.default_provider,
        model: chat.settings.default_model,
        value: defKey,
        label: `${chat.settings.default_provider} (${chat.settings.default_model})`,
      })
    }
  }
  return list
})

const selectedModelKey = computed({
  get() {
    if (chat.currentProvider && chat.currentModel) {
      return `${chat.currentProvider}/${chat.currentModel}`
    }
    if (chat.settings?.default_provider && chat.settings?.default_model) {
      return `${chat.settings.default_provider}/${chat.settings.default_model}`
    }
    if (modelOptions.value.length > 0) {
      return modelOptions.value[0]!.value
    }
    return ''
  },
  set(val: string) {
    const parts = val.split('/')
    if (parts.length === 2) {
      chat.setModel(parts[1]!, parts[0]!)
    } else {
      chat.setModel(val)
    }
  },
})

const autoStickToBottom = ref(true)
const showScrollBottomBtn = ref(false)
let scrollTicking = false

function handleScroll() {
  if (scrollTicking) return
  scrollTicking = true
  requestAnimationFrame(() => {
    scrollTicking = false
    const el = messagesRef.value
    if (!el) return
    const distance = el.scrollHeight - el.scrollTop - el.clientHeight
    autoStickToBottom.value = distance < 100
    showScrollBottomBtn.value = distance > 150
  })
}

function scrollToBottom(smooth = true) {
  const el = messagesRef.value
  if (!el) return
  if (!autoStickToBottom.value) return
  requestAnimationFrame(() => {
    if (smooth && typeof el.scrollTo === 'function') el.scrollTo({ top: el.scrollHeight, behavior: 'smooth' })
    else el.scrollTop = el.scrollHeight
  })
}

function forceScrollToBottom() {
  const el = messagesRef.value
  if (!el) return
  autoStickToBottom.value = true
  showScrollBottomBtn.value = false
  requestAnimationFrame(() => {
    if (typeof el.scrollTo === 'function') el.scrollTo({ top: el.scrollHeight, behavior: 'smooth' })
    else el.scrollTop = el.scrollHeight
  })
}

let inputAreaObserver: ResizeObserver | null = null

// 输入框已通过 Flex 纵向底栏与消息流解耦，天然不会遮挡最后一条消息。
// 监听输入区域高度变化时，如果用户处于底部跟随状态，保持滚动锁定在最底端。
function updateMessagesBottomPadding() {
  if (autoStickToBottom.value && messagesRef.value) {
    scrollToBottom()
  }
}

function setupInputAreaObserver() {
  inputAreaObserver?.disconnect()
  inputAreaObserver = null
  const area = inputAreaRef.value
  if (!area || typeof ResizeObserver === 'undefined') return
  inputAreaObserver = new ResizeObserver(updateMessagesBottomPadding)
  inputAreaObserver.observe(area)
  updateMessagesBottomPadding()
}

const lastMsgContent = computed(() => {
  const msgs = chat.messages
  return msgs.length > 0 ? msgs[msgs.length - 1]?.content : ''
})

watch(lastMsgContent, () => {
  if (chat.isStreaming) {
    scrollToBottom(false)
  }
})


const DIRECT_RUN_IDS = new Set(['wf_portfolio_rebalance', 'wf_goal_tracker', 'wf_bill_calendar', 'wf_health_score'])

function handleInput() {
  adjustTextareaHeight()
  const text = inputText.value
  const cursor = textareaRef.value?.selectionStart ?? text.length
  const textBeforeCursor = text.slice(0, cursor)
  const slashMatch = textBeforeCursor.match(/(?:^|\s)\/([^\s/]*)$/)
  if (slashMatch) {
    showSlashMenu.value = true
    slashQuery.value = slashMatch[1] ?? ''
  } else {
    showSlashMenu.value = false
    slashQuery.value = ''
  }
}

function handleTextareaKeyDown(e: KeyboardEvent) {
  if (showSlashMenu.value && slashMenuRef.value) {
    const handled = slashMenuRef.value.handleKeyDown(e)
    if (handled) return
  }
  if (e.key === 'Enter' && !e.shiftKey) {
    e.preventDefault()
    handleSend()
  } else if (e.key === 'Escape') {
    if (showSlashMenu.value) {
      showSlashMenu.value = false
    } else {
      handleStop()
    }
  }
}

function handleSelectWorkflow(wf: WorkflowDef) {
  showSlashMenu.value = false
  const text = inputText.value
  const cursor = textareaRef.value?.selectionStart ?? text.length
  const textBeforeCursor = text.slice(0, cursor)
  const slashIdx = textBeforeCursor.lastIndexOf('/')
  if (slashIdx >= 0) {
    inputText.value = (text.slice(0, slashIdx) + text.slice(cursor)).trim()
    nextTick(() => adjustTextareaHeight())
  }
  if (DIRECT_RUN_IDS.has(wf.id)) {
    chat.runWorkflow(wf.id)
  } else {
    chat.stageWorkflow(wf.id)
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
  } catch { /* optional */ }
}

async function fetchSettings() {
  try { await chat.fetchSettings() } catch { /* optional */ }
}

async function selectSession(id: number) {
  loadingMessages.value = true
  await chat.selectSession(id)
  loadingMessages.value = false
  autoStickToBottom.value = true
  showScrollBottomBtn.value = false
  nextTick(() => scrollToBottom(false))
}

function handleNewChat() {
  chat.clearCurrentSession()
  inputText.value = ''
  adjustTextareaHeight()
}

async function handleClearChat() {
  try {
    await ElMessageBox.confirm('确定清空当前对话中的消息？', '提示', { type: 'warning' })
    chat.clearMessages()
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

function handleQuickPrompt(promptText: string) {
  inputText.value = promptText
  handleSend()
}

async function handleSend() {
  const text = inputText.value.trim()
  if (!text || chat.isStreaming) return
  inputText.value = ''
  adjustTextareaHeight()
  autoStickToBottom.value = true
  showScrollBottomBtn.value = false
  try {
    await chat.sendMessage(text)
  } catch (err) {
    console.error('[chat] sendMessage failed', err)
  }
  nextTick(() => scrollToBottom())
  fetchSessions()
}

async function handleRegenerate() {
  if (chat.isStreaming) return
  autoStickToBottom.value = true
  showScrollBottomBtn.value = false
  await chat.regenerateLastMessage()
  nextTick(() => scrollToBottom())
}

function handleStop() {
  if (!chat.isStreaming) return
  chat.abortCurrentStream()
}

function startEdit(idx: number, content: string) {
  if (chat.isStreaming) return
  editingUserIdx.value = idx
  editDraft.value = content
}

function cancelEdit() {
  editingUserIdx.value = null
  editDraft.value = ''
}

async function confirmEdit(idx: number) {
  const text = editDraft.value.trim()
  if (!text || chat.isStreaming) return
  editingUserIdx.value = null
  editDraft.value = ''
  autoStickToBottom.value = true
  showScrollBottomBtn.value = false
  await chat.editAndResend(idx, text)
  nextTick(() => scrollToBottom())
  fetchSessions()
}

async function handleRetryMessage(msgId: number) {
  const idx = chat.messages.findIndex(m => m.id === msgId)
  if (idx < 0) return
  // 取该失败消息之前最近的一条用户消息（而非会话首条），避免重发陈旧 prompt 并误截断整段
  let userIdx = -1
  for (let i = idx - 1; i >= 0; i--) {
    if (chat.messages[i]?.role === 'user') { userIdx = i; break }
  }
  if (userIdx < 0) { ElMessage.error('无法找到对应用户消息') }
  else {
    const userMsg = chat.messages[userIdx]!
    await chat.editAndResend(userIdx, userMsg.content)
    nextTick(() => scrollToBottom())
  }
}

function setupInfiniteScroll() {
  if (scrollObserver) { scrollObserver.disconnect() }
  if (!loadMoreTrigger.value || typeof IntersectionObserver === 'undefined') return
  scrollObserver = new IntersectionObserver((entries) => {
    if (entries[0]?.isIntersecting && !chat.loadingMoreMessages && chat.loadedMessagePage * 50 < (chat.messageTotal ?? 0)) {
      chat.loadMoreMessages()
    }
  }, { root: messagesRef.value, threshold: 0.1 })
  scrollObserver.observe(loadMoreTrigger.value)
}

// 仅当“末尾新增消息”（最后一条消息 id 变化）时才自动吸底并滚动到底部。
// 向上滚动加载的历史消息是前置插入的，末尾 id 不变，此时绝不能把用户拽回底部。
const lastMessageId = computed(() => {
  const msgs = chat.messages
  return msgs.length > 0 ? (msgs[msgs.length - 1]?.id ?? null) : null
})

watch(lastMessageId, (id, prevId) => {
  if (id === null || id === prevId) return
  autoStickToBottom.value = true
  nextTick(() => scrollToBottom())
})

watch(inputText, () => { persistDraft() })
watch(() => chat.currentSessionId, () => {
  panelItems.value = []
  panelVisible.value = false
  activeTabId.value = ''
  loadDraft()
  nextTick(() => setupInfiniteScroll())
})

// 流式生成结束时，自动扫描助理最新输出并弹出展示
watch(() => chat.isStreaming, (streaming, prevStreaming) => {
  if (!streaming && prevStreaming) {
    const msgs = chat.messages
    if (msgs.length > 0) {
      const lastMsg = msgs[msgs.length - 1]
      if (lastMsg && lastMsg.role === 'assistant') {
        const res = extractPanelItems(lastMsg.id, lastMsg.content, lastMsg.workflowData, lastMsg.workflowConfig)
        if (res.panelItems.length > 0) {
          for (const item of res.panelItems) {
            if (!panelItems.value.some(i => i.id === item.id)) {
              if (panelItems.value.length >= MAX_PANEL_ITEMS) {
                panelItems.value.shift()
              }
              panelItems.value.push(item)
            }
          }
          if (!isMobile.value) {
            const latest = res.panelItems[res.panelItems.length - 1]
            if (latest) activeTabId.value = latest.id
            panelVisible.value = true
          }
        }
      }
    }
  }
})

function onGlobalKeydown(e: KeyboardEvent) {
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'k') {
    e.preventDefault()
    textareaRef.value?.focus()
  } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'n') {
    e.preventDefault()
    handleNewChat()
  } else if (e.key === 'Escape' && chat.isStreaming) {
    handleStop()
  }
}

onMounted(async () => {
  loadPanelWidth()
  checkMobile()
  window.addEventListener('resize', checkMobile)
  await Promise.allSettled([fetchSessions(), fetchSettings(), fetchModels()])
  if (chat.settings?.default_provider) {
    chat.setModel(chat.currentModel, chat.settings.default_provider)
  }
  if (chat.settings?.default_model) {
    chat.setModel(chat.settings.default_model, chat.currentProvider)
  } else if (!chat.currentModel && modelOptions.value.length > 0) {
    chat.setModel(modelOptions.value[0]!.model, modelOptions.value[0]!.provider_id)
  }
  if (!chat.currentSessionId) handleNewChat()
  loadDraft()
  nextTick(() => {
    scrollToBottom()
    setupInfiniteScroll()
    setupInputAreaObserver()
    textareaRef.value?.focus()
  })
  window.addEventListener('keydown', onGlobalKeydown)
})

onUnmounted(() => {
  stopPanelResize()
  window.removeEventListener('keydown', onGlobalKeydown)
  window.removeEventListener('resize', checkMobile)
  if (scrollObserver) { scrollObserver.disconnect(); scrollObserver = null }
  if (inputAreaObserver) { inputAreaObserver.disconnect(); inputAreaObserver = null }
  chat.abortCurrentStream()
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
  position: relative;
}

.chat-layout {
  flex: 1;
  display: flex;
  overflow: hidden;
  position: relative;
  height: 100%;
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
  padding: 12px 16px;
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

.sidebar-header-actions {
  display: flex;
  align-items: center;
  gap: 4px;
}

.icon-header-btn,
.icon-close-btn {
  background: transparent;
  border: none;
  color: var(--mf-text-muted);
  cursor: pointer;
  padding: 4px;
  border-radius: 6px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 15px;
  transition: color 0.15s, background 0.15s;
}

.icon-header-btn:hover,
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
  padding: 9px 12px;
  border-radius: var(--mf-radius-md);
  cursor: pointer;
  margin-bottom: 3px;
  background: transparent;
  border: 1px solid transparent;
  transition: background 0.18s, border-color 0.18s, transform 0.15s;
  position: relative;
}

.session-item:hover {
  background: var(--mf-surface-hover);
  border-color: var(--mf-border);
}

.session-item.active {
  background: var(--mf-primary-light);
  border-color: var(--mf-border-hover);
  box-shadow: 0 0 12px rgba(0, 212, 255, 0.08);
}

.session-item.active::before {
  content: '';
  position: absolute;
  left: 0;
  top: 6px;
  bottom: 6px;
  width: 3px;
  background: var(--mf-primary);
  border-radius: 0 3px 3px 0;
  box-shadow: 0 0 8px var(--mf-primary);
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

.session-item.active .session-title {
  font-weight: 600;
  color: var(--mf-primary);
}

.session-meta {
  font-size: 11px;
  color: var(--mf-text-muted);
  margin-top: 2px;
}

.session-item-actions {
  display: flex;
  align-items: center;
  gap: 4px;
  opacity: 0;
  transition: opacity 0.18s;
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
  background: var(--mf-primary-light);
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
  border-radius: 6px;
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

/* Main Area - container for dynamic width queries.
   container-type enables .messages / .chat-input-area to size
   against the actual content area (which changes when the global
   sidebar 260px<->64px or the session sidebar 280px<->0 collapses),
   not just the viewport. */
.chat-main {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  background: var(--mf-background);
  position: relative;
  container-type: inline-size;
  container-name: chat-main;
}

.chat-main::before {
  content: '';
  position: absolute;
  top: -60px;
  left: 50%;
  transform: translateX(-50%);
  width: 600px;
  height: 220px;
  background: radial-gradient(ellipse at center, rgba(0, 212, 255, 0.08), transparent 70%);
  pointer-events: none;
  z-index: 0;
}

/* Integrated Chat Toolbar */
.chat-toolbar {
  padding: 6px 16px;
  border-bottom: 1px solid var(--mf-border);
  background: var(--mf-surface);
  backdrop-filter: blur(16px);
  display: flex;
  align-items: center;
  justify-content: space-between;
  min-height: 48px;
  flex-shrink: 0;
  box-sizing: border-box;
  z-index: 5;
}

.toolbar-left {
  display: flex;
  align-items: center;
  gap: 10px;
  min-width: 0;
}

.toggle-sidebar-btn {
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 6px;
  width: 28px;
  height: 28px;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  color: var(--mf-text-muted);
  font-size: 15px;
  transition: all 0.15s ease;
  flex-shrink: 0;
}

.toggle-sidebar-btn:hover {
  color: var(--mf-primary);
  border-color: var(--mf-primary);
  background: var(--mf-surface-hover);
}

.toolbar-thinking-btn {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 6px;
  padding: 4px 10px;
  font-size: 12px;
  font-weight: 500;
  color: var(--mf-text-secondary);
  cursor: pointer;
  transition: all 0.2s ease;
  flex-shrink: 0;
  user-select: none;
}

.toolbar-thinking-btn:hover {
  background: var(--mf-surface-hover);
  color: var(--mf-text-primary);
  border-color: var(--mf-border-focus);
}

.toolbar-thinking-btn.active {
  background: rgba(99, 102, 241, 0.08);
  border-color: rgba(99, 102, 241, 0.4);
  color: var(--mf-primary);
}

.toolbar-thinking-btn .thinking-btn-icon {
  font-size: 14px;
}

.toolbar-thinking-btn.active .thinking-btn-icon {
  color: var(--mf-primary);
}

.toolbar-thinking-btn .thinking-status-tag {
  font-size: 10px;
  line-height: 1;
  padding: 1px 4px;
  border-radius: 4px;
  background: var(--mf-primary);
  color: #ffffff;
  font-weight: 600;
}

.toolbar-right {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-shrink: 0;
}

.toolbar-action-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 12px;
  font-weight: 500;
  border-radius: 6px;
  padding: 4px 8px;
  transition: all 0.15s ease;
}

.toolbar-action-btn .btn-icon {
  font-size: 13px;
}

.current-model-badge {
  display: inline-flex;
  align-items: center;
  font-size: 11px;
  font-family: var(--mf-font-mono, monospace);
  padding: 2px 8px;
  border-radius: 10px;
  background: var(--mf-primary-light);
  color: var(--mf-primary);
  border: 1px solid var(--mf-primary-border, var(--mf-border));
  white-space: nowrap;
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
  width: 230px;
}

.model-select :deep(.el-input__wrapper) {
  background: var(--mf-background);
  box-shadow: none !important;
  border: 1px solid var(--mf-border);
  border-radius: 8px;
  padding: 2px 10px;
}

.model-select :deep(.el-input__wrapper:hover) {
  border-color: var(--mf-border-hover);
}

.model-select :deep(.el-input__wrapper.is-focus) {
  border-color: var(--mf-primary);
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

/* Messages - Centered Reading Column (fluid: follows .chat-main width).
   Base 860px kept as fallback for browsers without container units;
   modern browsers use min(1060px, 100cqi) so the column widens when
   sidebars collapse and fills narrow pages without overflow. */
.messages-container {
  flex: 1;
  min-height: 0;
  position: relative;
  overflow: hidden;
  display: flex;
  flex-direction: column;
}

.messages {
  flex: 1;
  overflow-y: auto;
  padding: 20px 20px 24px;
  display: flex;
  flex-direction: column;
  gap: 24px;
  max-width: 860px;
  max-width: min(1060px, 100cqi);
  margin: 0 auto;
  width: 100%;
  box-sizing: border-box;
  position: relative;
}

@container chat-main (min-width: 1400px) {
  .messages {
    max-width: min(1120px, 96cqi);
  }
}

/* Message Rows */
.message-row {
  display: flex;
  gap: 14px;
  max-width: 100%;
  position: relative;
  content-visibility: auto;
  contain-intrinsic-size: 0 120px;
  animation: message-appear 0.25s cubic-bezier(0.4, 0, 0.2, 1);
}

@keyframes message-appear {
  from { opacity: 0; transform: translateY(8px); }
  to { opacity: 1; transform: translateY(0); }
}

.message-row.user {
  align-self: flex-end;
  flex-direction: row-reverse;
  max-width: 82%;
}

.message-row.assistant {
  align-self: flex-start;
  width: 100%;
  max-width: 100%;
}

.message-avatar {
  width: 36px;
  height: 36px;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  font-size: 18px;
  transition: box-shadow 0.3s ease;
}

.message-row.user .message-avatar {
  background: linear-gradient(135deg, var(--mf-primary), #6366f1);
  color: #ffffff;
  box-shadow: 0 2px 10px rgba(99, 102, 241, 0.3);
}

.message-row.assistant .message-avatar {
  background: linear-gradient(135deg, #6366f1, #8b5cf6);
  color: white;
  box-shadow: 0 2px 10px rgba(99, 102, 241, 0.25);
}

.message-row.assistant:has(.thinking) .message-avatar,
.message-row.assistant:has(.streaming-active) .message-avatar {
  box-shadow: 0 0 0 3px rgba(99, 102, 241, 0.25), 0 0 16px rgba(99, 102, 241, 0.45);
  animation: avatar-pulse 2s infinite ease-in-out;
}

@keyframes avatar-pulse {
  0%, 100% { box-shadow: 0 0 0 2px rgba(99, 102, 241, 0.2), 0 0 10px rgba(99, 102, 241, 0.3); }
  50% { box-shadow: 0 0 0 6px rgba(99, 102, 241, 0.35), 0 0 18px rgba(99, 102, 241, 0.5); }
}

.message-bubble-wrap {
  display: flex;
  flex-direction: column;
  gap: 6px;
  max-width: 100%;
  min-width: 0;
}

.message-content {
  padding: 13px 18px;
  border-radius: 18px;
  font-size: 14px;
  line-height: 1.65;
  word-break: break-word;
}

.message-row.user .message-content {
  background: linear-gradient(135deg, var(--mf-primary), #4f46e5);
  color: #ffffff;
  font-weight: 500;
  border-bottom-right-radius: 4px;
  box-shadow: 0 4px 14px rgba(79, 70, 229, 0.22);
}

[data-theme="light"] .message-row.user .message-content {
  background: linear-gradient(135deg, var(--mf-primary), #4338ca);
  color: #ffffff;
  text-shadow: 0 1px 2px rgba(0, 0, 0, 0.15);
  box-shadow: 0 4px 14px rgba(67, 56, 202, 0.2);
}

.message-row.assistant .message-content {
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  color: var(--mf-text-main);
  border-bottom-left-radius: 4px;
  backdrop-filter: blur(16px);
  box-shadow: var(--mf-shadow-sm);
}

.typing-cursor {
  display: inline-block;
  width: 5px;
  height: 1.1em;
  background-color: var(--mf-primary, #00d4ff);
  margin-left: 3px;
  vertical-align: -2px;
  animation: cursor-blink 0.8s steps(1) infinite;
  border-radius: 2px;
}

@keyframes cursor-blink {
  0%, 100% { opacity: 1; }
  50% { opacity: 0; }
}

.message-content.thinking {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 10px 16px;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 16px;
  box-shadow: var(--mf-shadow-sm);
}

.pulse-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--mf-primary);
  animation: thinking-bounce 1.2s infinite ease-in-out;
}
.pulse-dot:nth-child(2) { animation-delay: 0.2s; }
.pulse-dot:nth-child(3) { animation-delay: 0.4s; }

@keyframes thinking-bounce {
  0%, 80%, 100% { transform: scale(0.6); opacity: 0.4; }
  40% { transform: scale(1.1); opacity: 1; }
}

.thinking-text {
  font-size: 12.5px;
  color: var(--mf-text-muted);
  font-weight: 500;
}

/* 用量统计行：随 done 事件回传的 token/成本/耗时 */
.message-usage {
  display: flex;
  align-items: center;
  gap: 4px;
  margin-top: 4px;
  font-size: 11px;
  color: var(--mf-text-muted);
  font-variant-numeric: tabular-nums;
}

.message-usage .budget-tag {
  padding: 0 6px;
  border-radius: 6px;
  background: var(--mf-warning-light, rgba(250, 204, 21, 0.16));
  border: 1px solid var(--mf-warning-border, rgba(250, 204, 21, 0.3));
  color: var(--mf-warning, #ca8a04);
}

/* 流式失败提示：与正文解耦展示，不覆盖已生成的内容 */
.message-error {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  margin-top: 6px;
  padding: 6px 12px;
  font-size: 12.5px;
  border-radius: 10px;
  background: var(--mf-danger-light);
  border: 1px solid var(--mf-danger-border);
  color: var(--mf-danger);
}

.message-error svg {
  flex-shrink: 0;
}

/* Message Actions */
.message-actions {
  display: flex;
  align-items: center;
  gap: 6px;
  padding-left: 2px;
  margin-top: 2px;
  opacity: 0.85;
  transition: opacity 0.18s ease;
}

.message-row:hover .message-actions {
  opacity: 1;
}

.msg-action-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 11.5px;
  color: var(--mf-text-muted);
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 6px;
  padding: 3px 8px;
  cursor: pointer;
  backdrop-filter: blur(8px);
  transition: color 0.15s, background 0.15s, border-color 0.15s, transform 0.12s;
}

.msg-action-btn:hover {
  color: var(--mf-primary);
  background: var(--mf-primary-light);
  border-color: var(--mf-primary-border);
  transform: translateY(-1px);
}

/* Input Area - Natural Flex Bottom Dock */
.chat-input-area {
  flex-shrink: 0;
  width: 100%;
  max-width: 860px;
  max-width: min(1060px, 100cqi);
  margin: 0 auto;
  padding: 0 20px 16px;
  z-index: 10;
  box-sizing: border-box;
}

@container chat-main (min-width: 1400px) {
  .chat-input-area {
    max-width: min(1120px, 96cqi);
  }
}

.chat-input-box {
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 18px;
  padding: 12px 16px;
  backdrop-filter: blur(20px);
  box-shadow: 0 10px 30px rgba(0, 0, 0, 0.12), 0 2px 8px rgba(0, 0, 0, 0.04);
  transition: border-color 0.22s, box-shadow 0.22s, transform 0.2s;
  display: flex;
  flex-direction: column;
}

.chat-input-box:focus-within {
  border-color: var(--mf-primary);
  box-shadow: 0 12px 36px rgba(0, 0, 0, 0.16), 0 0 0 2px var(--mf-primary-light);
}

.chat-input-box textarea {
  width: 100%;
  background: transparent;
  border: none;
  color: var(--mf-text-main);
  font-size: 14px;
  line-height: 1.6;
  resize: none;
  outline: none;
  box-sizing: border-box;
  font-family: inherit;
  max-height: 200px;
}

.chat-input-box textarea::placeholder {
  color: var(--mf-text-placeholder);
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
  padding-top: 8px;
  border-top: 1px solid var(--mf-border);
}

.input-bottom-left {
  display: flex;
  align-items: center;
  gap: 8px;
}

.input-bottom-right {
  display: flex;
  align-items: center;
  gap: 12px;
}

.input-thinking-btn {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  background: transparent;
  border: 1px solid var(--mf-border);
  border-radius: 20px;
  padding: 3px 10px;
  font-size: 12px;
  font-weight: 500;
  color: var(--mf-text-placeholder);
  cursor: pointer;
  transition: all 0.2s ease;
  user-select: none;
}

.input-thinking-btn:hover {
  border-color: var(--mf-border-focus);
  color: var(--mf-text-regular);
}

.input-thinking-btn.active {
  background: rgba(99, 102, 241, 0.08);
  border-color: rgba(99, 102, 241, 0.35);
  color: var(--mf-primary);
}

.input-thinking-btn .btn-icon {
  font-size: 14px;
}

.input-thinking-btn .thinking-dot {
  width: 5px;
  height: 5px;
  border-radius: 50%;
  background: var(--mf-primary);
  display: inline-block;
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
  border-radius: 10px;
  padding: 7px 18px;
  background: linear-gradient(135deg, var(--mf-primary), #6366f1) !important;
  border: none !important;
  color: #ffffff !important;
  box-shadow: 0 2px 8px rgba(99, 102, 241, 0.25);
  transition: all 0.2s ease;
}

.send-btn:hover:not(:disabled) {
  opacity: 0.92;
  transform: translateY(-1px);
}

.send-btn:disabled {
  opacity: 0.45;
  cursor: not-allowed;
  box-shadow: none;
}

.send-icon {
  font-size: 14px;
}

.scroll-bottom-btn {
  position: absolute;
  bottom: 14px;
  left: 50%;
  transform: translateX(-50%);
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 6px 16px;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border-hover);
  border-radius: 20px;
  color: var(--mf-primary);
  font-size: 12px;
  font-weight: 600;
  box-shadow: var(--mf-shadow-md), var(--mf-shadow-glow);
  cursor: pointer;
  z-index: 15;
  backdrop-filter: blur(16px);
  transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
}

.scroll-bottom-btn:hover {
  background: var(--mf-primary-light);
  border-color: var(--mf-primary);
  transform: translateX(-50%) translateY(-2px);
}

.message-row {
  content-visibility: auto;
  contain-intrinsic-size: 0 120px;
}

.session-search-wrap {
  display: flex;
  align-items: center;
  gap: 6px;
  margin: 6px 8px 4px;
  padding: 6px 8px;
  background: var(--mf-background);
  border: 1px solid var(--mf-border);
  border-radius: 8px;
}
.session-search-icon { color: var(--mf-text-muted); font-size: 12px; flex-shrink: 0; }
.session-search-input {
  flex: 1;
  background: transparent;
  border: none;
  outline: none;
  color: var(--mf-text-main);
  font-size: 12px;
  min-width: 0;
}
.session-search-clear {
  background: transparent;
  border: none;
  color: var(--mf-text-muted);
  cursor: pointer;
  display: flex;
  padding: 2px;
}
.stop-gen-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  margin-left: 8px;
  padding: 2px 8px;
  font-size: 11px;
  background: var(--mf-danger-light, rgba(239,68,68,0.12));
  color: var(--mf-danger, #ef4444);
  border: 1px solid rgba(239,68,68,0.25);
  border-radius: 12px;
  cursor: pointer;
}
.stop-gen-btn:hover { background: rgba(239,68,68,0.18); }
.user-edit-box { display: flex; flex-direction: column; gap: 8px; min-width: 260px; }
.user-edit-input {
  width: 100%;
  min-height: 60px;
  padding: 8px 10px;
  background: var(--mf-background);
  border: 1px solid var(--mf-primary);
  border-radius: 8px;
  color: var(--mf-text-main);
  font-size: 13px;
  resize: vertical;
  outline: none;
  font-family: inherit;
}
.user-edit-actions { display: flex; justify-content: flex-end; gap: 8px; }
.user-actions { opacity: 0.85; }
.message-row:hover .user-actions { opacity: 1; }
.input-hint { font-size: 10px; color: var(--mf-text-placeholder); margin-right: auto; margin-left: 8px; }
.char-count.near { color: #f59e0b; }
.char-count.over { color: var(--mf-danger, #ef4444); }
.stop-btn { background: var(--mf-danger, #ef4444) !important; border-color: var(--mf-danger, #ef4444) !important; }

/* Session group labels */
.session-group-label {
  font-size: 11px;
  color: var(--mf-text-muted);
  padding: 6px 12px 2px;
  font-weight: 600;
  letter-spacing: 0.05em;
  text-transform: uppercase;
}

/* Load more spinner */
.load-more-sentinel { height: 1px; }
.load-more-spinner {
  display: flex;
  justify-content: center;
  align-items: center;
  gap: 4px;
  padding: 8px 0;
}
.spinner-dot {
  width: 6px; height: 6px;
  border-radius: 50%;
  background: var(--mf-text-muted);
  animation: dot-bounce 1.2s infinite ease-in-out;
}
.spinner-dot:nth-child(2) { animation-delay: 0.2s; }
.spinner-dot:nth-child(3) { animation-delay: 0.4s; }
@keyframes dot-bounce {
  0%, 80%, 100% { transform: scale(0.6); opacity: 0.4; }
  40% { transform: scale(1); opacity: 1; }
}

/* Mobile overlay backdrop */
.sidebar-overlay {
  display: none;
  position: fixed;
  inset: 0;
  background: var(--mf-surface-muted);
  z-index: 99;
  backdrop-filter: blur(2px);
}

@media (max-width: 768px) {
  .sidebar-overlay { display: block; }
  .chat-sidebar {
    position: fixed;
    left: 0; top: 0; bottom: 0;
    z-index: 100;
    transform: translateX(-100%);
    transition: transform 0.25s cubic-bezier(0.4, 0, 0.2, 1);
    box-shadow: 4px 0 20px var(--mf-shadow-lg);
  }
  .chat-sidebar:not(.is-collapsed) { transform: translateX(0); }
  .chat-main { width: 100% !important; }
  .chat-input-area {
    padding: 0 10px 10px;
    max-width: 100%;
    max-width: 100cqi;
  }
  .messages {
    padding: 16px 12px 20px;
    max-width: 100%;
    max-width: 100cqi;
  }
  .message-row.user { max-width: 90%; }
  .message-row.assistant { max-width: 100%; }
}

/* 面板抽屉模式下的半透明遮罩 */
.panel-overlay-backdrop {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.45);
  backdrop-filter: blur(4px);
  z-index: 110;
  animation: backdrop-fade 0.2s ease-out;
}

@keyframes backdrop-fade {
  from { opacity: 0; }
  to { opacity: 1; }
}

</style>
