<template>
  <div class="workflow-bar-compact">
    <!-- Single-Row Pill Bar (36px) -->
    <div class="pill-bar-track">
      <div class="pill-tag">
        <Icon icon="ph:lightning-bold" class="tag-icon" />
        <span>快捷工作流</span>
      </div>

      <div class="pills-container">
        <button
          v-for="wf in pinnedWorkflows"
          :key="wf.id"
          class="wf-pill"
          :class="{ 'is-disabled': chatStore.isStreaming }"
          :title="wf.description"
          @click="triggerWorkflow(wf)"
        >
          <Icon :icon="wf.icon || 'ph:git-merge'" class="pill-icon" />
          <span class="pill-title">{{ wf.title }}</span>
        </button>
      </div>

      <button class="all-drawer-btn" @click="drawerVisible = true">
        <Icon icon="ph:squares-four" />
        <span>全部 ({{ chatStore.workflows.length }})</span>
        <Icon icon="ph:caret-right" />
      </button>
    </div>

    <!-- All Workflows Drawer -->
    <el-drawer
      v-model="drawerVisible"
      title="智能财务分析工作流库"
      size="440px"
      direction="rtl"
      append-to-body
      class="workflow-library-drawer"
    >
      <div class="drawer-inner">
        <!-- Search & Filter -->
        <div class="drawer-search-row">
          <el-input
            v-model="searchQuery"
            placeholder="搜索工作流..."
            clearable
            size="small"
          >
            <template #prefix>
              <Icon icon="ph:magnifying-glass" />
            </template>
          </el-input>
        </div>

        <!-- Workflow Grid -->
        <div class="drawer-cards-grid">
          <div
            v-for="wf in drawerFilteredWorkflows"
            :key="wf.id"
            class="library-card"
            :class="{ 'is-disabled': chatStore.isStreaming }"
            @click="triggerFromDrawer(wf)"
          >
            <div class="card-top">
              <div class="card-icon-box">
                <Icon :icon="wf.icon || 'ph:git-merge'" />
              </div>
              <div class="card-meta">
                <div class="card-title">{{ wf.title }}</div>
                <div class="card-desc">{{ wf.description }}</div>
              </div>
              <button
                class="pin-action-btn"
                :class="{ pinned: isPinned(wf.id) }"
                :title="isPinned(wf.id) ? '取消快捷置顶' : '固定到常用栏'"
                @click.stop="chatStore.togglePinWorkflow(wf.id)"
              >
                <Icon :icon="isPinned(wf.id) ? 'ph:star-fill' : 'ph:star'" />
              </button>
            </div>
            <div class="card-bottom">
              <span class="steps-tag">{{ wf.step_count || wf.steps?.length || 4 }} 步流水线</span>
              <span class="mode-tag">{{ isDirect(wf.id) ? '一键执行' : '参数配置' }}</span>
            </div>
          </div>
        </div>
      </div>
    </el-drawer>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { Icon } from '@iconify/vue'
import { useChatStore } from '@/stores/chat'
import type { WorkflowDef } from '@/types'

const chatStore = useChatStore()
const drawerVisible = ref(false)
const searchQuery = ref('')

const DIRECT_RUN_IDS = new Set(['wf_portfolio_rebalance', 'wf_goal_tracker', 'wf_bill_calendar', 'wf_health_score'])
function isDirect(id: string) {
  return DIRECT_RUN_IDS.has(id)
}

function isPinned(id: string) {
  return chatStore.pinnedWorkflowIds.includes(id)
}

const pinnedWorkflows = computed(() => {
  return chatStore.workflows.filter(w => chatStore.pinnedWorkflowIds.includes(w.id))
})

const drawerFilteredWorkflows = computed(() => {
  const q = searchQuery.value.trim().toLowerCase()
  if (!q) return chatStore.workflows
  return chatStore.workflows.filter(w =>
    w.title.toLowerCase().includes(q) ||
    w.description.toLowerCase().includes(q)
  )
})

