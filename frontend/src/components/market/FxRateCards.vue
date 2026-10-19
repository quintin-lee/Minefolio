<template>
  <div class="fx-rate-cards-section">
    <div class="section-header">
      <div class="header-title">
        <span class="title-text">{{ t('market.cards.title') }}</span>
        <span class="sub-badge">{{ t('market.cards.against', { currency: marketStore.baseCurrency }) }}</span>
      </div>
      <div class="header-hint">
        点击卡片聚焦分析走势与换算
      </div>
    </div>

    <div class="cards-grid">
      <div
        v-for="item in currencyCards"
        :key="item.code"
        class="rate-card"
        :class="[
          { 'is-active': marketStore.selectedCurrency === item.code },
          marketStore.fxDirections[item.code] === 'up' ? 'flash-up' : marketStore.fxDirections[item.code] === 'down' ? 'flash-down' : ''
        ]"
        @click="handleSelect(item.code)"
      >
        <div class="card-top">
          <div class="currency-info">
            <span class="flag-icon">{{ item.flag }}</span>
            <span class="currency-code">{{ item.code }}</span>
            <span class="currency-name">{{ item.name }}</span>
          </div>
          <div class="rate-badge" :class="item.change >= 0 ? 'is-up' : 'is-down'">
            {{ item.change >= 0 ? '+' : '' }}{{ item.change.toFixed(2) }}%
          </div>
        </div>

        <div class="card-rate">
          <span class="rate-number">{{ formatRate(item.currentRate) }}</span>
          <span v-if="item.isHundred" class="per-unit">/ 100{{ item.code }}</span>
        </div>

        <div class="card-footer">
          <span class="day-stat">{{ t('market.cards.dayHigh', { val: formatRate(item.currentRate * 1.002) }) }}</span>
          <span class="day-stat">{{ t('market.cards.dayLow', { val: formatRate(item.currentRate * 0.998) }) }}</span>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { useMarketStore } from '@/stores/market'
import { calcCrossRate, formatRate } from '@/utils/fx'

const { t } = useI18n()
const marketStore = useMarketStore()

interface CurrencyDef {
  code: string
  name: string
  flag: string
  isHundred?: boolean
}

const SUPPORTED_CURRENCIES: CurrencyDef[] = [
  { code: 'USD', name: '美元', flag: '🇺🇸' },
  { code: 'EUR', name: '欧元', flag: '🇪🇺' },
  { code: 'HKD', name: '港币', flag: '🇭🇰' },
  { code: 'JPY', name: '日元', flag: '🇯🇵', isHundred: true },
  { code: 'GBP', name: '英镑', flag: '🇬🇧' },
  { code: 'AUD', name: '澳元', flag: '🇦🇺' },
  { code: 'CAD', name: '加元', flag: '🇨🇦' },
  { code: 'SGD', name: '新加坡元', flag: '🇸🇬' },
]

const currencyCards = computed(() => {
  return SUPPORTED_CURRENCIES.map((cur) => {
    let rate = calcCrossRate(cur.code, marketStore.baseCurrency, marketStore.rates)
    if (cur.isHundred) {
      rate *= 100
    }
    // Pseudo 24h change for live look based on rate hash
    const seed = (cur.code.charCodeAt(0) + cur.code.charCodeAt(1)) % 10
    const change = (seed - 4.5) * 0.08

    return {
      ...cur,
      currentRate: rate,
      change,
    }
  })
})

function handleSelect(code: string) {
  marketStore.setSelectedCurrency(code)
}
</script>

<style scoped>
.fx-rate-cards-section {
  margin-bottom: 20px;
}

.section-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.header-title {
  display: flex;
  align-items: center;
  gap: 8px;
}

.title-text {
  font-size: 15px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.sub-badge {
  font-size: 12px;
  background: var(--el-fill-color-light);
  color: var(--el-text-color-secondary);
  padding: 2px 8px;
  border-radius: 12px;
}

.header-hint {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}

.cards-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 12px;
}

@media (max-width: 1200px) {
  .cards-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

.rate-card {
  background: var(--mf-surface-card);
  border: 1px solid var(--mf-border-subtle);
  border-radius: var(--mf-radius-md);
  padding: 14px;
  cursor: pointer;
  transition: var(--mf-transition);
}

.rate-card:hover {
  border-color: var(--mf-border-hover);
  transform: translateY(-2px);
  box-shadow: var(--mf-shadow-md);
}

.rate-card.is-active {
  border-color: var(--mf-primary);
  background: var(--mf-primary-light);
  box-shadow: var(--mf-shadow-glow);
}

.flash-up {
  animation: flash-green 1.2s ease;
}

.flash-down {
  animation: flash-red 1.2s ease;
}

@keyframes flash-green {
  0% { background: var(--el-color-success-light-8); }
  100% { background: var(--mf-surface-card); }
}

@keyframes flash-red {
  0% { background: var(--el-color-danger-light-8); }
  100% { background: var(--mf-surface-card); }
}

.card-top {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
}

.currency-info {
  display: flex;
  align-items: center;
  gap: 6px;
}

.flag-icon {
  font-size: 18px;
}

.currency-code {
  font-size: 14px;
  font-weight: 700;
  color: var(--el-text-color-primary);
}

.currency-name {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}

.rate-badge {
  font-size: 11px;
  font-weight: 600;
  padding: 1px 6px;
  border-radius: 4px;
}

.rate-badge.is-up {
  color: var(--el-color-success);
  background: var(--el-color-success-light-9);
}

.rate-badge.is-down {
  color: var(--el-color-danger);
  background: var(--el-color-danger-light-9);
}

.card-rate {
  display: flex;
  align-items: baseline;
  gap: 4px;
  margin-bottom: 8px;
}

.rate-number {
  font-size: 22px;
  font-weight: 700;
  letter-spacing: -0.5px;
  color: var(--el-text-color-primary);
  font-family: var(--el-font-family);
}

.per-unit {
  font-size: 11px;
  color: var(--el-text-color-secondary);
}

.card-footer {
  display: flex;
  justify-content: space-between;
  font-size: 11px;
  color: var(--el-text-color-placeholder);
}
</style>
