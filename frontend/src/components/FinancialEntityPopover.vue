<template>
  <div
    v-if="visible && activeDetail"
    ref="popoverRef"
    class="financial-entity-popover"
    :style="popoverStyle"
    role="dialog"
    aria-label="金融实体概览"
    @mouseenter="$emit('mouseenter')"
    @mouseleave="$emit('mouseleave')"
  >
    <!-- Collision Disambiguation Tabs -->
    <div v-if="hasCollision" class="popover-tabs">
      <button
        type="button"
        class="tab-btn"
        :class="{ active: activeType === 'asset' }"
        @click="activeType = 'asset'"
      >
        <Icon icon="ph:coins-bold" />
        <span>资产标的</span>
      </button>
      <button
        type="button"
        class="tab-btn"
        :class="{ active: activeType === 'account' }"
        @click="activeType = 'account'"
      >
        <Icon icon="ph:wallet-bold" />
        <span>账户容器</span>
      </button>
    </div>

    <!-- Header -->
    <div class="popover-header">
      <div class="header-icon" :class="activeDetail.type">
        <Icon :icon="activeDetail.icon || (activeDetail.type === 'asset' ? 'ph:coins-bold' : 'ph:bank-bold')" />
      </div>
      <div class="header-info">
        <div class="entity-title" :title="activeDetail.name">
          <span>{{ activeDetail.name }}</span>
        </div>
        <div class="entity-subtitle">
          <span v-if="activeDetail.type === 'asset'">
            {{ activeDetail.assetType || '资产' }} · 所属 {{ activeDetail.accountName || '默认账户' }}
          </span>
          <span v-else>
            {{ activeDetail.institution || '账户' }} · 包含 {{ activeDetail.assetCount || 0 }} 项资产
          </span>
        </div>
      </div>
      <span class="type-badge" :class="activeDetail.type">
        {{ activeDetail.type === 'asset' ? '资产' : '账户' }}
      </span>
      <button
        type="button"
        class="popover-close-btn"
        title="关闭"
        aria-label="关闭微卡"
        @click.stop="$emit('close')"
      >
        <Icon icon="ph:x-bold" />
      </button>
    </div>

    <!-- Metrics Body -->
    <div class="popover-body">
      <!-- Valuation / Balance Metric -->
      <div class="metric-row main-metric">
        <span class="metric-label">{{ activeDetail.type === 'asset' ? '当前估值' : '账户总值' }}</span>
        <span class="metric-value mono-val">
          {{ formatCurrency(activeDetail.currentValue ?? 0) }}
        </span>
      </div>

      <!-- Asset Holding & PnL (for assets) -->
      <template v-if="activeDetail.type === 'asset'">
        <div v-if="activeDetail.floatingPnl !== undefined && activeDetail.costBasis !== undefined" class="metric-row pnl-metric">
          <span class="metric-label">浮动盈亏</span>
          <span
            class="metric-value mono-val"
            :class="(activeDetail.floatingPnl ?? 0) > 0 ? 'gain' : (activeDetail.floatingPnl ?? 0) < 0 ? 'loss' : 'neutral'"
          >
            {{ formatSigned(activeDetail.floatingPnl ?? 0) }}
            <span v-if="activeDetail.floatingPct !== undefined" class="pnl-pct">
              ({{ (activeDetail.floatingPct ?? 0) >= 0 ? '+' : '' }}{{ Number(activeDetail.floatingPct ?? 0).toFixed(2) }}%)
            </span>
          </span>
        </div>

        <div v-if="activeDetail.quantity || activeDetail.costBasis" class="meta-chips-row">
          <span v-if="activeDetail.quantity" class="meta-chip">
            份额: {{ Number(activeDetail.quantity).toLocaleString() }}
          </span>
          <span v-if="activeDetail.costBasis" class="meta-chip">
            成本: {{ formatCurrency(activeDetail.costBasis) }}
          </span>
        </div>
      </template>
    </div>

    <!-- Footer Action -->
    <div class="popover-footer">
      <button
        type="button"
        class="popover-action-btn"
        @click="handleNavigate"
      >
        <span>{{ activeDetail.type === 'asset' ? '前往资产详情' : '查看账户下资产' }}</span>
        <Icon icon="ph:arrow-right-bold" />
      </button>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { Icon } from '@iconify/vue'
import type { EntityDetail } from '@/composables/useFinancialEntities'
import { formatCurrency, formatSigned } from '@/utils/format'

const props = defineProps<{
  visible: boolean
  targetRect: DOMRect | null
  detail: EntityDetail | null
  collision?: { asset?: EntityDetail; account?: EntityDetail } | null
}>()

const emit = defineEmits<{
  (e: 'close'): void
  (e: 'navigate', payload: { type: 'asset' | 'account'; id: string | number }): void
  (e: 'mouseenter'): void
  (e: 'mouseleave'): void
}>()

const popoverRef = ref<HTMLElement | null>(null)
const activeType = ref<'asset' | 'account'>('asset')

const hasCollision = computed(() => {
  return !!(props.collision?.asset && props.collision?.account)
})

watch(
  () => [props.visible, props.detail, props.collision],
  () => {
    if (props.collision?.asset && props.collision?.account) {
      activeType.value = 'asset'
    } else if (props.detail) {
      activeType.value = props.detail.type
    }
  },
  { immediate: true }
)

