import { computed, onUnmounted, ref, type Ref } from 'vue'
import type { AiMessage } from '@/api/ai'
import { extractPanelItems } from '@/utils/chat-panel-extractor'
import type { PanelItem } from '@/types/chat-panel'

const PANEL_WIDTH_KEY = 'minefolio:analysis-panel-width'
const DEFAULT_PANEL_WIDTH = 420
const MIN_PANEL_WIDTH = 300
const MAX_PANEL_WIDTH = 600
const MAX_PANEL_ITEMS = 20

export interface UseChatAnalysisPanelOptions {
  messages: () => AiMessage[]
  isMobile: Ref<boolean>
}

/**
 * Manages the collapsible/resizable analysis panel tabs, extracted cards, and layout mode.
 */
export function useChatAnalysisPanel(options: UseChatAnalysisPanelOptions) {
  const panelVisible = ref(false)
  const panelItems = ref<PanelItem[]>([])
  const activeTabId = ref('')
  const panelOverlay = ref(false)
  const panelWidth = ref(DEFAULT_PANEL_WIDTH)

  let isResizing = false
  let startX = 0
  let startWidth = 0

  function loadPanelWidth() {
    try {
      const saved = localStorage.getItem(PANEL_WIDTH_KEY)
      if (saved) {
        const w = parseInt(saved, 10)
        if (!isNaN(w) && w >= MIN_PANEL_WIDTH && w <= MAX_PANEL_WIDTH) {
          panelWidth.value = w
        }
      }
    } catch { /* ignore */ }
  }

  function onPanelMouseMove(e: MouseEvent) {
    if (!isResizing) return
    const delta = startX - e.clientX
    panelWidth.value = Math.min(Math.max(startWidth + delta, MIN_PANEL_WIDTH), MAX_PANEL_WIDTH)
  }

  function stopPanelResize() {
    if (!isResizing) return
    isResizing = false
    if (typeof document !== 'undefined') {
      document.body.style.cursor = ''
      document.body.style.userSelect = ''
    }
    if (typeof window !== 'undefined') {
      window.removeEventListener('mousemove', onPanelMouseMove)
      window.removeEventListener('mouseup', stopPanelResize)
    }
    try {
      localStorage.setItem(PANEL_WIDTH_KEY, String(panelWidth.value))
    } catch { /* ignore */ }
  }

  function startPanelResize(e: MouseEvent) {
    if (panelOverlay.value || options.isMobile.value) return
    isResizing = true
    startX = e.clientX
    startWidth = panelWidth.value
    if (typeof document !== 'undefined') {
      document.body.style.cursor = 'col-resize'
      document.body.style.userSelect = 'none'
    }
    if (typeof window !== 'undefined') {
      window.addEventListener('mousemove', onPanelMouseMove)
      window.addEventListener('mouseup', stopPanelResize)
    }
  }

  function checkPanelMode() {
    if (typeof window === 'undefined') return
    panelOverlay.value = !options.isMobile.value && window.innerWidth < 1400
  }

  // 抽取卡片映射表：messageId -> segmentId[]
  // 在移动端模式下强制返回空映射，使所有结构化卡片直接全宽内联渲染
  const extractedSegmentIdMap = computed(() => {
    if (options.isMobile.value) return {}
    const map: Record<number, string[]> = {}
    for (const item of panelItems.value) {
      if (item.messageId !== undefined && item.segmentId) {
        if (!map[item.messageId]) map[item.messageId] = []
        map[item.messageId]!.push(item.segmentId)
      }
    }
    return map
  })

  function appendUniquePanelItems(items: PanelItem[], idSet: Set<string>) {
    for (const item of items) {
      if (!idSet.has(item.id)) {
        if (panelItems.value.length >= MAX_PANEL_ITEMS) {
          panelItems.value.shift()
        }
        panelItems.value.push(item)
        idSet.add(item.id)
      }
    }
  }

  function scanAllMessagesToPanel() {
    const idSet = new Set(panelItems.value.map(i => i.id))
    for (const msg of options.messages()) {
      if (msg.role !== 'assistant') continue
      const res = extractPanelItems(
        msg.id,
        msg.content,
        msg.workflowData,
        msg.workflowConfig,
        msg.tool_calls
      )
      appendUniquePanelItems(res.panelItems, idSet)
    }
  }

  function syncLatestAssistantMessage(lastMsg: AiMessage | undefined) {
    if (!lastMsg || lastMsg.role !== 'assistant') return
    const res = extractPanelItems(
      lastMsg.id,
      lastMsg.content,
      lastMsg.workflowData,
      lastMsg.workflowConfig,
      lastMsg.tool_calls
    )
    if (res.panelItems.length === 0) return

    const idSet = new Set(panelItems.value.map(i => i.id))
    appendUniquePanelItems(res.panelItems, idSet)

    if (!options.isMobile.value) {
      const latest = res.panelItems[res.panelItems.length - 1]
      if (latest) activeTabId.value = latest.id
      panelVisible.value = true
    }
  }

  function resetPanel() {
    panelItems.value = []
    panelVisible.value = false
    activeTabId.value = ''
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

  onUnmounted(() => {
    stopPanelResize()
  })

  return {
    panelVisible,
    panelItems,
    activeTabId,
    panelOverlay,
    panelWidth,
    loadPanelWidth,
    startPanelResize,
    stopPanelResize,
    checkPanelMode,
    extractedSegmentIdMap,
    scanAllMessagesToPanel,
    syncLatestAssistantMessage,
    resetPanel,
    handleOpenPanelItem,
    handleRemovePanelTab,
  }
}
