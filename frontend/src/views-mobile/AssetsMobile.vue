<template>
  <div class="assets-mobile" v-loading="loading">
    <div class="page-header"><h2>资产</h2></div>
    <div v-if="list.length === 0 && !loading" class="empty-state">暂无资产数据</div>
    <div v-for="a in list" :key="a.id" class="asset-card">
      <div class="asset-info">
        <span class="name">{{ a.name }}</span>
        <span v-if="a.currency && a.currency !== 'CNY'" class="currency-tag">{{ a.currency }}</span>
      </div>
      <div class="value-col">
        <span class="value">{{ fmt(a.current_value, a.currency) }}</span>
        <span v-if="a.currency && a.currency !== 'CNY' && exchangeRates[a.currency]" class="cny-hint">
          ≈ ¥{{ (Number(a.current_value) * (exchangeRates[a.currency] || 1)).toLocaleString('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 }) }}
        </span>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { assetsApi } from '@/api/assets'
import { marketApi } from '@/api/market'
import type { Asset } from '@/types'

const list = ref<Asset[]>([])
const loading = ref(false)
const exchangeRates = ref<Record<string, number>>({})

function fmt(v: number, cur?: string) {
  const currencyCode = cur && cur !== 'CNY' ? cur : 'CNY'
  try {
    return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: currencyCode }).format(v ?? 0)
  } catch {
    return `${currencyCode} ${(v ?? 0).toLocaleString('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 })}`
  }
}

onMounted(async () => {
  loading.value = true
  try {
    const [assetsRes, ratesRes] = await Promise.allSettled([
      assetsApi.list({ page_size: 500 }),
      marketApi.getExchangeRates()
    ])
    if (assetsRes.status === 'fulfilled') {
      list.value = assetsRes.value.list
    }
    if (ratesRes.status === 'fulfilled' && ratesRes.value) {
      exchangeRates.value = ratesRes.value
    }
  } catch (e) {
    console.error('[AssetsMobile] load error:', e)
  } finally {
    loading.value = false
  }
})
</script>

<style scoped>
.assets-mobile {
  padding-bottom: 20px;
}
.asset-card {
  display: flex;
  justify-content: space-between;
  align-items: center;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 12px;
  padding: 14px;
  margin-bottom: 10px;
}
.asset-info {
  display: flex;
  align-items: center;
  gap: 6px;
}
.name {
  font-weight: 500;
  font-size: 15px;
}
.currency-tag {
  font-size: 10px;
  padding: 1px 6px;
  border-radius: 4px;
  background: rgba(0, 212, 255, 0.1);
  color: var(--mf-info);
  border: 1px solid rgba(0, 212, 255, 0.2);
}
.value-col {
  display: flex;
  flex-direction: column;
  align-items: flex-end;
  gap: 2px;
}
.value {
  font-family: 'JetBrains Mono', monospace;
  font-size: 15px;
}
.cny-hint {
  font-size: 11px;
  color: var(--mf-text-muted);
  font-family: 'JetBrains Mono', monospace;
}
.empty-state {
  text-align: center;
  color: var(--mf-text-muted);
  padding: 40px 0;
  font-size: 14px;
}
</style>
