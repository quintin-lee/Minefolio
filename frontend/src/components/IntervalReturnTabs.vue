<template>
  <div class="interval-return-tabs">
    <div
      v-for="item in intervals"
      :key="item.key"
      :class="['interval-item', item.valid ? (item.value >= 0 ? 'gain' : 'loss') : 'muted']"
    >
      <div class="interval-label">{{ item.label }}</div>
      <div class="interval-value">
        <template v-if="item.valid">
          {{ item.value >= 0 ? '+' : '' }}{{ item.value.toFixed(2) }}%
        </template>
        <template v-else>—</template>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { IntervalReturns } from '@/types'

const props = defineProps<{ data: IntervalReturns }>()

const intervals = computed(() => [
  { key: '1m', label: '近1月', value: props.data.ret_1m, valid: !!props.data.valid_1m },
  { key: '3m', label: '近3月', value: props.data.ret_3m, valid: !!props.data.valid_3m },
  { key: '6m', label: '近6月', value: props.data.ret_6m, valid: !!props.data.valid_6m },
  { key: '1y', label: '近1年', value: props.data.ret_1y, valid: !!props.data.valid_1y },
  { key: 'all', label: '全部', value: props.data.ret_all, valid: !!props.data.valid_all },
])
</script>

<style scoped>
.interval-return-tabs {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
}

.interval-item {
  flex: 1;
  min-width: 80px;
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border);
  border-radius: var(--mf-radius-md);
  padding: 8px 12px;
  text-align: center;
}

.interval-label {
  font-size: 11px;
  color: var(--mf-text-muted);
  margin-bottom: 4px;
}

.interval-value {
  font-size: 15px;
  font-weight: 700;
  font-family: 'JetBrains Mono', monospace;
}

.gain .interval-value { color: var(--mf-gain); }
.loss .interval-value { color: var(--mf-loss); }
.muted .interval-value { color: var(--mf-text-muted); }
</style>
