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

.income-text { color: var(--mf-success); text-shadow: 0 0 8px var(--mf-success-light); }
.expense-text { color: var(--mf-danger); text-shadow: 0 0 8px var(--mf-danger-light); }

.highlight-card {
  border-color: var(--mf-primary-border);
  background: linear-gradient(135deg, var(--mf-primary-light) 0%, var(--mf-surface-card) 100%);
}

.highlight-card::before {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 3px;
  background: linear-gradient(90deg, var(--mf-primary), var(--mf-success));
  border-radius: var(--mf-radius-lg) var(--mf-radius-lg) 0 0;
}

.profit-card {
  background: var(--mf-success-light);
  border-color: var(--mf-success-border);
}

.loss-card {
  background: var(--mf-danger-light);
  border-color: var(--mf-danger-border);
}
</style>
