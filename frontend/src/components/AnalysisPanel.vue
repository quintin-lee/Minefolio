<template>
  <aside
    class="analysis-panel"
    :class="{ visible, overlay }"
    :style="panelStyle"
    role="complementary"
    aria-label="AI 分析结果面板"
  >
    <!-- 拖拽调整宽度的左边缘把手 (仅在桌面内联模式下可用) -->
    <div
      v-if="!overlay"
      class="panel-resize-handle"
      title="拖拽调整面板宽度"
      @mousedown="$emit('resize-start', $event)"
    >
      <div class="resize-indicator"></div>
    </div>

    <!-- 面板顶部工具栏 -->
    <div class="panel-header">
      <div class="header-left">
        <div class="panel-icon-wrap">
          <Icon icon="ph:sparkle-fill" class="panel-sparkle-icon" />
        </div>
        <span class="panel-title">分析结果</span>
        <span v-if="items.length > 0" class="panel-item-badge">
          {{ items.length }}
        </span>
      </div>
      <button
        class="panel-close-btn"
        @click="$emit('close')"
        title="收起分析面板"
        aria-label="收起分析面板"
      >
        <Icon icon="ph:x-bold" />
      </button>
    </div>

    <!-- 面板内容主体 -->
    <div class="panel-body">
      <!-- 空状态 -->
      <div v-if="items.length === 0" class="panel-empty-state">
        <div class="empty-icon-wrap">
          <Icon icon="ph:chart-polar" class="empty-icon" />
        </div>
        <p class="empty-title">暂无分析结果</p>
        <p class="empty-desc">
          AI 对话中生成的图表、财务报表、操作卡片与工作流将在此实时呈现。
        </p>
      </div>

      <!-- 标签页及内容 -->
      <div v-else class="panel-tabs-container">
        <el-tabs
          v-model="currentTab"
          type="card"
          closable
          class="analysis-tabs"
          @tab-remove="handleTabRemove"
        >
          <el-tab-pane
            v-for="item in items"
            :key="item.id"
            :name="item.id"
          >
            <template #label>
              <span class="tab-label-custom" :title="item.label">
                <Icon :icon="item.icon || 'ph:file-text'" class="tab-icon" />
                <span class="tab-text">{{ item.label }}</span>
              </span>
            </template>

            <div class="tab-content-scroller">
              <!-- Mermaid 图表 -->
              <MermaidBlock
                v-if="item.type === 'mermaid'"
                :code="item.content"
              />

              <!-- Action 操作卡片 -->
              <div v-else-if="item.type === 'action' && item.actionData" class="panel-action-wrap">
                <ActionCard :action-data="item.actionData" />
              </div>

              <!-- 工作流进度卡片 -->
              <WorkflowProgressCard
                v-else-if="item.type === 'workflow-progress' && item.workflowData"
                :workflow-data="item.workflowData"
              />

              <!-- 工作流配置卡片 -->
              <WorkflowConfigCard
                v-else-if="item.type === 'workflow-config' && item.workflowConfig"
                :message-id="item.messageId"
                :config="item.workflowConfig"
              />

              <!-- 代码块 -->
              <CodeBlock
                v-else-if="item.type === 'code'"
                :code="item.content"
                :lang="item.lang"
              />

              <!-- Markdown 表格或富文本段落 -->
              <div
                v-else-if="item.type === 'markdown'"
                class="markdown-part panel-markdown"
                v-html="renderMarkdown(item.content)"
              ></div>
            </div>
          </el-tab-pane>
        </el-tabs>
      </div>
    </div>
  </aside>
</template>

<script setup lang="ts">
import { computed, defineAsyncComponent } from 'vue'
import { marked } from 'marked'
import DOMPurify from 'dompurify'
import { Icon } from '@iconify/vue'
import ActionCard from '@/components/ActionCard.vue'
import WorkflowProgressCard from '@/components/WorkflowProgressCard.vue'
import WorkflowConfigCard from '@/components/WorkflowConfigCard.vue'
import type { PanelItem } from '@/types/chat-panel'

const MermaidBlock = defineAsyncComponent(() => import('@/components/MermaidBlock.vue'))
const CodeBlock = defineAsyncComponent(() => import('@/components/CodeBlock.vue'))

const props = withDefaults(
  defineProps<{
    visible: boolean
    items: PanelItem[]
    activeTabId: string
    overlay?: boolean
    width?: number
  }>(),
  {
    overlay: false,
    width: 420
  }
)

const emit = defineEmits<{
  (e: 'close'): void
  (e: 'update:activeTabId', id: string): void
  (e: 'remove-tab', id: string): void
  (e: 'resize-start', event: MouseEvent): void
}>()

const currentTab = computed({
  get: () => props.activeTabId,
  set: (val: string) => emit('update:activeTabId', val)
})

const panelStyle = computed(() => {
  if (props.overlay) {
    return {
      width: `${Math.min(props.width || 420, 520)}px`
    }
  }
  return {
    width: props.visible ? `${props.width || 420}px` : '0px'
  }
})

function handleTabRemove(tabId: string | number) {
  emit('remove-tab', String(tabId))
}

function renderMarkdown(text?: string): string {
  if (!text) return ''
  try {
    const rawHtml = marked.parse(text, { async: false, breaks: true }) as string
    return DOMPurify.sanitize(rawHtml)
  } catch {
    return DOMPurify.sanitize(text.replace(/</g, '&lt;').replace(/>/g, '&gt;'))
  }
}
</script>

<style scoped>
.analysis-panel {
  position: relative;
  height: 100%;
  display: flex;
  flex-direction: column;
  background: var(--mf-surface);
  backdrop-filter: blur(20px);
  border-left: 1px solid var(--mf-border);
  transition: width 0.25s cubic-bezier(0.4, 0, 0.2, 1), transform 0.25s cubic-bezier(0.4, 0, 0.2, 1);
  overflow: hidden;
  z-index: 8;
  box-sizing: border-box;
}

