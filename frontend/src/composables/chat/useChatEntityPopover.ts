import { onMounted, onUnmounted, ref } from 'vue'
import { useFinancialEntities, type EntityDetail } from '@/composables/useFinancialEntities'

export interface EntityTargetPayload {
  el: HTMLElement
  type: 'asset' | 'account'
  id: string
  name: string
  rect: DOMRect
}

export interface UseChatEntityPopoverOptions {
  onNavigate: (payload: { type: 'asset' | 'account'; id: string | number }) => void
}

/**
 * Manages hover, click, and outside-dismiss state for the chat financial entity popover.
 */
export function useChatEntityPopover(options: UseChatEntityPopoverOptions) {
  const { loadEntities, getEntityDetail, getCollisionEntities } = useFinancialEntities()

  const popoverVisible = ref(false)
  const popoverTargetRect = ref<DOMRect | null>(null)
  const popoverDetail = ref<EntityDetail | null>(null)
  const popoverCollision = ref<{ asset?: EntityDetail; account?: EntityDetail } | null>(null)

  let popoverHoverTimer: number | null = null
  let popoverLeaveTimer: number | null = null

  function clearPopoverHoverTimer() {
    if (popoverHoverTimer !== null) {
      clearTimeout(popoverHoverTimer)
      popoverHoverTimer = null
    }
  }

  function clearPopoverLeaveTimer() {
    if (popoverLeaveTimer !== null) {
      clearTimeout(popoverLeaveTimer)
      popoverLeaveTimer = null
    }
  }

  function closePopover() {
    if (popoverVisible.value) {
      popoverVisible.value = false
    }
  }

  function applyEntityTarget(payload: EntityTargetPayload) {
    popoverDetail.value = getEntityDetail(payload.type, payload.id)
    popoverCollision.value = getCollisionEntities(payload.name)
    popoverTargetRect.value = payload.rect
    popoverVisible.value = true
  }

  function handleEntityHover(payload: EntityTargetPayload) {
    clearPopoverLeaveTimer()
    clearPopoverHoverTimer()
    popoverHoverTimer = window.setTimeout(() => {
      applyEntityTarget(payload)
    }, 180)
  }

  function handleEntityLeave() {
    clearPopoverHoverTimer()
    clearPopoverLeaveTimer()
    popoverLeaveTimer = window.setTimeout(() => {
      popoverVisible.value = false
    }, 220)
  }

  function handleEntityClick(payload: EntityTargetPayload) {
    clearPopoverHoverTimer()
    clearPopoverLeaveTimer()
    const detail = getEntityDetail(payload.type, payload.id)
    const isSameTarget =
      popoverVisible.value &&
      popoverDetail.value?.id === detail?.id &&
      popoverDetail.value?.type === detail?.type

    if (isSameTarget) {
      popoverVisible.value = false
      return
    }
    applyEntityTarget(payload)
  }

  function handlePopoverNavigate(payload: { type: 'asset' | 'account'; id: string | number }) {
    popoverVisible.value = false
    options.onNavigate(payload)
  }

  function onGlobalPointerDown(e: PointerEvent) {
    if (!popoverVisible.value) return
    const target = e.target as HTMLElement | null
    if (!target) return
    if (target.closest('.financial-entity-popover') || target.closest('.mf-entity-badge')) {
      return
    }
    popoverVisible.value = false
  }

  onMounted(() => {
    if (typeof window !== 'undefined') {
      window.addEventListener('pointerdown', onGlobalPointerDown)
    }
  })

  onUnmounted(() => {
    clearPopoverHoverTimer()
    clearPopoverLeaveTimer()
    if (typeof window !== 'undefined') {
      window.removeEventListener('pointerdown', onGlobalPointerDown)
    }
  })

  return {
    loadEntities,
    popoverVisible,
    popoverTargetRect,
    popoverDetail,
    popoverCollision,
    clearPopoverLeaveTimer,
    closePopover,
    handleEntityHover,
    handleEntityLeave,
    handleEntityClick,
    handlePopoverNavigate,
  }
}
