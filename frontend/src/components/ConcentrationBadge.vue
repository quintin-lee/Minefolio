<template>
  <div :class="['concentration-badge', level]">
    <span class="badge-label">集中度 HHI</span>
    <span class="badge-value">{{ (hhi * 100).toFixed(0) }}%</span>
    <span v-if="maxHoldingWeight > 0.5" class="badge-warn">
      最大持仓 {{ (maxHoldingWeight * 100).toFixed(0) }}%
    </span>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{
  hhi: number
  maxHoldingWeight: number
}>()

const level = computed(() => {
  if (props.hhi >= 0.5) return 'high'
  if (props.hhi >= 0.25) return 'medium'
  return 'low'
})
</script>

<style scoped>
.concentration-badge {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 4px 12px;
  border-radius: 999px;
  font-size: 12px;
  font-weight: 600;
}

.concentration-badge.low {
  background: rgba(52, 211, 153, 0.12);
  color: #34d399;
}

.concentration-badge.medium {
  background: rgba(251, 191, 36, 0.12);
  color: #fbbf24;
}

.concentration-badge.high {
  background: rgba(244, 63, 94, 0.12);
  color: #f43f5e;
}

.badge-label {
  opacity: 0.7;
}

.badge-value {
  font-family: 'JetBrains Mono', monospace;
}

.badge-warn {
  font-size: 11px;
  opacity: 0.85;
}
</style>
