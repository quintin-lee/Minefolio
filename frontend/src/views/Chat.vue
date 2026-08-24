<template>
  <div class="chat-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>AI助手</h2>
      </div>
      <div class="header-actions">
        <el-button @click="handleNewChat">
          <el-icon><Plus /></el-icon>
          新对话
        </el-button>
      </div>
    </div>

    <div class="chat-layout">
      <!-- Sidebar: session list -->
      <aside class="chat-sidebar" :class="{ 'is-collapsed': !showSidebar }">
        <div class="sidebar-header">
          <span>对话列表</span>
          <el-button text size="small" @click="showSidebar = false">✕</el-button>
        </div>
        <div class="session-list" v-loading="loadingSessions">
          <div
            v-for="s in chat.sessions"
            :key="s.id"
            class="session-item"
            :class="{ active: chat.currentSessionId === s.id }"
            @click="selectSession(s.id)"
          >
            <div class="session-title">{{ s.title }}</div>
            <div class="session-meta">{{ formatTime(s.updated_at) }}</div>
            <el-button
              text
              size="small"
              class="delete-btn"
              @click.stop="handleDeleteSession(s.id)"
            >
              <el-icon><Delete /></el-icon>
            </el-button>
          </div>
          <el-empty v-if="chat.sessions.length === 0 && !loadingSessions" description="暂无对话" />
        </div>
      </aside>

      <!-- Toggle sidebar button -->
      <div class="toggle-sidebar" @click="showSidebar = !showSidebar">
        <el-icon><List /></el-icon>
      </div>

      <!-- Main chat area -->
      <main class="chat-main">
        <!-- Model selector -->
        <div class="model-bar">
          <el-select v-model="chat.currentModel" size="small" style="width: 200px" placeholder="选择模型">
            <el-option
              v-for="m in chat.availableModels"
              :key="m.provider_id"
              :value="`${m.provider_id}/${m.models[0]}`"
            >
              <span>{{ m.provider_name }} / {{ m.models[0] }}</span>
            </el-option>
          </el-select>
        </div>

        <!-- Messages -->
        <div class="messages" ref="messagesRef" v-loading="loadingMessages">
          <div v-if="chat.messages.length === 0" class="empty-hint">
            <Icon icon="ph:chat-circle-text" class="empty-icon" />
            <p>开始与 AI 对话吧！</p>
          </div>
          <div
            v-for="msg in chat.messages"
            :key="msg.id"
            class="message"
            :class="msg.role"
          >
            <div class="message-avatar">
              <Icon :icon="msg.role === 'user' ? 'ph:user' : 'ph:robot'" />
            </div>
            <div class="message-content" v-html="renderMarkdown(msg.content)"></div>
          </div>
          <div v-if="chat.isStreaming" class="message assistant">
            <div class="message-avatar"><Icon icon="ph:robot" /></div>
            <div class="message-content thinking">
              <el-icon class="is-loading"><Loading /></el-icon>
            </div>
          </div>
        </div>

        <!-- Input -->
        <div class="chat-input">
          <textarea
            v-model="inputText"
            :disabled="chat.isStreaming"
            placeholder="输入消息... (Shift+Enter 换行，Enter 发送)"
            rows="3"
            @keydown.enter.exact.prevent="handleSend"
            @keydown.shift.enter.prevent
          ></textarea>
          <div class="input-actions">
            <span class="char-count">{{ inputText.length }}</span>
            <el-button
              type="primary"
              :disabled="!inputText.trim() || chat.isStreaming"
              @click="handleSend"
            >
              <el-icon><Promotion /></el-icon>
              发送
            </el-button>
          </div>
        </div>
      </main>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, nextTick, computed } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Plus, Delete, List, Loading, Promotion } from '@element-plus/icons-vue'
import { Icon } from '@iconify/vue'
import { useChatStore } from '@/stores/chat'
import { marked } from 'marked'

const chat = useChatStore()
const inputText = ref('')
const messagesRef = ref<HTMLElement | null>(null)
const showSidebar = ref(true)
const loadingSessions = ref(false)
const loadingMessages = ref(false)

function formatTime(dateStr: string): string {
  try {
    const d = new Date(dateStr)
    const now = new Date()
    const diff = now.getTime() - d.getTime()
    if (diff < 60000) return '刚刚'
    if (diff < 3600000) return `${Math.floor(diff / 60000)}分钟前`
    if (diff < 86400000) return `${Math.floor(diff / 3600000)}小时前`
    return d.toLocaleDateString()
  } catch { return dateStr }
}

function renderMarkdown(text: string): string {
  if (!text) return ''
  try { return marked.parse(text, { async: false }) as string }
  catch { return text.replace(/</g, '&lt;').replace(/>/g, '&gt;') }
}

function parseModel(model: string) {
  const parts = model.split('/')
  if (parts.length === 2) return { provider: parts[0], model: parts[1] }
  return { provider: '', model }
}

function scrollToBottom() {
  if (messagesRef.value) messagesRef.value.scrollTop = messagesRef.value.scrollHeight
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
  } catch { /* models optional */ }
}

async function fetchSettings() {
  try { await chat.fetchSettings() } catch { /* optional */ }
}

async function selectSession(id: number) {
  loadingMessages.value = true
  chat.currentSessionId = id
  await chat.selectSession(id)
  nextTick(() => scrollToBottom())
  loadingMessages.value = false
}

function handleNewChat() {
  chat.createNewSession()
  chat.messages = []
  chat.currentSessionId = null
}

