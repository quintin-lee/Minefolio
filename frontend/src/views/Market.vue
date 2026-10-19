<template>
  <div class="market-view-container">
    <!-- Top Hub Header -->
    <div class="market-header">
      <div class="header-left">
        <h1 class="page-title">📊 {{ t('market.title') }}</h1>
        <el-tabs v-model="activeTab" class="market-tabs" @tab-change="handleTabChange">
          <el-tab-pane :label="`💱 ${t('market.tabs.rates')}`" name="rates" />
          <el-tab-pane :label="`📈 ${t('market.tabs.indices')}`" name="indices" />
        </el-tabs>
      </div>

      <!-- Header Global Controls -->
      <div class="header-controls">
        <!-- Base Currency Dropdown -->
        <div class="control-item">
          <span class="control-label">{{ t('market.baseCurrency') }}:</span>
          <el-select
            :model-value="marketStore.baseCurrency"
            size="default"
            class="base-currency-select"
            @update:model-value="marketStore.setBaseCurrency($event)"
          >
            <el-option label="🇨🇳 CNY (人民币)" value="CNY" />
            <el-option label="🇺🇸 USD (美元)" value="USD" />
            <el-option label="🇪🇺 EUR (欧元)" value="EUR" />
            <el-option label="🇭🇰 HKD (港币)" value="HKD" />
            <el-option label="🇯🇵 JPY (日元)" value="JPY" />
            <el-option label="🇬🇧 GBP (英镑)" value="GBP" />
          </el-select>
        </div>

        <span v-if="marketStore.lastSyncAt" class="sync-time">
          {{ t('market.lastSyncAt', { time: marketStore.lastSyncAt }) }}
        </span>

        <el-button
          type="primary"
          :loading="marketStore.isSyncing"
          @click="handleSyncAll"
        >
          🔄 {{ t('market.syncNow') }}
        </el-button>

        <el-button @click="editDialogRef?.open()">
          ⚙ {{ t('market.editRate.btnText') }}
        </el-button>
      </div>
    </div>

    <!-- Tab 1: Real-time FX Rates -->
    <div v-if="activeTab === 'rates'" class="tab-content">
      <!-- 1. Major Currency Cards -->
      <FxRateCards />

      <!-- 2. Split Middle Row (Converter & Trend Chart) -->
      <div class="middle-split-row">
        <div class="split-col-converter">
          <FxConverter />
        </div>
        <div class="split-col-chart">
          <FxTrendChart />
        </div>
      </div>

      <!-- 3. Holdings Summary -->
      <FxHoldingsSummary />
    </div>

    <!-- Tab 2: Global Indices Board -->
    <div v-else-if="activeTab === 'indices'" class="tab-content">
      <GlobalIndicesBoard />
    </div>

    <!-- Manual Edit Dialog -->
    <FxRateEditDialog ref="editDialogRef" />
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { ElMessage } from 'element-plus'
import { useMarketStore } from '@/stores/market'
import FxRateCards from '@/components/market/FxRateCards.vue'
import FxConverter from '@/components/market/FxConverter.vue'
import FxTrendChart from '@/components/market/FxTrendChart.vue'
import FxHoldingsSummary from '@/components/market/FxHoldingsSummary.vue'
import FxRateEditDialog from '@/components/market/FxRateEditDialog.vue'
import GlobalIndicesBoard from '@/components/market/GlobalIndicesBoard.vue'

const { t } = useI18n()
const route = useRoute()
const router = useRouter()
const marketStore = useMarketStore()

const activeTab = ref<string>((route.query.tab as string) || 'rates')
const editDialogRef = ref<InstanceType<typeof FxRateEditDialog> | null>(null)

function handleTabChange(tabName: any) {
  router.replace({ query: { ...route.query, tab: tabName } })
}

async function handleSyncAll() {
  const success = await marketStore.syncAllQuotes()
  if (success) {
    ElMessage.success(t('market.syncSuccess'))
  } else {
    ElMessage.warning(t('market.syncFailed'))
  }
}

onMounted(() => {
  marketStore.init()
})
</script>

<style scoped>
.market-view-container {
  padding: 24px;
  height: 100%;
  overflow-y: auto;
  box-sizing: border-box;
}

.market-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  border-bottom: 1px solid var(--mf-border-subtle);
  padding-bottom: 8px;
  margin-bottom: 20px;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 24px;
}

.page-title {
  font-size: 20px;
  font-weight: 700;
  color: var(--el-text-color-primary);
  margin: 0;
}

.market-tabs :deep(.el-tabs__header) {
  margin: 0;
  border-bottom: none;
}

.market-tabs :deep(.el-tabs__nav-wrap::after) {
  display: none;
}

.header-controls {
  display: flex;
  align-items: center;
  gap: 12px;
}

.control-item {
  display: flex;
  align-items: center;
  gap: 6px;
}

.control-label {
  font-size: 13px;
  color: var(--el-text-color-secondary);
}

.base-currency-select {
  width: 150px;
}

.base-currency-select :deep(.el-select__wrapper) {
  background: var(--mf-surface-muted);
  box-shadow: 0 0 0 1px var(--mf-border-subtle) inset;
  border-radius: var(--mf-radius-md);
}

.sync-time {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}

.middle-split-row {
  display: grid;
  grid-template-columns: 1fr 1.5fr;
  gap: 16px;
  margin-bottom: 20px;
}

@media (max-width: 1024px) {
  .middle-split-row {
    grid-template-columns: 1fr;
  }
}
</style>
