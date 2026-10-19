<template>
  <div class="clocks-ribbon">
    <div class="clocks-list">
      <div class="ribbon-label">
        <el-icon><Clock /></el-icon>
        <span>{{ t('market.indicesData.clocksTitle') }}:</span>
      </div>
      <div
        v-for="clock in marketStore.marketClocks"
        :key="clock.market"
        class="clock-pill"
        :class="`status-${clock.status}`"
      >
        <span class="status-dot" />
        <span class="clock-name">{{ clock.name }}</span>
        <span class="clock-status-tag">{{ getStatusLabel(clock.status) }}</span>
      </div>
    </div>

    <div class="ribbon-controls">
      <el-tag
        v-if="!marketStore.isAnyMarketActive"
        type="info"
        size="small"
        effect="light"
        class="sleep-tag"
      >
        <span class="sleep-dot" />
        {{ t('market.indicesData.allClosedSleeping') }}
      </el-tag>

      <span class="refresh-label">{{ t('market.indicesData.autoRefresh') }}:</span>
      <el-select
        :model-value="marketStore.autoRefreshInterval"
        size="small"
        class="interval-select"
        @update:model-value="marketStore.setAutoRefreshInterval($event)"
      >
        <el-option :label="t('market.indicesData.refresh1m')" :value="60" />
        <el-option :label="t('market.indicesData.refresh3m')" :value="180" />
        <el-option :label="t('market.indicesData.refresh5m')" :value="300" />
        <el-option :label="t('market.indicesData.refreshOff')" :value="0" />
      </el-select>

      <el-button
        size="small"
        type="primary"
        plain
        :loading="marketStore.isLoadingIndices"
        @click="marketStore.fetchIndices()"
      >
        <el-icon><Refresh /></el-icon>
        {{ t('market.indicesData.refreshNow') }}
      </el-button>
    </div>
  </div>
</template>

<script setup lang="ts">
import { useI18n } from '@/composables/useI18n'
import { Clock, Refresh } from '@element-plus/icons-vue'
import { useMarketStore } from '@/stores/market'

const { t } = useI18n()
const marketStore = useMarketStore()

function getStatusLabel(status: string) {
  if (status === 'open') return t('market.indicesData.statusOpen')
  if (status === 'pre') return t('market.indicesData.statusPre')
  return t('market.indicesData.statusClosed')
}
</script>

<style scoped>
.clocks-ribbon {
  display: flex;
  align-items: center;
  justify-content: space-between;
  background: var(--mf-surface-card);
  border: 1px solid var(--mf-border-subtle);
  border-radius: var(--mf-radius-md);
  padding: 8px 14px;
  margin-bottom: 12px;
  gap: 12px;
  flex-wrap: wrap;
}
.clocks-list {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}
.ribbon-label {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 13px;
  font-weight: 600;
  color: var(--el-text-color-regular);
}
.clock-pill {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 3px 10px;
  border-radius: 12px;
  font-size: 12px;
  background: var(--el-fill-color-light);
  border: 1px solid transparent;
}
.status-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--el-color-info);
}
.status-open .status-dot {
  background: var(--el-color-success);
  box-shadow: 0 0 6px var(--el-color-success);
  animation: pulse-dot 1.8s infinite;
}
.status-pre .status-dot {
  background: var(--el-color-warning);
}
.clock-name {
  font-weight: 500;
  color: var(--el-text-color-primary);
}
.clock-status-tag {
  font-size: 11px;
  color: var(--el-text-color-secondary);
}
.ribbon-controls {
  display: flex;
  align-items: center;
  gap: 8px;
}
.refresh-label {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}
.interval-select {
  width: 95px;
}
.interval-select :deep(.el-select__wrapper) {
  background: var(--mf-surface-muted);
  box-shadow: 0 0 0 1px var(--mf-border-subtle) inset;
}
.sleep-tag {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-size: 11px;
  border: none !important;
  background: var(--mf-surface-muted) !important;
}
.sleep-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--el-color-info);
}
@keyframes pulse-dot {
  0% { transform: scale(0.95); opacity: 0.8; }
  50% { transform: scale(1.3); opacity: 1; }
  100% { transform: scale(0.95); opacity: 0.8; }
}
</style>
