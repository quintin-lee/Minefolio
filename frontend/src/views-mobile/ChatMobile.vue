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
          v-for="(msg, idx) in chat.messages"
          :key="msg.id"
          class="message-row"
          :class="msg.role"
        >
          <ChatMessageContent
            :message-id="msg.id"
            :content="msg.content"
            :thinking-content="msg.thinking_content"
            :tool-calls="msg.tool_calls"
            :workflow-data="msg.workflowData"
            :workflow-config="msg.workflowConfig"
            :is-streaming="chat.isStreaming && idx === chat.messages.length - 1 && msg.role === 'assistant'"
            :enable-buffer="Boolean(chat.enableTypewriterBuffer)"
            @action-executed="handleActionExecuted(msg, $event)"
          />
          <div v-if="msg.error" class="message-error">{{ msg.error }}</div>
          <div
            v-if="!chat.isStreaming && (msg.content || msg.thinking_content || msg.error)"
            class="message-actions"
          >
            <button
              v-if="msg.content || msg.thinking_content"
              type="button"
              class="msg-action-btn"
              aria-label="复制消息"
              @click="copyMessage(msg.content || msg.thinking_content || '')"
            >
              <Icon icon="ph:copy" />
              <span>复制</span>
            </button>
            <button
              v-if="msg.role === 'assistant' && idx === chat.messages.length - 1 && typeof chat.regenerateLastMessage === 'function'"
              type="button"
              class="msg-action-btn"
              aria-label="重新生成"
              @click="handleRegenerate"
            >
              <Icon icon="ph:arrows-clockwise" />
              <span>重新生成</span>
            </button>
          </div>
        </div>
      </template>
    </div>

    <div v-if="isOnline && !isWorkflowCollapsed && Array.isArray(chat.workflows)" class="mobile-workflow-wrap">
      <WorkflowBar />
    </div>

    <div class="input-bar" @paste="handlePaste">
      <div v-if="!isOnline" class="offline-hint">当前离线，输入内容会保存为草稿</div>

      <div v-if="attachedImage" class="attachment-pill">
        <img :src="attachedImage.dataUrl" class="attachment-thumb" alt="票据截图" />
        <div class="attachment-meta">
          <span class="attachment-name">{{ attachedImage.name }}</span>
          <span class="attachment-size">{{ attachedImage.sizeText }} · 待 AI 识别</span>
        </div>
        <button
          type="button"
          class="attachment-remove-btn"
          aria-label="移除图片"
          @click="removeAttachment"
        >
          <Icon icon="ph:x-bold" />
        </button>
      </div>

      <textarea
        ref="textareaRef"
        v-model="inputText"
        class="input-field"
        rows="2"
        placeholder="输入消息，或上传票据截图…"
        :disabled="chat.isStreaming || isScanningReceipt"
        @keydown.enter.exact.prevent="handleSendAction"
      />

      <div class="input-actions-row">
        <div class="input-tools-left">
          <label class="tool-pill-btn" :class="{ active: !!attachedImage }" title="上传票据截图">
            <Icon icon="ph:image-square-bold" />
            <span>{{ attachedImage ? '更换票据' : '票据' }}</span>
            <input
              type="file"
              accept="image/*"
              style="display: none;"
              @change="handleFileSelect"
            />
          </label>
          <button
            v-if="typeof chat.toggleThinking === 'function'"
            type="button"
            class="tool-pill-btn"
            :class="{ active: Boolean(chat.enableThinking) }"
            aria-label="切换深度思考"
            @click="chat.toggleThinking?.()"
          >
            <Icon :icon="chat.enableThinking ? 'ph:brain-fill' : 'ph:brain'" />
            <span>深度思考</span>
          </button>
        </div>

        <el-button
          v-if="chat.isStreaming"
          type="danger"
          @click="handleStop"
        >
          停止
        </el-button>
        <el-button
          v-else
          type="primary"
          :loading="isScanningReceipt"
          :disabled="!isOnline || isScanningReceipt || (!inputText.trim() && !attachedImage)"
          @click="handleSendAction"
        >
          {{ isScanningReceipt ? '识别中' : '发送' }}
        </el-button>
      </div>
    </div>

    <el-drawer v-model="sessionDrawer" direction="ltr" size="82%" :with-header="false">
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
        <span class="session-title">{{ s.title || `会话 ${s.id}` }}</span>
        <div class="session-item-actions" @click.stop>
          <button
            v-if="typeof chat.renameSession === 'function'"
            type="button"
            class="session-act-btn"
            aria-label="重命名会话"
            @click="handleRenameSession(s)"
          >
            <Icon icon="ph:pencil-simple" />
          </button>
          <button
            v-if="typeof chat.deleteSessionById === 'function'"
            type="button"
            class="session-act-btn danger"
            aria-label="删除会话"
            @click="handleDeleteSession(s.id)"
          >
            <Icon icon="ph:trash" />
          </button>
        </div>
      </div>
      <div v-if="chat.sessions.length === 0" class="session-empty">暂无历史会话</div>
    </el-drawer>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Icon } from '@iconify/vue'