const activeDetail = computed<EntityDetail | null>(() => {
  if (hasCollision.value) {
    return activeType.value === 'account' ? (props.collision?.account || null) : (props.collision?.asset || null)
  }
  return props.detail
})

const popoverStyle = computed(() => {
  if (!props.targetRect) {
    return { display: 'none' }
  }

  const rect = props.targetRect
  const popoverWidth = 310
  const popoverHeight = hasCollision.value ? 230 : 185
  const margin = 8

  // Calculate horizontal position (centered or constrained within window)
  let left = rect.left + rect.width / 2 - popoverWidth / 2
  if (left < 12) left = 12
  if (left + popoverWidth > window.innerWidth - 12) {
    left = window.innerWidth - popoverWidth - 12
  }

  // Calculate vertical position (prefer above, fallback below)
  let top = rect.top - popoverHeight - margin
  if (top < 50) {
    top = rect.bottom + margin
  }
  if (top + popoverHeight > window.innerHeight - 12) {
    top = Math.max(12, window.innerHeight - popoverHeight - 12)
  }

  return {
    top: `${Math.round(top)}px`,
    left: `${Math.round(left)}px`,
    width: `${popoverWidth}px`,
  }
})

function handleNavigate() {
  if (!activeDetail.value) return
  emit('navigate', {
    type: activeDetail.value.type,
    id: activeDetail.value.id,
  })
}
</script>

<style scoped>
.financial-entity-popover {
  position: fixed;
  z-index: 2500;
  background: var(--mf-background-elevated);
  backdrop-filter: blur(20px);
  border: 1px solid var(--mf-border);
  border-radius: 12px;
  box-shadow: var(--mf-shadow-lg);
  padding: 12px 14px;
  animation: popoverFadeIn 0.15s cubic-bezier(0.16, 1, 0.3, 1);
  display: flex;
  flex-direction: column;
  gap: 10px;
}

@keyframes popoverFadeIn {
  from {
    opacity: 0;
    transform: scale(0.96) translateY(4px);
  }
  to {
    opacity: 1;
    transform: scale(1) translateY(0);
  }
}

.popover-tabs {
  display: flex;
  background: var(--mf-surface-hover);
  border-radius: 8px;
  padding: 2px;
  gap: 2px;
}

.tab-btn {
  flex: 1;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 4px;
  padding: 3px 8px;
  border: none;
  background: transparent;
  border-radius: 6px;
  font-size: 11px;
  font-weight: 500;
  color: var(--mf-text-muted);
  cursor: pointer;
  transition: all 0.12s ease;
}

.tab-btn.active {
  background: var(--mf-background-elevated);
  color: var(--mf-primary);
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.08);
}

.popover-header {
  display: flex;
  align-items: center;
  gap: 10px;
}

.header-icon {
  width: 32px;
  height: 32px;
  border-radius: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 16px;
  flex-shrink: 0;
}

.header-icon.asset {
  background: var(--mf-primary-light);
  color: var(--mf-primary);
}

.header-icon.account {
  background: rgba(16, 185, 129, 0.12);
  color: #10b981;
}

.header-info {
  flex: 1;
  min-width: 0;
}

.entity-title {
  font-size: 13.5px;
  font-weight: 600;
  color: var(--mf-text-main);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.entity-subtitle {
  font-size: 11px;
  color: var(--mf-text-muted);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  margin-top: 1px;
}

.type-badge {
  font-size: 10px;
  padding: 1px 6px;
  border-radius: 6px;
  font-weight: 600;
  flex-shrink: 0;
}

.type-badge.asset {
  background: var(--mf-primary-light);
  color: var(--mf-primary);
}

.type-badge.account {
  background: rgba(16, 185, 129, 0.12);
  color: #10b981;
}

.popover-body {
  display: flex;
  flex-direction: column;
  gap: 6px;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 8px;
  padding: 8px 10px;
}

.metric-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.metric-label {
  font-size: 11.5px;
  color: var(--mf-text-muted);
}

.metric-value {
  font-size: 12.5px;
  font-weight: 600;
  color: var(--mf-text-main);
}

.mono-val {
  font-family: var(--mf-font-mono, monospace);
}

.metric-value.gain {
  color: var(--mf-gain, #10b981);
}

.metric-value.loss {
  color: var(--mf-loss, #ef4444);
}

.metric-value.neutral {
  color: var(--mf-text-muted);
}

.pnl-pct {
  font-size: 11px;
  margin-left: 3px;
  font-weight: 500;
}

.meta-chips-row {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
  padding-top: 2px;
  border-top: 1px dashed var(--mf-border);
}

.meta-chip {
  font-size: 10.5px;
  color: var(--mf-text-muted);
  font-family: var(--mf-font-mono, monospace);
}

.popover-footer {
  display: flex;
}

.popover-action-btn {
  width: 100%;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 5px;
  padding: 6px 12px;
  background: var(--mf-primary-light);
  border: 1px solid var(--mf-primary-border);
  color: var(--mf-primary);
  border-radius: 8px;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.15s ease;
}

.popover-action-btn:hover {
  background: var(--mf-primary);
  color: #ffffff;
}

.popover-close-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 22px;
  height: 22px;
  border-radius: 6px;
  border: none;
  background: transparent;
  color: var(--mf-text-muted);
  cursor: pointer;
  transition: all 0.12s ease;
  font-size: 13px;
  flex-shrink: 0;
}

.popover-close-btn:hover {
  background: var(--mf-surface-hover);
  color: var(--mf-text-main);
}
</style>
