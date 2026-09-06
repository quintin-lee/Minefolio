<template>
  <div class="panel-container">
    <div class="panel-header">
      <h3>{{ t('settings.marketSyncTitle') }}</h3>
    </div>
    <p class="export-hint">{{ t('settings.marketSyncHint') }}</p>
    <el-form label-width="120px" class="premium-form" style="margin-top: 16px;">
      <el-form-item :label="t('settings.httpProxyLabel')">
        <el-input v-model="marketForm.market_proxy" :placeholder="t('settings.httpProxyPlaceholder')" />
      </el-form-item>
      <el-form-item :label="t('settings.autoSyncMode')">
        <el-radio-group v-model="marketForm.market_sync_mode">
          <el-radio value="trading_hours">{{ t('settings.syncTradingHours') }}</el-radio>
          <el-radio value="interval">{{ t('settings.syncFixedInterval') }}</el-radio>
          <el-radio value="manual">{{ t('settings.syncManualOnly') }}</el-radio>
        </el-radio-group>
      </el-form-item>
      <el-form-item v-if="marketForm.market_sync_mode !== 'manual'" :label="t('settings.syncCycle')">
        <el-select v-model="marketForm.market_sync_interval_min" style="width: 200px;">
          <el-option :value="15" :label="t('settings.interval15')" />
          <el-option :value="30" :label="t('settings.interval30')" />
          <el-option :value="60" :label="t('settings.interval60')" />
          <el-option :value="120" :label="t('settings.interval120')" />
          <el-option :value="1440" :label="t('settings.interval1440')" />
        </el-select>
      </el-form-item>
      <el-form-item>
        <div style="display: flex; gap: 12px; align-items: center;">
          <el-button type="primary" class="action-btn" :loading="savingMarket" @click="saveMarketSettings">
            {{ t('settings.saveMarketConfig') }}
          </el-button>
          <el-button :loading="testingMarketProxy" @click="testMarketProxy">
            {{ t('settings.testMarketConnectivity') }}
          </el-button>
          <el-tag v-if="marketTestResult" :type="marketTestResult.success ? 'success' : 'danger'">
            {{ marketTestResult.message }} ({{ marketTestResult.latency_ms }}ms)
          </el-tag>
        </div>
      </el-form-item>
    </el-form>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import { marketApi } from '@/api/market'
import { t } from '@/utils/locale'
import type { MarketSettings, TestProxyResult } from '@/types'

const marketForm = reactive<MarketSettings>({
  market_proxy: '',
  market_auto_sync: true,
  market_sync_interval_min: 30,
  market_sync_mode: 'trading_hours'
})
const savingMarket = ref(false)
const testingMarketProxy = ref(false)
const marketTestResult = ref<TestProxyResult | null>(null)

async function loadMarketSettings() {
  try {
    const res = await marketApi.getSettings()
    if (res) {
      marketForm.market_proxy = res.market_proxy || ''
      marketForm.market_auto_sync = res.market_auto_sync ?? true
      marketForm.market_sync_interval_min = res.market_sync_interval_min || 30
      marketForm.market_sync_mode = res.market_sync_mode || 'trading_hours'
    }
  } catch (err) {
    console.error('[MarketSyncSettings] loadMarketSettings failed:', err)
  }
}

async function saveMarketSettings() {
  savingMarket.value = true
  try {
    await marketApi.updateSettings({
      market_proxy: marketForm.market_proxy,
      market_auto_sync: marketForm.market_sync_mode !== 'manual',
      market_sync_interval_min: marketForm.market_sync_interval_min,
      market_sync_mode: marketForm.market_sync_mode
    })
    ElMessage.success(t('settings.marketSaved'))
  } catch (err: any) {
    ElMessage.error(err?.message || t('settings.saveFailed'))
  } finally {
    savingMarket.value = false
  }
}

async function testMarketProxy() {
  testingMarketProxy.value = true
  marketTestResult.value = null
  try {
    const res = await marketApi.testProxy({ market_proxy: marketForm.market_proxy })
    marketTestResult.value = res
  } catch (err: any) {
    marketTestResult.value = {
      success: false,
      message: err?.message || t('settings.marketTestFailed'),
      latency_ms: 0
    }
  } finally {
    testingMarketProxy.value = false
  }
}

onMounted(() => {
  loadMarketSettings()
})
</script>
