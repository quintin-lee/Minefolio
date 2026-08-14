<!-- frontend/src/components/SummaryCard.vue -->
<template>
  <div class="summary-card" :class="[typeClass, extraClass]">
    <div class="summary-label">{{ label }}</div>
    <div class="summary-value" :class="valueClass">{{ value }}</div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{
  label: string
  value: string | number
  type?: 'neutral' | 'income' | 'expense' | 'highlight'
  extraClass?: string
}>()

const typeClass = computed(() => {
  const map: Record<string, string> = {
    income: 'income-text',
    expense: 'expense-text',
    highlight: 'highlight-card',
  }
  return map[props.type ?? 'neutral'] ?? ''
})

const valueClass = computed(() => {
  const map: Record<string, string> = {
    income: 'income-text',
    expense: 'expense-text',
    highlight: '',
  }
  return map[props.type ?? 'neutral'] ?? ''
})
</script>

<style scoped>
.summary-card {
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  padding: 20px 24px;
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
  transition: all 0.2s ease;
}

.summary-card:hover {
  border-color: var(--mf-border-hover);
  box-shadow: var(--mf-shadow-glow);
}

.summary-label {
  font-size: 13px;
  color: var(--mf-text-muted);
  margin-bottom: 8px;
  font-weight: 500;
}

.summary-value {
  font-size: 26px;
  font-weight: 700;
  letter-spacing: -0.5px;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  color: var(--mf-text-main);
}

.income-text { color: #34d399; text-shadow: 0 0 8px rgba(52,211,153,0.4); }
.expense-text { color: #f87171; text-shadow: 0 0 8px rgba(248,113,113,0.3); }

.highlight-card {
  border-color: rgba(0, 212, 255, 0.3);
  background: linear-gradient(135deg, rgba(0, 212, 255, 0.06) 0%, rgba(15, 23, 42, 0.8) 100%);
}

.highlight-card::before {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 3px;
  background: linear-gradient(90deg, #00d4ff, #34d399);
  border-radius: var(--mf-radius-lg) var(--mf-radius-lg) 0 0;
}

.profit-card {
  background: rgba(52, 211, 153, 0.08);
  border-color: rgba(52, 211, 153, 0.3);
}

.loss-card {
  background: rgba(239, 68, 68, 0.08);
  border-color: rgba(239, 68, 68, 0.3);
}
</style>