.analysis-panel:not(.visible):not(.overlay) {
  width: 0 !important;
  border-left: none;
}

/* 抽屉浮层模式 */
.analysis-panel.overlay {
  position: fixed;
  top: 0;
  right: 0;
  bottom: 0;
  height: 100vh;
  z-index: 120;
  box-shadow: -8px 0 32px rgba(0, 0, 0, 0.2);
  transform: translateX(100%);
}

.analysis-panel.overlay.visible {
  transform: translateX(0);
}

/* 拖拽把手 */
.panel-resize-handle {
  position: absolute;
  left: 0;
  top: 0;
  bottom: 0;
  width: 6px;
  cursor: col-resize;
  z-index: 12;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: background 0.18s;
}

.panel-resize-handle:hover {
  background: var(--mf-primary-light);
}

.panel-resize-handle:hover .resize-indicator {
  background: var(--mf-primary);
}

.resize-indicator {
  width: 2px;
  height: 28px;
  border-radius: 1px;
  background: transparent;
  transition: background 0.18s;
}

/* 顶部栏 */
.panel-header {
  height: 52px;
  min-height: 52px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 16px;
  border-bottom: 1px solid var(--mf-border);
  flex-shrink: 0;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 8px;
}

.panel-icon-wrap {
  width: 26px;
  height: 26px;
  border-radius: 8px;
  background: var(--mf-primary-light);
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--mf-primary);
}

.panel-sparkle-icon {
  font-size: 14px;
}

.panel-title {
  font-size: 14.5px;
  font-weight: 700;
  color: var(--mf-text-main);
  letter-spacing: -0.2px;
}

.panel-item-badge {
  font-size: 11px;
  font-family: var(--mf-font-mono, monospace);
  padding: 1px 7px;
  border-radius: 10px;
  background: var(--mf-primary-light);
  color: var(--mf-primary);
  font-weight: 600;
  border: 1px solid var(--mf-primary-border);
}

.panel-close-btn {
  background: transparent;
  border: none;
  color: var(--mf-text-muted);
  width: 28px;
  height: 28px;
  border-radius: 6px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 15px;
  cursor: pointer;
  transition: color 0.15s, background 0.15s;
}

.panel-close-btn:hover {
  color: var(--mf-text-main);
  background: var(--mf-surface-hover);
}

/* 主体容器 */
.panel-body {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  position: relative;
}

/* 空状态 */
.panel-empty-state {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 32px 24px;
  text-align: center;
}

.empty-icon-wrap {
  width: 56px;
  height: 56px;
  border-radius: 16px;
  background: var(--mf-surface-card);
  border: 1px solid var(--mf-border);
  display: flex;
  align-items: center;
  justify-content: center;
  margin-bottom: 16px;
  color: var(--mf-text-muted);
  box-shadow: var(--mf-shadow-sm);
}

.empty-icon {
  font-size: 28px;
  opacity: 0.7;
}

.empty-title {
  font-size: 14px;
  font-weight: 600;
  color: var(--mf-text-main);
  margin: 0 0 6px;
}

.empty-desc {
  font-size: 12.5px;
  color: var(--mf-text-muted);
  line-height: 1.6;
  margin: 0;
  max-width: 280px;
}

/* 标签页样式微调 */
.panel-tabs-container {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.analysis-tabs {
  height: 100%;
  display: flex;
  flex-direction: column;
}

:deep(.el-tabs__header) {
  margin: 0 !important;
  background: var(--mf-surface-card);
  border-bottom: 1px solid var(--mf-border);
  padding: 4px 8px 0;
}

:deep(.el-tabs__nav-wrap) {
  overflow-x: auto;
}

:deep(.el-tabs__item) {
  height: 34px !important;
  line-height: 34px !important;
  font-size: 12.5px !important;
  padding: 0 12px !important;
  border-radius: 8px 8px 0 0 !important;
  color: var(--mf-text-muted) !important;
  border-color: var(--mf-border) !important;
  transition: all 0.18s ease !important;
}

:deep(.el-tabs__item:hover) {
  color: var(--mf-text-main) !important;
}

:deep(.el-tabs__item.is-active) {
  color: var(--mf-primary) !important;
  font-weight: 600 !important;
  background: var(--mf-surface) !important;
  border-bottom-color: var(--mf-surface) !important;
}

:deep(.el-tabs__content) {
  flex: 1;
  overflow: hidden;
  padding: 0 !important;
}

:deep(.el-tab-pane) {
  height: 100%;
  overflow: hidden;
}

.tab-label-custom {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  max-width: 120px;
}

.tab-icon {
  font-size: 13px;
  flex-shrink: 0;
}

.tab-text {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.tab-content-scroller {
  height: 100%;
  overflow-y: auto;
  padding: 16px;
  box-sizing: border-box;
}

.panel-action-wrap {
  margin-top: 4px;
}

.panel-markdown {
  font-size: 13.5px;
  line-height: 1.65;
}

.panel-markdown :deep(table) {
  width: 100%;
  border-collapse: separate;
  border-spacing: 0;
  margin: 10px 0;
  font-size: 12.5px;
  border-radius: 8px;
  overflow: hidden;
  border: 1px solid var(--mf-border);
}

.panel-markdown :deep(th),
.panel-markdown :deep(td) {
  padding: 8px 12px;
  border-right: 1px solid var(--mf-border);
  border-bottom: 1px solid var(--mf-border);
}

.panel-markdown :deep(th) {
  background: var(--mf-surface-muted);
  font-weight: 600;
}
</style>
