<template>
  <div v-if="visible" class="workflow-slash-menu" role="menu" aria-label="快捷工作流选择">
    <div class="menu-header">
      <Icon icon="ph:lightning-bold" class="header-icon" />
      <span>快捷工作流指令 (按 ↑↓ 导航，Enter 确认，Esc 退出)</span>
    </div>
    <div class="menu-list" ref="listRef">
      <div
        v-for="(wf, idx) in filteredWorkflows"
        :key="wf.id"
        class="menu-item"
        :class="{ active: selectedIndex === idx }"
        @click="selectItem(wf)"
        @mouseenter="selectedIndex = idx"
      >
        <div class="item-icon">
          <Icon :icon="wf.icon || 'ph:git-merge'" />
        </div>
        <div class="item-info">
          <div class="item-title">{{ wf.title }}</div>
          <div class="item-desc">{{ wf.description }}</div>
        </div>
        <span class="item-tag">{{ isDirect(wf.id) ? '快捷启动' : '参数配置' }}</span>
      </div>
      <div v-if="filteredWorkflows.length === 0" class="menu-empty">
        未找到匹配的工作流
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, nextTick } from 'vue'
import { Icon } from '@iconify/vue'
import { useChatStore } from '@/stores/chat'
import type { WorkflowDef } from '@/types'

const props = defineProps<{
  visible: boolean
  query: string
}>()

const emit = defineEmits<{
  (e: 'select', wf: WorkflowDef): void
  (e: 'close'): void
}>()

const chatStore = useChatStore()
const selectedIndex = ref(0)
const listRef = ref<HTMLElement | null>(null)

const DIRECT_RUN_IDS = new Set(['wf_portfolio_rebalance', 'wf_goal_tracker', 'wf_bill_calendar', 'wf_health_score'])
function isDirect(id: string) {
  return DIRECT_RUN_IDS.has(id)
}

const filteredWorkflows = computed(() => {
  const q = props.query.trim().toLowerCase()
  if (!q) return chatStore.workflows
  return chatStore.workflows.filter(w =>
    w.title.toLowerCase().includes(q) ||
    w.description.toLowerCase().includes(q) ||
    w.id.toLowerCase().includes(q)
  )
})

watch(() => props.query, () => {
  selectedIndex.value = 0
})

function selectItem(wf: WorkflowDef) {
  emit('select', wf)
}

function handleKeyDown(e: KeyboardEvent): boolean {
  if (!props.visible) return false
  if (e.key === 'ArrowDown') {
    e.preventDefault()
    selectedIndex.value = (selectedIndex.value + 1) % Math.max(1, filteredWorkflows.value.length)
    scrollToActive()
    return true
  } else if (e.key === 'ArrowUp') {
    e.preventDefault()
    selectedIndex.value = (selectedIndex.value - 1 + filteredWorkflows.value.length) % Math.max(1, filteredWorkflows.value.length)
    scrollToActive()
    return true
  } else if (e.key === 'Enter') {
    e.preventDefault()
    if (filteredWorkflows.value[selectedIndex.value]) {
      selectItem(filteredWorkflows.value[selectedIndex.value]!)
    }
    return true
  } else if (e.key === 'Escape') {
    e.preventDefault()
    emit('close')
    return true
  }
  return false
}

function scrollToActive() {
  nextTick(() => {
    const el = listRef.value?.querySelector('.menu-item.active') as HTMLElement | null
    if (el && listRef.value) {
      el.scrollIntoView({ block: 'nearest' })
    }
  })
}

defineExpose({ handleKeyDown })
</script>

<style scoped>
.workflow-slash-menu {
  position: absolute;
  bottom: calc(100% + 8px);
  left: 0;
  width: 380px;
  max-width: 90vw;
  background: var(--mf-background-elevated);
  backdrop-filter: blur(12px);
  border: 1px solid var(--mf-primary-border);
  border-radius: 8px;
  box-shadow: var(--mf-shadow-lg);
  overflow: hidden;
  z-index: 100;
  animation: slideUp 0.15s ease-out;
}

@keyframes slideUp {
  from { opacity: 0; transform: translateY(6px); }
  to { opacity: 1; transform: translateY(0); }
}

.menu-header {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 8px 12px;
  font-size: 11px;
  color: var(--mf-primary);
  background: var(--mf-primary-light);
  border-bottom: 1px solid var(--mf-primary-border);
  font-weight: 500;
}

.header-icon {
  font-size: 13px;
}

.menu-list {
  max-height: 240px;
  overflow-y: auto;
  padding: 4px;
}

.menu-item:hover, .menu-item.active {
  background: var(--mf-primary-light);
}

.item-icon {
  width: 26px;
  height: 26px;
  border-radius: 6px;
  background: var(--mf-primary-light);
  color: var(--mf-primary);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 14px;
  flex-shrink: 0;
}

.item-info {
  flex: 1;
  min-width: 0;
}

.item-title {
  font-size: 12.5px;
  font-weight: 600;
  color: #f1f5f9;
}

.item-desc {
  font-size: 11px;
  color: #94a3b8;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.item-tag {
  font-size: 10px;
  padding: 1px 6px;
  border-radius: 8px;
  background: rgba(255, 255, 255, 0.06);
  color: #94a3b8;
  flex-shrink: 0;
}

.menu-empty {
  padding: 16px;
  text-align: center;
  font-size: 12px;
  color: #64748b;
}
</style>
