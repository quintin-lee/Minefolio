<template>
  <div class="panel-container">
    <div class="panel-header">
      <h3>行情同步与网络代理</h3>
    </div>
    <p class="export-hint">配置外部行情源的网络代理（如访问海外美股、加密货币源时可选配置），以及查看行情调度状态。</p>
    <el-form label-width="120px" class="premium-form" style="margin-top: 16px;">
      <el-form-item label="HTTP 代理">
        <el-input v-model="marketForm.market_proxy" placeholder="如: http://127.0.0.1:7890 或 socks5://127.0.0.1:1080 (留空为直连)" />
      </el-form-item>
      <el-form-item label="自动同步模式">
        <el-radio-group v-model="marketForm.market_sync_mode">
          <el-radio value="trading_hours">智能开盘时段 (开盘期刷新 + 夜间基金清算，推荐)</el-radio>
          <el-radio value="interval">全天固定间隔</el-radio>
          <el-radio value="manual">仅手动同步</el-radio>
        </el-radio-group>
      </el-form-item>
      <el-form-item v-if="marketForm.market_sync_mode !== 'manual'" label="同步周期">
        <el-select v-model="marketForm.market_sync_interval_min" style="width: 200px;">
          <el-option :value="15" label="每 15 分钟" />
          <el-option :value="30" label="每 30 分钟" />
          <el-option :value="60" label="每 1 小时" />
          <el-option :value="120" label="每 2 小时" />
          <el-option :value="1440" label="每天一次" />
        </el-select>
      </el-form-item>
      <el-form-item>
        <div style="display: flex; gap: 12px; align-items: center;">
          <el-button type="primary" class="action-btn" :loading="savingMarket" @click="saveMarketSettings">
            保存行情配置
          </el-button>
          <el-button :loading="testingMarketProxy" @click="testMarketProxy">
            测试行情连通性
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
    ElMessage.success('行情设置保存成功')
  } catch (err: any) {
    ElMessage.error(err?.message || '保存失败')
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
      message: err?.message || '测试失败',
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
