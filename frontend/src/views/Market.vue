<template>
  <div class="market-view-container">
    <div v-if="mobileMode" class="mobile-market-nav">
      <div class="mobile-segmented">
        <button class="segmented-item" @click="$router.push('/m/assets')">{{ t('nav.assets') }}</button>
        <button class="segmented-item" @click="$router.push('/m/holdings')">{{ t('nav.holdings') }}</button>
        <button class="segmented-item active">{{ t('nav.market') }}</button>
      </div>
    </div>
    <el-alert
      v-if="mobileMode && !isOnline"
      title="当前处于离线状态，市场行情与汇率需要网络连接"
      type="warning"
      :closable="false"
      show-icon
      style="margin-bottom: 12px"
    />
    <!-- Top Hub Header -->
    <PageHeader :title="t('nav.market')" class="market-header">
      <template #extra>
        <el-tabs v-model="activeTab" class="market-tabs" @tab-change="handleTabChange">
          <el-tab-pane :label="t('market.tabs.rates')" name="rates" />
          <el-tab-pane :label="t('market.tabs.indices')" name="indices" />
        </el-tabs>
      </template>

      <template #actions>
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
          {{ t('market.syncNow') }}
        </el-button>

        <el-button @click="editDialogRef?.open()">
          {{ t('market.editRate.btnText') }}
        </el-button>
      </template>
    </PageHeader>

    <!-- Tab 1: Real-time FX Rates -->
    <div v-if="activeTab === 'rates'" class="tab-content">
      <FxSkeleton v-if="marketStore.isLoadingRates && Object.keys(marketStore.rates).length <= 1" />
      <template v-else>
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
      </template>
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
import { ref, onMounted, onBeforeUnmount } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { ElMessage } from 'element-plus'
import { useMarketStore } from '@/stores/market'
import { isMobileMode } from '@/utils/http'
import PageHeader from '@/components/PageHeader.vue'
import FxSkeleton from '@/components/market/FxSkeleton.vue'
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

const mobileMode = isMobileMode()
const isOnline = ref(typeof navigator !== 'undefined' ? navigator.onLine : true)
const activeTab = ref<string>((route.query.tab as string) || 'rates')
const editDialogRef = ref<InstanceType<typeof FxRateEditDialog> | null>(null)

function handleOnline() { isOnline.value = true }
function handleOffline() { isOnline.value = false }

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
  window.addEventListener('online', handleOnline)
  window.addEventListener('offline', handleOffline)
  marketStore.init()
})

onBeforeUnmount(() => {
  window.removeEventListener('online', handleOnline)
  window.removeEventListener('offline', handleOffline)
})
</script>

<style scoped>
.market-view-container {
  width: 100%;
  box-sizing: border-box;
}

.market-header {
  border-bottom: 1px solid var(--mf-border-subtle);
  padding-bottom: 8px;
  margin-bottom: 20px;
}

.market-tabs :deep(.el-tabs__header) {
  margin: 0;
  border-bottom: none;
}

.market-tabs :deep(.el-tabs__nav-wrap::after) {
  display: none;
}

.control-item {
  display: flex;
  align-items: center;
  gap: 6px;
}

.control-label {
  font-size: 13px;
  color: var(--el-text-color-secondary);
  white-space: nowrap;
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
  white-space: nowrap;
}

.middle-split-row {
  display: grid;
  grid-template-columns: 1fr 1.5fr;
  gap: 16px;
  margin-bottom: 20px;
}

.mobile-market-nav {
  margin-bottom: 12px;
}

@media (max-width: 1024px) {
  .middle-split-row {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 768px) {
  .market-header :deep(.header-actions) {
    flex-wrap: wrap;
    gap: 8px;
  }
  .base-currency-select {
    width: 132px;
  }
  .sync-time {
    width: 100%;
  }
}
</style>