function triggerWorkflow(wf: WorkflowDef) {
  if (chatStore.isStreaming) return
  if (isDirect(wf.id)) {
    chatStore.runWorkflow(wf.id)
  } else {
    chatStore.stageWorkflow(wf.id)
  }
}

function triggerFromDrawer(wf: WorkflowDef) {
  drawerVisible.value = false
  triggerWorkflow(wf)
}

onMounted(async () => {
  if (chatStore.workflows.length === 0) {
    await chatStore.fetchWorkflowsList()
  }
})
</script>

<style scoped>
.workflow-bar-compact {
  margin-bottom: 8px;
}

.pill-bar-track {
  display: flex;
  align-items: center;
  gap: 8px;
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border);
  border-radius: var(--mf-radius-md, 8px);
  padding: 4px 8px;
  height: 36px;
  backdrop-filter: blur(8px);
}

.pill-tag {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 11.5px;
  font-weight: 600;
  color: var(--mf-primary, #00d4ff);
  flex-shrink: 0;
}

.tag-icon {
  font-size: 13px;
}

.pills-container {
  display: flex;
  align-items: center;
  gap: 6px;
  overflow-x: auto;
  scrollbar-width: none;
  flex: 1;
}

.pills-container::-webkit-scrollbar {
  display: none;
}

.wf-pill {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 3px 10px;
  background: var(--mf-surface-hover);
  border: 1px solid var(--mf-border);
  border-radius: 12px;
  font-size: 11.5px;
  color: var(--mf-text-muted);
  cursor: pointer;
  white-space: nowrap;
  transition: all 0.15s ease;
}

.wf-pill:hover:not(.is-disabled) {
  background: var(--mf-primary-light);
  border-color: var(--mf-primary-border);
  color: var(--mf-text-main);
  transform: translateY(-1px);
}

.wf-pill.is-disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.pill-icon {
  font-size: 12px;
  color: var(--mf-primary, #00d4ff);
}

.all-drawer-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 3px 8px;
  background: transparent;
  border: none;
  font-size: 11px;
  color: #94a3b8;
  cursor: pointer;
  flex-shrink: 0;
  transition: color 0.15s ease;
}

.all-drawer-btn:hover {
  color: var(--mf-primary, #00d4ff);
}

/* Drawer styles */
.drawer-inner {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.drawer-cards-grid {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.library-card {
  background: rgba(2, 6, 23, 0.7);
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 8px;
  padding: 10px 12px;
  cursor: pointer;
  transition: all 0.15s ease;
}

.library-card:hover:not(.is-disabled) {
  border-color: rgba(0, 212, 255, 0.35);
  background: rgba(0, 212, 255, 0.05);
}

.card-top {
  display: flex;
  align-items: flex-start;
  gap: 10px;
}

.card-icon-box {
  width: 28px;
  height: 28px;
  border-radius: 6px;
  background: rgba(0, 212, 255, 0.1);
  color: var(--mf-primary, #00d4ff);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 16px;
  flex-shrink: 0;
}

.card-meta {
  flex: 1;
  min-width: 0;
}

.card-title {
  font-size: 12.5px;
  font-weight: 600;
  color: #f1f5f9;
}

.card-desc {
  font-size: 11px;
  color: #94a3b8;
  margin-top: 2px;
  line-height: 1.4;
}

.pin-action-btn {
  background: transparent;
  border: none;
  color: #64748b;
  font-size: 15px;
  cursor: pointer;
  padding: 2px;
  transition: color 0.15s ease;
}

.pin-action-btn:hover, .pin-action-btn.pinned {
  color: #f59e0b;
}

.card-bottom {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-top: 8px;
  padding-top: 6px;
  border-top: 1px solid rgba(255, 255, 255, 0.05);
  font-size: 10.5px;
  color: #64748b;
}

.steps-tag {
  color: #94a3b8;
}

.mode-tag {
  color: var(--mf-primary, #00d4ff);
}
</style>