async function handleDeleteSession(id: number) {
  try {
    await ElMessageBox.confirm('确定删除此对话？', '提示', { type: 'warning' })
    await chat.deleteSessionById(id)
  } catch { /* cancelled or error */ }
}

async function handleSend() {
  const text = inputText.value.trim()
  if (!text || chat.isStreaming) return
  const parsed = parseModel(chat.currentModel)
  chat.currentModel = parsed.model || ''
  if (parsed.provider) chat.currentProvider = parsed.provider
  inputText.value = ''
  await chat.sendMessage(text)
  nextTick(() => scrollToBottom())
  fetchSessions()
}

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
  background: var(--bg-secondary);
}
.page-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px 24px;
  border-bottom: 1px solid var(--border-color);
  background: var(--bg-primary);
}
.header-title { display: flex; align-items: center; gap: 12px; }
.title-accent { width: 4px; height: 24px; background: linear-gradient(180deg, #6366f1, #8b5cf6); border-radius: 2px; }
.header-title h2 { margin: 0; font-size: 18px; font-weight: 600; color: var(--text-primary); }
.header-actions { display: flex; gap: 8px; }

.chat-layout { flex: 1; display: flex; overflow: hidden; position: relative; }

.chat-sidebar {
  width: 260px; min-width: 260px; background: var(--bg-primary);
  border-right: 1px solid var(--border-color); display: flex; flex-direction: column;
  transition: width 0.2s, min-width 0.2s;
}
.chat-sidebar.is-collapsed { width: 0; min-width: 0; overflow: hidden; border-right: none; }
.sidebar-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 12px 16px; border-bottom: 1px solid var(--border-color);
  font-size: 14px; font-weight: 600; color: var(--text-primary);
}
.session-list { flex: 1; overflow-y: auto; padding: 8px; }
.session-item { padding: 10px 12px; border-radius: 8px; cursor: pointer; position: relative; margin-bottom: 4px; transition: background 0.15s; }
.session-item:hover { background: var(--bg-hover); }
.session-item.active { background: var(--color-primary-light); }
.session-title { font-size: 13px; color: var(--text-primary); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.session-meta { font-size: 11px; color: var(--text-secondary); margin-top: 2px; }
.delete-btn { position: absolute; top: 8px; right: 8px; opacity: 0; transition: opacity 0.15s; color: var(--text-secondary); }
.session-item:hover .delete-btn { opacity: 1; }
.delete-btn:hover { color: var(--color-danger); }

.toggle-sidebar {
  display: none; align-items: center; justify-content: center;
  width: 32px; height: 32px; background: var(--bg-primary);
  border: 1px solid var(--border-color); border-radius: 6px;
  cursor: pointer; color: var(--text-secondary); margin: 8px; align-self: flex-start;
}

.chat-main { flex: 1; display: flex; flex-direction: column; overflow: hidden; background: var(--bg-secondary); }
.model-bar { padding: 12px 20px; border-bottom: 1px solid var(--border-color); background: var(--bg-primary); }
.messages { flex: 1; overflow-y: auto; padding: 20px; display: flex; flex-direction: column; gap: 16px; }
.empty-hint { flex: 1; display: flex; flex-direction: column; align-items: center; justify-content: center; color: var(--text-secondary); gap: 12px; }
.empty-icon { font-size: 48px; opacity: 0.3; }
.message { display: flex; gap: 12px; max-width: 80%; }
.message.user { align-self: flex-end; flex-direction: row-reverse; }
.message.assistant { align-self: flex-start; }
.message-avatar { width: 32px; height: 32px; border-radius: 50%; display: flex; align-items: center; justify-content: center; flex-shrink: 0; font-size: 14px; }
.message.user .message-avatar { background: var(--color-primary); color: white; }
.message.assistant .message-avatar { background: #6366f1; color: white; }
.message-content { padding: 10px 14px; border-radius: 12px; font-size: 14px; line-height: 1.6; white-space: pre-wrap; word-break: break-word; }
.message.user .message-content { background: var(--color-primary); color: white; border-bottom-right-radius: 4px; }
.message.assistant .message-content { background: var(--bg-primary); color: var(--text-primary); border: 1px solid var(--border-color); border-bottom-left-radius: 4px; }
.message-content.thinking { display: flex; align-items: center; gap: 8px; color: var(--text-secondary); font-style: italic; }

.chat-input { padding: 16px 20px; border-top: 1px solid var(--border-color); background: var(--bg-primary); }
.chat-input textarea {
  width: 100%; padding: 12px 16px; border: 1px solid var(--border-color);
  border-radius: 12px; background: var(--bg-secondary); color: var(--text-primary);
  font-size: 14px; line-height: 1.5; resize: none; outline: none;
  transition: border-color 0.15s; box-sizing: border-box;
}
.chat-input textarea:focus { border-color: var(--color-primary); }
.chat-input textarea:disabled { opacity: 0.7; cursor: not-allowed; }
.input-actions { display: flex; align-items: center; justify-content: space-between; margin-top: 8px; }
.char-count { font-size: 12px; color: var(--text-secondary); }

@media (max-width: 768px) {
  .chat-sidebar { position: absolute; z-index: 10; height: 100%; box-shadow: 2px 0 12px rgba(0,0,0,0.15); }
  .chat-sidebar.is-collapsed { width: 0; min-width: 0; }
  .toggle-sidebar { display: flex; }
  .message { max-width: 90%; }
}
</style>
