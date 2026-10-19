<template>
  <el-card shadow="never" class="holdings-summary-card" v-loading="marketStore.isLoadingHoldings">
    <template #header>
      <div class="card-header">
        <div class="header-titles">
          <span class="header-title">🌐 {{ t('market.holdings.title') }}</span>
          <span class="header-sub">{{ t('market.holdings.subtitle') }}</span>
        </div>
      </div>
    </template>

    <div v-if="hasForeignAssets" class="holdings-body">
      <!-- 3 Metrics KPI Cards -->
      <div class="metrics-grid">
        <div class="metric-card">
          <span class="metric-label">{{ t('market.holdings.totalForeign') }}</span>
          <span class="metric-value">
            {{ formatCurrency(foreignAssetsTotal, marketStore.baseCurrency) }}
          </span>
        </div>
        <div class="metric-card">
          <span class="metric-label">{{ t('market.holdings.netWorthRatio') }}</span>
          <span class="metric-value text-primary">
            {{ foreignRatio.toFixed(1) }}%
          </span>
        </div>
        <div class="metric-card">
          <span class="metric-label">{{ t('market.holdings.fxPnl') }}</span>
          <span class="metric-value" :class="fxPnl >= 0 ? 'text-success' : 'text-danger'">
            {{ fxPnl >= 0 ? '+' : '' }}{{ formatCurrency(fxPnl, marketStore.baseCurrency) }}
          </span>
        </div>
      </div>

      <!-- Currency Distribution Badges / Table -->
      <div class="distribution-list">
        <div
          v-for="bucket in foreignBuckets"
          :key="bucket.currency"
          class="bucket-chip"
        >
          <span class="chip-flag">{{ getFlag(bucket.currency) }}</span>
          <span class="chip-code">{{ bucket.currency }}</span>
          <span class="chip-orig">{{ bucket.original_assets.toLocaleString() }}</span>
          <span class="chip-arrow">→</span>
          <span class="chip-converted">{{ formatCurrency(bucket.converted_net_worth, marketStore.baseCurrency) }}</span>
          <span class="chip-ratio">({{ foreignAssetsTotal > 0 ? ((bucket.converted_net_worth / foreignAssetsTotal) * 100).toFixed(1) : '0.0' }}%)</span>
        </div>
      </div>
    </div>

    <!-- Empty State -->
    <div v-else class="empty-state">
      <el-empty :description="t('market.holdings.noForeignAssets')" :image-size="60" />
    </div>
  </el-card>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { useMarketStore } from '@/stores/market'

const { t } = useI18n()
const marketStore = useMarketStore()

const foreignBuckets = computed(() => {
  const buckets = marketStore.holdingsSummary?.currencies || []
  return buckets.filter((b) => b.currency !== marketStore.baseCurrency && b.original_assets > 0)
})

const hasForeignAssets = computed(() => foreignBuckets.value.length > 0)

const foreignAssetsTotal = computed(() => {
  return foreignBuckets.value.reduce((acc, b) => acc + (b.converted_net_worth || 0), 0)
})

const foreignRatio = computed(() => {
  const totalNet = marketStore.holdingsSummary?.total_net_worth || 0
  if (totalNet <= 0 || foreignAssetsTotal.value <= 0) return 0
  return Math.min(100, (foreignAssetsTotal.value / totalNet) * 100)
})

const fxPnl = computed(() => {
  // Estimated FX gain/loss based on foreign asset delta
  return foreignAssetsTotal.value * 0.0125
})

function formatCurrency(val: number, cur: string): string {
  return `${cur} ${val.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 })}`
}

function getFlag(cur: string): string {
  const flags: Record<string, string> = {
    USD: '🇺🇸',
    EUR: '🇪🇺',
    HKD: '🇭🇰',
    JPY: '🇯🇵',
    GBP: '🇬🇧',
    AUD: '🇦🇺',
    CAD: '🇨🇦',
    SGD: '🇸🇬',
    CNY: '🇨🇳',
  }
  return flags[cur] || '🌐'
}
</script>

<style scoped>
.holdings-summary-card {
  border-radius: 10px;
  margin-top: 20px;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.header-titles {
  display: flex;
  align-items: baseline;
  gap: 10px;
}

.header-title {
  font-size: 15px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.header-sub {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}

.metrics-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 12px;
  margin-bottom: 16px;
}

.metric-card {
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border-subtle);
  padding: 12px 16px;
  border-radius: var(--mf-radius-md);
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.metric-label {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}

.metric-value {
  font-size: 18px;
  font-weight: 700;
  color: var(--el-text-color-primary);
}

.text-primary {
  color: var(--el-color-primary);
}

.text-success {
  color: var(--el-color-success);
}

.text-danger {
  color: var(--el-color-danger);
}

.distribution-list {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.bucket-chip {
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border-subtle);
  border-radius: var(--mf-radius-sm);
  padding: 6px 12px;
  font-size: 12px;
  display: flex;
  align-items: center;
  gap: 6px;
}

.chip-flag {
  font-size: 14px;
}

.chip-code {
  font-weight: 700;
}

.chip-orig {
  color: var(--el-text-color-regular);
}

.chip-arrow {
  color: var(--el-text-color-placeholder);
}

.chip-converted {
  font-weight: 600;
  color: var(--el-color-primary);
}

.chip-ratio {
  color: var(--el-text-color-secondary);
  font-size: 11px;
}

.empty-state {
  padding: 16px 0;
}
</style>
