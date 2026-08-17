<template>
  <div class="holdings-mobile">
    <div class="page-header"><h2>持仓</h2></div>

    <!-- 汇总卡片 -->
    <div class="summary-row">
      <div class="kpi-card">
        <span class="label">总市值</span>
        <b class="value">{{ fmt(summary.total_market_value) }}</b>
      </div>
      <div class="kpi-card" :class="pnlCardClass(summary.total_floating_pnl)">
        <span class="label">浮动盈亏</span>
        <b class="value">{{ fmtSigned(summary.total_floating_pnl) }}</b>
      </div>
      <div class="kpi-card">
        <span class="label">已实现盈亏</span>
        <b class="value">{{ fmtSigned(summary.total_realized_pnl) }}</b>
      </div>
    </div>

    <!-- 持仓列表 -->
    <div v-if="holdings.length === 0" class="empty-state">暂无持仓数据</div>
    <div v-for="h in holdings" :key="h.asset_id" class="holding-card">
      <div class="card-top">
        <span class="asset-name">{{ h.name }}</span>
        <span :class="['type-pill', h.asset_type === 'crypto' ? 'pill-crypto' : 'pill-default']">
          {{ typeLabel(h.asset_type) }}
        </span>
      </div>
      <div class="card-row">
        <span class="meta">份额 {{ h.quantity.toFixed(2) }}</span>
        <span class="meta">成本 {{ fmt(h.cost_basis) }}</span>
      </div>
      <div class="card-row">
        <span class="value">{{ fmt(h.current_value) }}</span>
        <span :class="['pnl', pnlClass(h.floating_pnl)]">{{ fmtSigned(h.floating_pnl) }}  {{ (h.floating_pct >= 0 ? '↑' : '↓') }}{{ Math.abs(h.floating_pct).toFixed(2) }}%</span>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { reportsApi, type HoldingsReport } from '@/api/reports'

const report = ref<HoldingsReport | null>(null)
const summary = ref({ total_market_value: 0, total_cost_basis: 0, total_floating_pnl: 0, total_realized_pnl: 0, floating_pct: 0 })
const holdings = ref<{ asset_id: number; name: string; asset_type: string; currency: string; quantity: number; net_value: number; cost_basis: number; current_value: number; floating_pnl: number; floating_pct: number; realized_pnl: number }[]>([])

function fmt(v: number) {
  return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(v ?? 0)
}
function fmtSigned(v: number) {
  const s = fmt(Math.abs(v))
  return v > 0 ? `+${s}` : v < 0 ? `-${s}` : s
}
function pnlClass(v: number) {
  return v > 0 ? 'income' : v < 0 ? 'expense' : ''
}
function pnlCardClass(v: number) {
  if (v > 0) return 'profit'
  if (v < 0) return 'loss'
  return ''
}
function typeLabel(t: string) {
  const map: Record<string, string> = { stock: '股票', fund: '基金', bond: '债券', crypto: '加密货币' }
  return map[t] ?? t
}

onMounted(async () => {
  try {
    const r = await reportsApi.holdings()
    report.value = r
    summary.value = r.summary
    holdings.value = r.holdings
  } catch (e) {
    console.error('[HoldingsMobile] onMounted failed:', e)
  }
})
</script>

<style scoped>
.summary-row {
  display: flex;
  gap: 10px;
  margin-bottom: 14px;
}
.kpi-card {
  flex: 1;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 12px;
  padding: 12px 10px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.kpi-card.profit { border-color: rgba(52, 211, 153, 0.35); background: rgba(52, 211, 153, 0.06); }
.kpi-card.loss { border-color: rgba(239, 68, 68, 0.35); background: rgba(239, 68, 68, 0.06); }
.kpi-card .label { color: var(--mf-text-muted); font-size: 11px; }
.kpi-card .value { font-family: 'JetBrains Mono', monospace; font-size: 14px; }
.kpi-card.profit .value { color: #34d399; }
.kpi-card.loss .value { color: #f87171; }

.holding-card {
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 12px;
  padding: 14px;
  margin-bottom: 10px;
}
.card-top {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
}
.asset-name { font-weight: 600; font-size: 15px; }
.type-pill {
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 9999px;
  background: rgba(0, 212, 255, 0.08);
  color: #00d4ff;
  border: 1px solid rgba(0, 212, 255, 0.15);
  font-weight: 500;
}
.type-pill.pill-crypto {
  background: rgba(124, 58, 237, 0.08);
  color: #a78bfa;
  border-color: rgba(124, 58, 237, 0.15);
}
.card-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 13px;
}
.card-row .meta { color: var(--mf-text-muted); }
.card-row .value { font-family: 'JetBrains Mono', monospace; font-weight: 500; }
.pnl.income { color: #34d399; }
.pnl.expense { color: #f87171; }
.empty-state {
  text-align: center;
  color: var(--mf-text-muted);
  padding: 40px 0;
  font-size: 14px;
}
</style>