import { useChatStore } from '@/stores/chat'
import { receiptsApi } from '@/api/receipts'
import { useChatAttachments } from '@/composables/chat/useChatAttachments'
import { loadDraft, saveDraft, clearDraft } from '@/utils/chat-draft'
import ChatMessageContent from '@/components/ChatMessageContent.vue'
import PromptStarters from '@/components/PromptStarters.vue'
import WorkflowBar from '@/components/WorkflowBar.vue'

const chat = useChatStore()
const currentScopeKey = computed(() => (chat.currentSessionId ? `session:${chat.currentSessionId}` : 'new'))
const inputText = ref(loadDraft('new'))
let draftTimer: ReturnType<typeof setTimeout> | null = null
const sessionDrawer = ref(false)
const isOnline = ref(typeof navigator !== 'undefined' ? navigator.onLine : true)
const listRef = ref<HTMLElement | null>(null)
const textareaRef = ref<HTMLTextAreaElement | null>(null)
const currentSession = computed(() => chat.sessions.find((s) => s.id === chat.currentSessionId) ?? null)

const {
  attachedImage,
  isScanningReceipt,
  handlePaste,
  handleFileSelect,
  removeAttachment,
  scanReceiptAttachment,
} = useChatAttachments({
  scanReceipt: (payload) => receiptsApi.scan(payload),
})

const isWorkflowCollapsed = computed(() => {
  return inputText.value.trim().length > 0 || attachedImage.value !== null
})

function handleOnline() { isOnline.value = true }
function handleOffline() { isOnline.value = false }

function flushDraft() {
  if (draftTimer) {
    clearTimeout(draftTimer)
    draftTimer = null
  }
  try {
    saveDraft(currentScopeKey.value, inputText.value)
  } catch {
    // localDb 不可用时静默降级
  }
}

// 输入防抖 300ms 写本地草稿；切换会话前先 flush，避免写入错误 scope
watch(inputText, () => {
  if (draftTimer) clearTimeout(draftTimer)
  draftTimer = setTimeout(() => {
    saveDraft(currentScopeKey.value, inputText.value)
    draftTimer = null
  }, 300)
})

function scrollToBottom() {
  nextTick(() => {
    const el = listRef.value
    if (!el) return
    el.scrollTop = chat.messages.length === 0 ? 0 : el.scrollHeight
  })
}

// 只监听最后一条消息 ID，避免历史分页前置加载触发误滚动（AGENTS.md 第 12 条）
watch(
  () => chat.messages[chat.messages.length - 1]?.id,
  () => scrollToBottom()
)

