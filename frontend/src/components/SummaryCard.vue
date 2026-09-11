<!-- frontend/src/components/SummaryCard.vue -->
<template>
  <div class="summary-card" :class="[typeClass, extraClass]">
    <div class="card-glow-mesh" :class="glowClass" />
    <div class="card-header">
      <div class="summary-label">{{ label }}</div>
      <div v-if="badge" class="trend-badge" :class="badgeClass">
        <span class="badge-arrow" v-if="badgeArrow">{{ badgeArrow }}</span>
        <span>{{ badge }}</span>
      </div>
    </div>
    <div class="summary-value tabular-nums" :class="valueClass">{{ value }}</div>
    <div v-if="subtext" class="summary-subtext">{{ subtext }}</div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{
  label: string
  value: string | number
  type?: 'neutral' | 'income' | 'expense' | 'highlight'
  badge?: string
  badgeType?: 'success' | 'danger' | 'warning' | 'primary' | 'neutral'
  subtext?: string
  extraClass?: string
}>()

const typeClass = computed(() => {
  const map: Record<string, string> = {
    income: 'is-income',
    expense: 'is-expense',
    highlight: 'highlight-card',
  }
  return map[props.type ?? 'neutral'] ?? ''
})

const glowClass = computed(() => {
  if (props.type === 'income') return 'glow-income'
  if (props.type === 'expense') return 'glow-expense'
  if (props.type === 'highlight') return 'glow-highlight'
  return 'glow-neutral'
})

const valueClass = computed(() => {
  const map: Record<string, string> = {
    income: 'income-text',
    expense: 'expense-text',
    highlight: 'highlight-text',
  }
  return map[props.type ?? 'neutral'] ?? ''
})

const badgeClass = computed(() => {
  const t = props.badgeType ?? (props.badge?.startsWith('+') ? 'success' : props.badge?.startsWith('-') ? 'danger' : 'neutral')
  return `badge-${t}`
})

const badgeArrow = computed(() => {
  if (!props.badge) return ''
  if (props.badge.startsWith('+')) return '↑'
  if (props.badge.startsWith('-')) return '↓'
  return ''
})
</script>

<style scoped>
.summary-card {
  position: relative;
  overflow: hidden;
  background: var(--mf-surface-card);
  backdrop-filter: blur(16px);
  -webkit-backdrop-filter: blur(16px);
  border-radius: var(--mf-radius-lg);
  padding: 20px 24px;
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
  transition: transform 0.22s cubic-bezier(0.4, 0, 0.2, 1),
              box-shadow 0.22s cubic-bezier(0.4, 0, 0.2, 1),
              border-color 0.22s ease;
}

.summary-card:hover {
  transform: translateY(-2px);
  border-color: var(--mf-border-hover);
  box-shadow: var(--mf-shadow-glow);
}

.card-glow-mesh {
  position: absolute;
  top: -24px;
  right: -24px;
  width: 90px;
  height: 90px;
  border-radius: 50%;
  filter: blur(36px);
  pointer-events: none;
  opacity: 0.35;
  transition: opacity 0.25s ease;
}

.summary-card:hover .card-glow-mesh {
  opacity: 0.6;
}

.glow-neutral { background: var(--mf-primary); }
.glow-income { background: var(--mf-success); }
.glow-expense { background: var(--mf-danger); }
.glow-highlight { background: var(--mf-accent); }

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 8px;
}

.summary-label {
  font-size: 13px;
  color: var(--mf-text-muted);
  font-weight: 500;
  letter-spacing: 0.02em;
}

.trend-badge {
  display: inline-flex;
  align-items: center;
  gap: 2px;
  font-size: 11px;
  font-weight: 600;
  padding: 2px 7px;
  border-radius: var(--mf-radius-pill);
  font-family: var(--mf-font-mono);
  line-height: 1.2;
}

.badge-success {
  background: var(--mf-success-light);
  color: var(--mf-success);
  border: 1px solid var(--mf-success-border);
}

.badge-danger {
  background: var(--mf-danger-light);
  color: var(--mf-danger);
  border: 1px solid var(--mf-danger-border);
}

.badge-warning {
  background: var(--mf-warning-light);
  color: var(--mf-warning);
  border: 1px solid var(--mf-warning-border);
}

.badge-primary {
  background: var(--mf-primary-light);
  color: var(--mf-primary);
  border: 1px solid var(--mf-primary-border);
}

.badge-neutral {
  background: var(--mf-surface-muted);
  color: var(--mf-text-muted);
  border: 1px solid var(--mf-border);
}

.summary-value {
  font-size: 26px;
  font-weight: 700;
  letter-spacing: -0.5px;
  font-family: var(--mf-font-mono);
  color: var(--mf-text-main);
  line-height: 1.2;
}

.summary-subtext {
  font-size: 12px;
  color: var(--mf-text-placeholder);
  margin-top: 6px;
}

.income-text {
  color: var(--mf-success);
}

.expense-text {
  color: var(--mf-danger);
}

.highlight-text {
  color: var(--mf-primary);
}

[data-theme="dark"] .income-text {
  text-shadow: 0 0 12px var(--mf-success-light);
}

[data-theme="dark"] .expense-text {
  text-shadow: 0 0 12px var(--mf-danger-light);
}

[data-theme="dark"] .highlight-text {
  text-shadow: 0 0 12px var(--mf-primary-light);
}

.highlight-card {
  border-color: var(--mf-primary-border);
  background: linear-gradient(135deg, rgba(59, 130, 246, 0.08) 0%, var(--mf-surface-card) 100%);
}

.highlight-card::before {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 2px;
  background: linear-gradient(90deg, var(--mf-primary), var(--mf-accent));
  border-radius: var(--mf-radius-lg) var(--mf-radius-lg) 0 0;
}

.profit-card {
  background: var(--mf-success-light) !important;
  border-color: var(--mf-success-border) !important;
}

.loss-card {
  background: var(--mf-danger-light) !important;
  border-color: var(--mf-danger-border) !important;
}
</style>
