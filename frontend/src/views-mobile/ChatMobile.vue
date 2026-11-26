<template>
  <div class="chat-mobile">
    <header class="chat-header">
      <el-button text @click="sessionDrawer = true">会话</el-button>
      <span class="chat-title">{{ currentSession?.title || 'AI 助手' }}</span>
      <el-button text type="primary" @click="newSession">新建</el-button>
    </header>

    <div ref="listRef" class="message-list">
      <PromptStarters v-if="chat.messages.length === 0" @select="onQuickPrompt" />
      <template v-else>
        <div
          v-for="msg in chat.messages"
          :key="msg.id"
          class="message-row"
          :class="msg.role"
        >
          <ChatMessageContent
            :message-id="msg.id"
            :content="msg.content"
            :thinking-content="msg.thinking_content"
            :is-streaming="chat.isStreaming && msg === chat.messages[chat.messages.length - 1]"
            :tool-calls="msg.tool_calls"
          />
          <div v-if="msg.error" class="message-error">{{ msg.error }}</div>
        </div>
      </template>
    </div>

    <div class="input-bar">
      <div v-if="!isOnline" class="offline-hint">当前离线，输入内容会保存为草稿</div>
      <textarea
        ref="textareaRef"
        v-model="inputText"
        class="input-field"
        rows="2"
        placeholder="输入消息…"
        :disabled="chat.isStreaming"
        @keydown.enter.exact.prevent="send"
      />
      <el-button
        type="primary"
        :loading="chat.isStreaming"
        :disabled="!isOnline || chat.isStreaming || !inputText.trim()"
        @click="send"
      >发送</el-button>
    </div>

    <el-drawer v-model="sessionDrawer" direction="ltr" size="80%" :with-header="false">
      <div class="session-head">
        <strong>历史会话</strong>
        <el-button text @click="sessionDrawer = false">关闭</el-button>
      </div>
      <div
        v-for="s in chat.sessions"
        :key="s.id"
        class="session-item"
        :class="{ active: s.id === chat.currentSessionId }"
        @click="selectSession(s.id)"
      >
        {{ s.title || `会话 ${s.id}` }}
      </div>
      <div v-if="chat.sessions.length === 0" class="session-empty">暂无历史会话</div>
    </el-drawer>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { useChatStore } from '@/stores/chat'
import ChatMessageContent from '@/components/ChatMessageContent.vue'
import PromptStarters from '@/components/PromptStarters.vue'

const chat = useChatStore()
const inputText = ref('')
const sessionDrawer = ref(false)
const isOnline = ref(typeof navigator !== 'undefined' ? navigator.onLine : true)
const listRef = ref<HTMLElement | null>(null)
const textareaRef = ref<HTMLTextAreaElement | null>(null)
const currentSession = computed(() => chat.sessions.find((s) => s.id === chat.currentSessionId) ?? null)

function handleOnline() { isOnline.value = true }
function handleOffline() { isOnline.value = false }

function scrollToBottom() {
  nextTick(() => {
    const el = listRef.value
    if (el) el.scrollTop = el.scrollHeight
  })
}

// 只监听最后一条消息 ID，避免历史分页前置加载触发误滚动（AGENTS.md 第 12 条）
watch(
  () => chat.messages[chat.messages.length - 1]?.id,
  () => scrollToBottom()
)

async function newSession() {
  await chat.createNewSession()
  inputText.value = ''
  sessionDrawer.value = false
  scrollToBottom()
}

async function selectSession(id: number) {
  await chat.selectSession(id)
  sessionDrawer.value = false
  scrollToBottom()
}

function onQuickPrompt(prompt: string) {
  inputText.value = prompt
  nextTick(() => textareaRef.value?.focus())
}

async function send() {
  const text = inputText.value.trim()
  if (!text || chat.isStreaming || !isOnline.value) return
  await chat.sendMessage(text)
  scrollToBottom()
}

onMounted(async () => {
  window.addEventListener('online', handleOnline)
  window.addEventListener('offline', handleOffline)
  try {
    await chat.fetchSessions()
    await chat.fetchModels()
  } catch (e) {
    console.error('[ChatMobile] init failed:', e)
  }
  scrollToBottom()
})

onBeforeUnmount(() => {
  window.removeEventListener('online', handleOnline)
  window.removeEventListener('offline', handleOffline)
  chat.abortCurrentStream()
})
</script>

<style scoped>
.chat-mobile { display: flex; flex-direction: column; height: 100%; min-height: 0; }
.chat-header { display: flex; align-items: center; justify-content: space-between; flex-shrink: 0; padding: 8px 12px; border-bottom: 1px solid var(--mf-border); }
.chat-title { font-size: 14px; font-weight: 600; color: var(--mf-text-primary); }
.message-list { flex: 1; min-height: 0; overflow-y: auto; padding: 12px; overscroll-behavior: contain; }
.message-row { margin-bottom: 12px; }
.message-row.user { padding-left: 18%; }
.message-row.assistant { padding-right: 8%; }
.message-error { margin-top: 4px; font-size: 12px; color: var(--mf-danger); }
.input-bar { flex-shrink: 0; display: flex; align-items: flex-end; gap: 8px; padding: 8px 12px; border-top: 1px solid var(--mf-border); flex-wrap: wrap; }
.input-field { flex: 1; min-width: 120px; resize: none; border-radius: 8px; padding: 8px; background: var(--mf-surface); color: var(--mf-text-primary); border: 1px solid var(--mf-border); }
.offline-hint { width: 100%; font-size: 11px; color: var(--mf-warning); }
.session-head { display: flex; align-items: center; justify-content: space-between; padding: 8px 0 12px; }
.session-item { padding: 12px 8px; border-bottom: 1px solid var(--mf-border); font-size: 13px; cursor: pointer; }
.session-item.active { color: var(--mf-primary); }
.session-empty { padding: 24px 0; text-align: center; color: var(--mf-text-secondary); font-size: 13px; }
</style>