function handleActionExecuted(msg: any, payload: { draftToken: string }) {
  if (!msg || !payload?.draftToken) return
  const token = payload.draftToken
  if (msg.content && msg.content.includes(token)) {
    msg.content = msg.content
      .replace(/"status"\s*:\s*"pending"/g, '"status": "executed"')
      .replace(/"status"\s*:\s*"proposed"/g, '"status": "executed"')
  }
  if (msg.tool_calls && Array.isArray(msg.tool_calls)) {
    for (const tc of msg.tool_calls) {
      if (tc.result && typeof tc.result === 'string' && tc.result.includes(token)) {
        try {
          const parsed = JSON.parse(tc.result)
          parsed.status = 'executed'
          tc.result = JSON.stringify(parsed)
        } catch {
          // ignore
        }
      } else if (tc.result && typeof tc.result === 'object') {
        const obj = tc.result as Record<string, unknown>
        if (obj.draft_token === token) {
          obj.status = 'executed'
        }
      }
    }
  }
}

async function copyMessage(text: string) {
  try {
    await navigator.clipboard.writeText(text)
    ElMessage.success('已复制到剪贴板')
  } catch {
    ElMessage.error('复制失败，请手动复制')
  }
}

async function handleRegenerate() {
  if (chat.isStreaming) return
  await chat.regenerateLastMessage?.()
  scrollToBottom()
}

function handleStop() {
  if (!chat.isStreaming) return
  chat.abortCurrentStream()
}

async function handleRenameSession(s: { id: number; title?: string }) {
  try {
    const res = await ElMessageBox.prompt('请输入新的会话名称', '重命名会话', {
      confirmButtonText: '确定',
      cancelButtonText: '取消',
      inputValue: s.title || '',
    })
    const title = res.value?.trim()
    if (title) {
      await chat.renameSession?.(s.id, title)
      ElMessage.success('会话重命名成功')
    }
  } catch {
    /* cancelled */
  }
}

async function handleDeleteSession(id: number) {
  try {
    await ElMessageBox.confirm('确定删除此会话记录？', '确认删除', { type: 'warning' })
    await chat.deleteSessionById?.(id)
    ElMessage.success('会话已删除')
  } catch {
    /* cancelled */
  }
}

async function newSession() {
  flushDraft()
  await chat.createNewSession()
  inputText.value = loadDraft(currentScopeKey.value)
  sessionDrawer.value = false
  scrollToBottom()
}

async function selectSession(id: number) {
  flushDraft()
  await chat.selectSession(id)
  inputText.value = loadDraft(currentScopeKey.value)
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
  const oldScope = currentScopeKey.value
  if (draftTimer) {
    clearTimeout(draftTimer)
    draftTimer = null
  }
  const beforeLen = chat.messages.length
  await chat.sendMessage(text)
  // doSendMessage 把流式异常写入 assistant.error 且不 reject（stores/chat.ts:798-804），
  // 因此必须等 isStreaming 归 false 后按 assistant 消息判定成功。
  while (chat.isStreaming) await new Promise((resolve) => setTimeout(resolve, 50))
  const assistant = chat.messages.slice(beforeLen).find((m) => m.role === 'assistant')
  const failed = !assistant || Boolean(assistant.error)
  if (!failed) {
    clearDraft(oldScope)
    clearDraft(currentScopeKey.value)
    inputText.value = ''
  }
  scrollToBottom()
}

async function handleSendAction() {
  if (chat.isStreaming || isScanningReceipt.value || !isOnline.value) return
  const img = attachedImage.value
  if (!img) {
    await send()
    return
  }
  const rawText = inputText.value.trim()
  const oldScope = currentScopeKey.value
  if (draftTimer) {
    clearTimeout(draftTimer)
    draftTimer = null
  }
  attachedImage.value = null
  const finalPrompt = await scanReceiptAttachment(img, rawText)
  const beforeLen = chat.messages.length
  await chat.sendMessage(finalPrompt)
  while (chat.isStreaming) await new Promise((resolve) => setTimeout(resolve, 50))
  const assistant = chat.messages.slice(beforeLen).find((m) => m.role === 'assistant')
  const failed = !assistant || Boolean(assistant.error)
  if (!failed) {
    clearDraft(oldScope)
    clearDraft(currentScopeKey.value)
    inputText.value = ''
  }
  scrollToBottom()
}

