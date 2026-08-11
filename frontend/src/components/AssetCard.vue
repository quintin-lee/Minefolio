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
  background: #ffffff;
  border-radius: 16px;
  overflow: hidden;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.04);
  transition: all 0.3s ease;
  cursor: pointer;
}

.asset-card:hover {
  transform: translateY(-4px);
  box-shadow: 0 12px 24px rgba(0, 0, 0, 0.08);
}

.card-accent {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 4px;
  background: linear-gradient(90deg, #3b82f6 0%, #2563eb 100%);
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
  color: #94a3b8;
  font-weight: 500;
}

.value-amount {
  font-size: 24px;
  font-weight: 700;
  color: #1e293b;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  letter-spacing: -0.5px;
}
</style>
