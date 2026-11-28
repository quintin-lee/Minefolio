<template>
  <div class="holdings-mobile" v-loading="loading">
    <div class="page-header">
      <div class="mobile-segmented">
        <button class="segmented-item" @click="$router.push('/m/assets')">{{ t('nav.assets') }}</button>
        <button class="segmented-item active">{{ t('nav.holdings') }}</button>
        <button class="segmented-item" @click="$router.push('/m/market')">{{ t('nav.market') }}</button>
      </div>
      <el-button :icon="Refresh" circle @click="load" aria-label="刷新持仓" />
    </div>
    <div v-if="approximate || ratesCachedAt" class="cache-row">
      <span v-if="approximate" class="cache-pill">离线近似 · 已实现盈亏未更新</span>
      <span v-if="ratesCachedAt" class="cache-pill">汇率缓存于 {{ ratesCachedAt }}</span>
    </div>

    <!-- 汇总卡片 -->
    <div class="summary-row">
      <div class="kpi-card">
        <span class="label">总市值</span>
        <b class="value mf-amount-mask">{{ formatCurrency(summary.total_market_value) }}</b>
      </div>
      <div class="kpi-card" :class="pnlCardClass(summary.total_floating_pnl)">
        <span class="label">浮动盈亏</span>
        <b class="value mf-amount-mask">{{ formatSigned(summary.total_floating_pnl) }}</b>
      </div>
      <div class="kpi-card">
        <span class="label">已实现盈亏</span>
        <b class="value mf-amount-mask">{{ formatSigned(summary.total_realized_pnl) }}</b>
      </div>
    </div>

    <!-- 集中度与图表分析 -->
    <div v-if="holdings.length > 0" class="analytics-card">
      <div class="analytics-head">
        <ConcentrationBadge :hhi="concentration.hhi" :max-holding-weight="concentration.maxWeight" />
        <div class="chart-toggle">
          <button
            type="button"
            class="toggle-pill"
            :class="{ active: chartTab === 'pie' }"
            @click="chartTab = 'pie'"
          >
            资产配置
          </button>
          <button
            type="button"
            class="toggle-pill"
            :class="{ active: chartTab === 'bar' }"
            @click="chartTab = 'bar'"
          >
            成本对比
          </button>
        </div>
      </div>
      <div class="chart-wrap">
        <HoldingsTypePie v-if="chartTab === 'pie'" :data="typeShare" />
        <HoldingsCostBar v-else :data="barData" />
      </div>
    </div>

    <!-- 持仓列表 -->
    <el-alert
      v-if="loadError"
      title="无法获取最新持仓数据（当前离线或服务端不可达）"
      type="warning"
      :closable="false"
      show-icon
      style="margin-bottom: 12px"
    />
    <el-empty v-if="holdings.length === 0 && !loading && !loadError" description="暂无持仓数据" :image-size="72" />
    <div
      v-for="h in holdings"
      :key="h.asset_id"
      class="holding-card"
      @click="$router.push(`/m/assets/${h.asset_id}`)"
    >
      <div class="card-top">
        <div class="asset-title-group">
          <span class="asset-name">{{ h.name }}</span>
          <span v-if="h.currency && h.currency !== 'CNY'" class="currency-tag">{{ h.currency }}</span>
        </div>
        <span :class="['type-pill', h.asset_type === 'crypto' ? 'pill-crypto' : 'pill-default']">
          {{ typeLabel(h.asset_type) }}
        </span>
      </div>
      <div class="card-row">
        <span class="meta">份额 <span class="mf-amount-mask">{{ h.quantity.toFixed(2) }}</span></span>
        <span class="meta">成本 <span class="mf-amount-mask">{{ formatCurrency(h.cost_basis, h.currency) }}</span></span>
      </div>

      <div class="card-row">
        <div class="value-group">
          <span class="value mf-amount-mask">{{ formatCurrency(h.current_value, h.currency) }}</span>
          <span v-if="h.currency && h.currency !== 'CNY' && exchangeRates[h.currency]" class="cny-hint mf-amount-mask">
            ≈ ¥{{ (Number(h.current_value) * (exchangeRates[h.currency] || 1)).toLocaleString('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 }) }}
          </span>
        </div>
        <span :class="['pnl', pnlClass(h.floating_pnl)]"><span class="mf-amount-mask">{{ formatSigned(h.floating_pnl, h.currency) }}</span>  {{ (h.floating_pct >= 0 ? '↑' : '↓') }}{{ Math.abs(h.floating_pct).toFixed(2) }}%</span>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { Refresh } from '@element-plus/icons-vue'
import { reportsApi, type HoldingsReport } from '@/api/reports'
import { marketApi } from '@/api/market'
import { formatCurrency, formatSigned } from '@/utils/format'
import { notifyMobileReadError } from '@/utils/http'
import { deriveLocalHoldings } from '@/composables/mobile/useLocalHoldings'
import { loadViewCache, saveViewCache } from '@/composables/mobile/useViewCache'
import { t } from '@/utils/locale'
import ConcentrationBadge from '@/components/ConcentrationBadge.vue'
import HoldingsTypePie from '@/components/HoldingsTypePie.vue'
import HoldingsCostBar from '@/components/HoldingsCostBar.vue'

const report = ref<HoldingsReport | null>(null)
const loading = ref(false)
const loadError = ref(false)
const approximate = ref(false)
const ratesCachedAt = ref<string | null>(null)
const exchangeRates = ref<Record<string, number>>({})
const chartTab = ref<'pie' | 'bar'>('pie')
const summary = ref({ total_market_value: 0, total_cost_basis: 0, total_floating_pnl: 0, total_realized_pnl: 0, floating_pct: 0 })
const holdings = ref<{ asset_id: number; name: string; asset_type: string; currency: string; quantity: number; net_value: number; cost_basis: number; current_value: number; floating_pnl: number; floating_pct: number; realized_pnl: number }[]>([])

function toCnyValue(val: number, cur?: string): number {
  if (!cur || cur === 'CNY') return Number(val) || 0
  return (Number(val) || 0) * (exchangeRates.value[cur] || 1)
}

const typeShare = computed(() => {
  const map = new Map<string, number>()
  for (const h of holdings.value) {
    const cnyVal = toCnyValue(h.current_value, h.currency)
    map.set(h.asset_type, (map.get(h.asset_type) ?? 0) + cnyVal)
  }
  return [...map.entries()].map(([name, value]) => ({ name, value }))
})

const barData = computed(() =>
  holdings.value.map((h) => ({
    name: h.name,
    cost_basis: toCnyValue(h.cost_basis, h.currency),
    current_value: toCnyValue(h.current_value, h.currency),
  })),
)

const concentration = computed(() => {
  const vals = holdings.value.map((h) => Math.max(0, toCnyValue(h.current_value, h.currency)))
  const total = vals.reduce((s, v) => s + v, 0)
  if (total <= 0) return { hhi: 0, maxWeight: 0 }
  let hhi = 0
  let maxWeight = 0
  for (const v of vals) {
    const w = v / total
    hhi += w * w
    if (w > maxWeight) maxWeight = w
  }
  return { hhi, maxWeight }
})

function pnlClass(v: number) {
  return v > 0 ? 'gain' : v < 0 ? 'loss' : ''
}
function pnlCardClass(v: number) {
  if (v > 0) return 'profit'
  if (v < 0) return 'loss'
  return ''
}
function typeLabel(tp: string) {
  const map: Record<string, string> = { stock: '股票', fund: '基金', bond: '债券', crypto: '加密货币' }
  return map[tp] ?? tp
}

async function load() {
  loading.value = true
  loadError.value = false

  // 本地优先：先用投资类资产派生持仓，避免首屏空白
  let hadLocalHoldings = false
  try {
    const local = deriveLocalHoldings()
    hadLocalHoldings = local.report.holdings.length > 0
    report.value = local.report
    summary.value = local.report.summary
    holdings.value = local.report.holdings
    approximate.value = true
  } catch (e) {
    console.warn('[HoldingsMobile] deriveLocalHoldings failed, falling back to remote:', e)
  }

  const ratesCache = loadViewCache<Record<string, number>>('exchange_rates')
  if (ratesCache.data) {
    exchangeRates.value = ratesCache.data
    ratesCachedAt.value = ratesCache.updatedAt?.slice(0, 16).replace('T', ' ') ?? null
  }

  try {
    const [hRes, rRes] = await Promise.allSettled([
      reportsApi.holdings(),
      marketApi.getExchangeRates(),
    ])
    if (hRes.status === 'fulfilled' && hRes.value) {
      report.value = hRes.value
      summary.value = hRes.value.summary
      holdings.value = hRes.value.holdings
      approximate.value = false
    } else if (hRes.status === 'rejected') {
      if (!hadLocalHoldings) {
        loadError.value = true
        notifyMobileReadError(hRes.reason, '持仓数据')
      }
      approximate.value = hadLocalHoldings
      console.error('[HoldingsMobile] remote holdings failed, using local derived:', hRes.reason)
    }
    if (rRes.status === 'fulfilled' && rRes.value) {
      exchangeRates.value = rRes.value
      saveViewCache('exchange_rates', rRes.value)
      ratesCachedAt.value = null
    }
  } catch (e) {
    if (!hadLocalHoldings) {
      loadError.value = true
      notifyMobileReadError(e, '持仓数据')
    }
    console.error('[HoldingsMobile] onMounted failed:', e)
  } finally {
    loading.value = false
  }
}

onMounted(load)
</script>

<style scoped>
.page-header { display: flex; align-items: center; justify-content: space-between; gap: 8px; margin-bottom: 12px; }
.cache-row { display: flex; flex-wrap: wrap; gap: 6px; margin: 8px 0; }
.cache-pill { font-size: 11px; color: var(--mf-warning); border: 1px solid var(--mf-warning); border-radius: 999px; padding: 2px 8px; white-space: nowrap; }
.summary-row {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 8px;
  margin-bottom: 14px;
}
.kpi-card {
  min-width: 0;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 12px;
  padding: 10px 8px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.kpi-card.profit { border-color: var(--mf-gain-border); background: var(--mf-gain-light); }
.kpi-card.loss { border-color: var(--mf-loss-border); background: var(--mf-loss-light); }
.kpi-card .label { color: var(--mf-text-muted); font-size: 11px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.kpi-card .value { font-family: 'JetBrains Mono', monospace; font-size: clamp(12px, 3.4vw, 14px); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.kpi-card.profit .value { color: var(--mf-gain); }
.kpi-card.loss .value { color: var(--mf-loss); }
.analytics-card {
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 12px;
  padding: 12px;
  margin-bottom: 14px;
}
.analytics-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 8px;
  margin-bottom: 10px;
}
.chart-toggle {
  display: inline-flex;
  gap: 4px;
  background: var(--mf-surface-muted);
  padding: 2px;
  border-radius: 8px;
}
.toggle-pill {
  border: none;
  background: transparent;
  color: var(--mf-text-muted);
  font-size: 11px;
  padding: 4px 10px;
  border-radius: 6px;
  cursor: pointer;
}
.toggle-pill.active {
  background: var(--mf-surface);
  color: var(--mf-primary);
  font-weight: 600;
}
.chart-wrap {
  height: 240px;
}
.holding-card {
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 12px;
  padding: 14px;
  margin-bottom: 10px;
  cursor: pointer;
}
.card-top {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}
.asset-title-group {
  display: flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
  flex: 1;
}
.asset-name {
  font-weight: 600;
  font-size: 15px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.currency-tag {
  flex-shrink: 0;
  font-size: 10px;
  padding: 1px 6px;
  border-radius: 4px;
  background: var(--mf-info-light);
  color: var(--mf-info);
  border: 1px solid var(--mf-info-border);
}
.type-pill {
  flex-shrink: 0;
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 9999px;
  background: var(--mf-info-light);
  color: var(--mf-info);
  border: 1px solid var(--mf-info-border);
  font-weight: 500;
}
.type-pill.pill-crypto {
  background: rgba(124, 58, 237, 0.08);
  color: var(--mf-accent);
  border-color: rgba(124, 58, 237, 0.15);
}
.card-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
  font-size: 13px;
}
.card-row .meta { color: var(--mf-text-muted); }
.value-group {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 2px;
  min-width: 0;
}
.value-group .value { font-family: 'JetBrains Mono', monospace; font-weight: 500; }
.value-group .cny-hint {
  font-size: 11px;
  color: var(--mf-text-muted);
  font-family: 'JetBrains Mono', monospace;
}
.pnl { white-space: nowrap; font-size: 12px; font-family: 'JetBrains Mono', monospace; }
.pnl.gain { color: var(--mf-gain); }
.pnl.loss { color: var(--mf-loss); }
.empty-state {
  text-align: center;
  color: var(--mf-text-muted);
  padding: 40px 0;
  font-size: 14px;
}
</style>
