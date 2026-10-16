<template>
  <div
    class="index-card"
    :class="[
      direction === 'up' ? 'flash-up' : direction === 'down' ? 'flash-down' : '',
      isPositive ? 'trend-positive' : 'trend-negative'
    ]"
    @click="handleClick"
  >
    <div class="card-header">
      <div class="name-box">
        <span class="region-flag">{{ item.region_flag }}</span>
        <span class="index-name">{{ item.name }}</span>
      </div>
      <span class="index-symbol">{{ item.symbol }}</span>
    </div>

    <div class="card-price-row">
      <div class="price-val">
        {{ formatNumber(item.current_price) }}
      </div>
      <div class="price-change-pill" :class="isPositive ? 'pill-up' : 'pill-down'">
        <span>{{ isPositive ? '+' : '' }}{{ formatChange(item.change_amount) }}</span>
        <span>({{ isPositive ? '+' : '' }}{{ formatChange(item.change_percent) }}%)</span>
      </div>
    </div>

    <!-- Mini SVG Sparkline -->
    <div class="card-sparkline">
      <svg viewBox="0 0 200 40" preserveAspectRatio="none" class="sparkline-svg">
        <defs>
          <linearGradient :id="`grad-${item.symbol}`" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" :stop-color="isPositive ? '#67c23a' : '#f56c6c'" stop-opacity="0.3" />
            <stop offset="100%" :stop-color="isPositive ? '#67c23a' : '#f56c6c'" stop-opacity="0.0" />
          </linearGradient>
        </defs>
        <polygon :points="areaPoints" :fill="`url(#grad-${item.symbol})`" />
        <polyline
          :points="linePoints"
          fill="none"
          :stroke="isPositive ? '#67c23a' : '#f56c6c'"
          stroke-width="2"
          stroke-linecap="round"
          stroke-linejoin="round"
        />
      </svg>
    </div>

    <div class="card-footer-stats">
      <span>{{ t('market.indicesData.low') }}: {{ formatNumber(item.low_price) }}</span>
      <span>{{ t('market.indicesData.high') }}: {{ formatNumber(item.high_price) }}</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useI18n } from '@/composables/useI18n'
import type { GlobalIndexItem } from '@/types'

const props = defineProps<{
  item: GlobalIndexItem
  direction?: 'up' | 'down' | 'same'
}>()

const emit = defineEmits<{
  (e: 'select', item: GlobalIndexItem): void
}>()

const { t } = useI18n()

const isPositive = computed(() => Number(props.item.change_percent) >= 0)

function formatNumber(val: number | string | undefined | null) {
  if (val === undefined || val === null || val === '') return '0.00'
  const n = Number(val)
  if (isNaN(n)) return '0.00'
  return n.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 })
}

function formatChange(val: number | string | undefined | null) {
  if (val === undefined || val === null || val === '') return '0.00'
  const n = Number(val)
  if (isNaN(n)) return '0.00'
  return n.toFixed(2)
}

function handleClick() {
  emit('select', props.item)
}

const linePoints = computed(() => {
  const rawPts = props.item.sparkline
  let pts: number[] = []
  if (Array.isArray(rawPts)) {
    pts = rawPts.map((v) => Number(v) || 0)
  } else if (typeof rawPts === 'string') {
    try {
      const parsed = JSON.parse(rawPts)
      pts = Array.isArray(parsed) ? parsed.map((v) => Number(v) || 0) : []
    } catch {
      pts = []
    }
  }
  if (!pts || pts.length < 2) return '0,20 200,20'
  const min = Math.min(...pts)
  const max = Math.max(...pts)
  const range = max - min || 1

  return pts
    .map((val, idx) => {
      const x = (idx / (pts.length - 1)) * 200
      const y = 35 - ((val - min) / range) * 30
      return `${x.toFixed(1)},${y.toFixed(1)}`
    })
    .join(' ')
})

const areaPoints = computed(() => {
  const pts = linePoints.value
  return `0,40 ${pts} 200,40`
})
</script>

<style scoped>
.index-card {
  background: var(--el-bg-color-overlay);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 10px;
  padding: 14px;
  cursor: pointer;
  transition: all 0.25s ease;
  position: relative;
  overflow: hidden;
}
.index-card:hover {
  transform: translateY(-2px);
  box-shadow: 0 4px 14px rgba(0, 0, 0, 0.08);
  border-color: var(--el-color-primary-light-5);
}
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
}
.name-box {
  display: flex;
  align-items: center;
  gap: 6px;
}
.region-flag {
  font-size: 16px;
}
.index-name {
  font-size: 14px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}
.index-symbol {
  font-size: 11px;
  color: var(--el-text-color-secondary);
  font-family: monospace;
}
.card-price-row {
  display: flex;
  justify-content: space-between;
  align-items: baseline;
  margin-bottom: 6px;
}
.price-val {
  font-size: 22px;
  font-weight: 700;
  font-family: var(--el-font-family);
  color: var(--el-text-color-primary);
}
.price-change-pill {
  display: flex;
  gap: 4px;
  font-size: 12px;
  font-weight: 600;
  padding: 2px 6px;
  border-radius: 4px;
}
.pill-up {
  color: var(--el-color-success);
  background: var(--el-color-success-light-9);
}
.pill-down {
  color: var(--el-color-danger);
  background: var(--el-color-danger-light-9);
}
.card-sparkline {
  height: 40px;
  width: 100%;
  margin: 6px 0;
}
.sparkline-svg {
  width: 100%;
  height: 100%;
}
.card-footer-stats {
  display: flex;
  justify-content: space-between;
  font-size: 11px;
  color: var(--el-text-color-secondary);
}
.flash-up {
  animation: flash-green 1.2s ease;
}
.flash-down {
  animation: flash-red 1.2s ease;
}
@keyframes flash-green {
  0% { background: var(--el-color-success-light-8); }
  100% { background: var(--el-bg-color-overlay); }
}
@keyframes flash-red {
  0% { background: var(--el-color-danger-light-8); }
  100% { background: var(--el-bg-color-overlay); }
}
</style>
