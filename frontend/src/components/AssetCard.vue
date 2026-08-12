<template>
  <div class="asset-card">
    <div class="card-accent"></div>
    <div class="card-content">
      <div class="asset-name">{{ asset.name }}</div>
      <div class="asset-value">
        <span class="currency-symbol">{{ currency }}</span>
        <span class="value-amount">{{ asset.current_value.toFixed(2) }}</span>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
interface Asset {
  name: string
  current_value: number
  currency?: string
}

const props = defineProps<{ asset: Asset }>()

const currency = props.asset.currency ?? '¥'
</script>

<style scoped>
.asset-card {
  position: relative;
  background: var(--mf-surface);
  border-radius: 16px;
  overflow: hidden;
  border: 1px solid var(--mf-border);
  box-shadow: var(--mf-shadow-sm);
  transition: all 0.3s ease;
  cursor: pointer;
}

.asset-card:hover {
  transform: translateY(-4px);
  border-color: var(--mf-border-hover);
  box-shadow: var(--mf-shadow-glow);
}

.card-accent {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 3px;
  background: linear-gradient(90deg, #00d4ff, #38bdf8);
  box-shadow: 0 0 10px rgba(0, 212, 255, 0.5);
}

.card-content {
  padding: 20px 24px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.asset-name {
  font-size: 14px;
  font-weight: 500;
  color: #64748b;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.asset-value {
  display: flex;
  align-items: baseline;
  gap: 2px;
}

.currency-symbol {
  font-size: 16px;
  color: #475569;
  font-weight: 500;
}

.value-amount {
  font-size: 24px;
  font-weight: 700;
  color: #00d4ff;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  letter-spacing: -0.5px;
  text-shadow: 0 0 12px rgba(0, 212, 255, 0.5);
}
</style>