onMounted(async () => {
  window.addEventListener('online', handleOnline)
  window.addEventListener('offline', handleOffline)
  try {
    await Promise.allSettled([
      chat.fetchSessions(),
      chat.fetchModels(),
      chat.fetchWorkflowsList?.(),
    ])
  } catch (e) {
    console.error('[ChatMobile] init failed:', e)
  }
  inputText.value = loadDraft(currentScopeKey.value)
  scrollToBottom()
})

onBeforeUnmount(() => {
  window.removeEventListener('online', handleOnline)
  window.removeEventListener('offline', handleOffline)
  try {
    flushDraft()
  } catch {
    // 确保草稿持久化异常不阻断中止流式响应
  }
  chat.abortCurrentStream()
})
</script>

<style scoped>
.chat-mobile { display: flex; flex-direction: column; height: 100%; min-height: 0; }
.chat-header { display: flex; align-items: center; justify-content: space-between; flex-shrink: 0; padding: 8px 12px; border-bottom: 1px solid var(--mf-border); gap: 8px; }
.chat-title { font-size: 14px; font-weight: 600; color: var(--mf-text-primary); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.message-list { flex: 1; min-height: 0; overflow-y: auto; padding: 12px; overscroll-behavior: contain; }
.message-row { margin-bottom: 14px; }
.message-row.user { padding-left: 14%; }
.message-row.assistant { padding-right: 4%; }
.message-error { margin-top: 4px; font-size: 12px; color: var(--mf-danger); }
.message-actions { display: flex; gap: 8px; margin-top: 6px; }
.msg-action-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 3px 8px;
  border-radius: 6px;
  border: 1px solid var(--mf-border);
  background: var(--mf-surface);
  color: var(--mf-text-muted);
  font-size: 11px;
  cursor: pointer;
}
.mobile-workflow-wrap { padding: 4px 10px 0; flex-shrink: 0; }
.input-bar { flex-shrink: 0; display: flex; flex-direction: column; gap: 8px; padding: 8px 12px; border-top: 1px solid var(--mf-border); background: var(--mf-surface); }
.offline-hint { font-size: 11px; color: var(--mf-warning); }
.attachment-pill {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 8px;
  border-radius: 8px;
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border);
}
.attachment-thumb { width: 34px; height: 34px; border-radius: 6px; object-fit: cover; flex-shrink: 0; }
.attachment-meta { flex: 1; min-width: 0; display: flex; flex-direction: column; }
.attachment-name { font-size: 12px; color: var(--mf-text-main); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.attachment-size { font-size: 11px; color: var(--mf-text-muted); }
.attachment-remove-btn {
  border: none;
  background: transparent;
  color: var(--mf-text-muted);
  cursor: pointer;
  padding: 4px;
}
.input-field { width: 100%; box-sizing: border-box; resize: none; border-radius: 8px; padding: 8px 10px; background: var(--mf-surface-muted); color: var(--mf-text-primary); border: 1px solid var(--mf-border); font-size: 14px; }
.input-actions-row { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
.input-tools-left { display: flex; align-items: center; gap: 6px; }
.tool-pill-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 5px 10px;
  border-radius: 999px;
  border: 1px solid var(--mf-border);
  background: var(--mf-surface-muted);
  color: var(--mf-text-muted);
  font-size: 12px;
  cursor: pointer;
}
.tool-pill-btn.active {
  color: var(--mf-primary);
  border-color: var(--mf-primary-border);
  background: var(--mf-primary-light);
}
.session-head { display: flex; align-items: center; justify-content: space-between; padding: 8px 0 12px; }
.session-item { display: flex; align-items: center; justify-content: space-between; gap: 8px; padding: 11px 8px; border-bottom: 1px solid var(--mf-border); font-size: 13px; cursor: pointer; }
.session-title { flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.session-item.active { color: var(--mf-primary); font-weight: 600; }
.session-item-actions { display: flex; align-items: center; gap: 4px; flex-shrink: 0; }
.session-act-btn { border: none; background: transparent; color: var(--mf-text-muted); padding: 4px 6px; cursor: pointer; font-size: 14px; }
.session-act-btn.danger { color: var(--mf-danger); }
.session-empty { padding: 24px 0; text-align: center; color: var(--mf-text-secondary); font-size: 13px; }
</style>
