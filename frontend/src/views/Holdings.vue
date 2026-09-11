<template>
  <div class="holdings-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>持仓</h2>
      </div>
      <div class="header-actions">
        <el-button :icon="Refresh" circle @click="load" />
      </div>
    </div>

    <!-- 汇总卡片 -->
    <el-row :gutter="24">
      <el-col :span="8">
        <SummaryCard label="总市值" :value="formatCurrency(report?.summary.total_market_value ?? 0)" type="highlight" />
      </el-col>
      <el-col :span="8">
        <SummaryCard
          label="总浮动盈亏"
          :value="formatSigned(report?.summary.total_floating_pnl ?? 0)"
          :type="(report?.summary.total_floating_pnl ?? 0) > 0 ? 'income' : (report?.summary.total_floating_pnl ?? 0) < 0 ? 'expense' : 'neutral'"
          :badge="`${(report?.summary.floating_pct ?? 0) >= 0 ? '+' : ''}${(report?.summary.floating_pct ?? 0).toFixed(2)}%`"
          :badgeType="(report?.summary.total_floating_pnl ?? 0) >= 0 ? 'success' : 'danger'"
          :extraClass="floatCardClass"
        />
        <el-progress
          :percentage="Math.min(100, Math.abs(report?.summary.floating_pct ?? 0))"
          :color="floatPnlColor"
          :show-text="false"
          :stroke-width="3"
          class="pnl-progress"
        />
      </el-col>
      <el-col :span="8">
        <SummaryCard
          label="总已实现盈亏"
          :value="formatSigned(report?.summary.total_realized_pnl ?? 0)"
          :type="(report?.summary.total_realized_pnl ?? 0) > 0 ? 'income' : (report?.summary.total_realized_pnl ?? 0) < 0 ? 'expense' : 'neutral'"
          :badgeType="(report?.summary.total_realized_pnl ?? 0) >= 0 ? 'success' : 'danger'"
        />
      </el-col>
    </el-row>

    <!-- 图表 -->
    <el-row :gutter="24" class="chart-row">
      <el-col :span="10">
        <div class="chart-card">
          <div class="chart-title">资产配置</div>
          <HoldingsTypePie :data="typeShare" />
        </div>
      </el-col>
      <el-col :span="14">
        <div class="chart-card">
          <div class="chart-title">成本 vs 市值</div>
          <HoldingsCostBar :data="barData" />
        </div>
      </el-col>
    </el-row>

    <!-- 持仓表格 -->
    <div class="table-card">
      <el-table :data="pagedHoldings" class="premium-table" row-class-name="premium-row" header-cell-class-name="premium-header" empty-text="">
        <el-table-column label="标的名称" min-width="160">
          <template #default="{ row }">
            <div class="symbol-cell">
              <span class="asset-name">
                <Icon :icon="ASSET_ICONS[row.asset_type] ?? 'ph:briefcase'" class="asset-type-icon" />
                <span class="asset-name-text">{{ row.name }}</span>
              </span>
              <span class="symbol-code">{{ row.symbol || '—' }}</span>
            </div>
          </template>
        </el-table-column>
        <el-table-column label="类型" width="90">
          <template #default="{ row }">
            <span :class="['type-pill', typePillClass(row.asset_type)]">{{ typeLabel(row.asset_type) }}</span>
          </template>
        </el-table-column>
        <el-table-column label="占比" width="95">
          <template #default="{ row }">
            <div class="weight-cell">
              <span class="weight-pct">
                {{ ((Number(row.current_value) / (report?.summary.total_market_value || 1)) * 100).toFixed(1) }}%
              </span>
              <div class="weight-bar-track">
                <div
                  class="weight-bar-fill"
                  :style="{ width: `${Math.min(100, Math.max(0, (Number(row.current_value) / (report?.summary.total_market_value || 1)) * 100))}%` }"
                />
              </div>
            </div>
          </template>
        </el-table-column>
        <el-table-column label="份额" width="110" align="right">
          <template #default="{ row }"><span class="mf-mono">{{ row.quantity.toFixed(2) }}</span></template>
        </el-table-column>
        <el-table-column label="净值" width="110" align="right">
          <template #default="{ row }"><span class="mf-mono">{{ row.net_value.toFixed(4) }}</span></template>
        </el-table-column>
        <el-table-column label="成本" width="120" align="right">
          <template #default="{ row }"><span class="mf-mono">{{ formatCurrency(row.cost_basis) }}</span></template>
        </el-table-column>
        <el-table-column label="市值" width="130" align="right">
          <template #default="{ row }">
            <div style="display: flex; flex-direction: column; align-items: flex-end; justify-content: center;">
              <span class="mf-mono">{{ formatCurrency(row.current_value) }}</span>
              <span v-if="row.currency && row.currency !== 'CNY' && exchangeRates[row.currency]" class="cny-converted-hint">
                ≈ ¥{{ (Number(row.current_value) * (exchangeRates[row.currency] || 1)).toLocaleString('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 }) }}
              </span>
            </div>
          </template>
        </el-table-column>
        <el-table-column label="浮动盈亏" width="120" align="right">
          <template #default="{ row }">
            <span :class="pnlClass(row.floating_pnl)">{{ formatSigned(row.floating_pnl) }}</span>
          </template>
        </el-table-column>
        <el-table-column label="盈亏率" width="100" align="right">
          <template #default="{ row }">
            <span :class="pnlClass(row.floating_pnl)">
              {{ row.floating_pnl > 0 ? '↑' : row.floating_pnl < 0 ? '↓' : '—' }}
              {{ row.floating_pct.toFixed(2) }}%
            </span>
          </template>
        </el-table-column>
        <el-table-column label="已实现盈亏" width="120" align="right">
          <template #default="{ row }">
            <span :class="pnlClass(row.realized_pnl)">{{ formatSigned(row.realized_pnl) }}</span>
          </template>
        </el-table-column>
        <template #empty>
          <el-empty description="暂无持仓数据" :image-size="80" />
        </template>
      </el-table>
      <div class="pagination-bar">
        <el-pagination
          v-model:current-page="page"
          v-model:page-size="pageSize"
          :total="report?.holdings.length ?? 0"
          :page-sizes="[10, 20, 50, 100]"
          layout="total, sizes, prev, pager, next, jumper"
          background
          @size-change="handleSizeChange"
        />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { Refresh } from '@element-plus/icons-vue'
import { Icon } from '@iconify/vue'
import { reportsApi, type HoldingsReport } from '@/api/reports'
import { marketApi } from '@/api/market'
import HoldingsTypePie from '@/components/HoldingsTypePie.vue'
import HoldingsCostBar from '@/components/HoldingsCostBar.vue'
import SummaryCard from '@/components/SummaryCard.vue'
import { formatCurrency, formatSigned } from '@/utils/format'

const report = ref<HoldingsReport | null>(null)
const exchangeRates = ref<Record<string, number>>({})

const page = ref(1)
const pageSize = ref(20)

const pagedHoldings = computed(() => {
  const all = report.value?.holdings ?? []
  const start = (page.value - 1) * pageSize.value
  return all.slice(start, start + pageSize.value)
})

function handleSizeChange() {
  page.value = 1
}

const ASSET_ICONS: Record<string, string> = {
  stock: 'ph:trend-up',
  fund: 'ph:trend-up',
  bond: 'ph:trend-up',
  crypto: 'ph:currency-btc',
}
const TYPE_LABELS: Record<string, string> = {
  stock: '股票',
  fund: '基金',
  bond: '债券',
  crypto: '加密货币',
}
const TYPE_TAGS: Record<string, 'primary' | 'success' | 'warning' | 'danger'> = {
  stock: 'primary',
  fund: 'success',
  bond: 'warning',
  crypto: 'danger',
}

function typeLabel(t: string): string {
  return TYPE_LABELS[t] ?? t
}
function typeTag(t: string): 'primary' | 'success' | 'warning' | 'danger' | 'info' {
  return TYPE_TAGS[t] ?? 'info'
}
function typePillClass(t: string): string {
  return t === 'crypto' ? 'pill-crypto' : 'pill-default'
}
function pnlClass(v: number): string {
  return v > 0 ? 'income-text' : v < 0 ? 'expense-text' : ''
}

const floatCardClass = computed(() => {
  const pnl = report.value?.summary.total_floating_pnl ?? 0
  if (pnl > 0) return 'profit-card'
  if (pnl < 0) return 'loss-card'
  return ''
})

const floatPnlColor = computed(() => {
  const pnl = report.value?.summary.total_floating_pnl ?? 0
  return pnl >= 0 ? '#10b981' : '#ef4444'
})

const typeShare = computed(() => {
  const map = new Map<string, number>()
  for (const h of report.value?.holdings ?? []) {
    map.set(h.asset_type, (map.get(h.asset_type) ?? 0) + h.current_value)
  }
  return [...map.entries()].map(([name, value]) => ({ name, value }))
})

const barData = computed(() =>
  (report.value?.holdings ?? []).map((h) => ({
    name: h.name,
    cost_basis: h.cost_basis,
    current_value: h.current_value,
  })),
)

async function load() {
  try {
    const [hRes, rRes] = await Promise.allSettled([
      reportsApi.holdings(),
      marketApi.getExchangeRates()
    ])
    if (hRes.status === 'fulfilled' && hRes.value) {
      report.value = hRes.value
    }
    if (rRes.status === 'fulfilled' && rRes.value) {
      exchangeRates.value = rRes.value
    }
  } catch (e) {
    console.error('加载持仓数据失败', e)
  }
}

onMounted(() => {
  load()
})
</script>

<style scoped>
.holdings-page {
  height: 100%;
  overflow: hidden;
  display: flex;
  flex-direction: column;
  gap: 16px;
}
.cny-converted-hint {
  font-size: 11px;
  color: var(--mf-text-muted, #94a3b8);
  font-variant-numeric: tabular-nums;
  margin-top: 2px;
}
.summary-sub {
  font-size: 12px;
  color: var(--mf-text-muted);
  margin-left: 4px;
}
.chart-row {
  flex: 0 0 auto;
}
.chart-card,
.table-card {
  background: var(--mf-surface-card);
  border: 1px solid var(--mf-border);
  border-radius: var(--mf-radius-lg);
  padding: 16px;
  backdrop-filter: blur(16px);
  -webkit-backdrop-filter: blur(16px);
  box-shadow: var(--mf-shadow-sm);
  transition: var(--mf-transition);
}

.chart-card:hover {
  border-color: var(--mf-border-hover);
  box-shadow: var(--mf-shadow-glow);
}

.chart-title {
  font-size: 14px;
  font-weight: 600;
  color: var(--mf-text-main);
  margin-bottom: 8px;
}
.table-card {
  flex: 1;
  overflow: hidden;
  display: flex;
  flex-direction: column;
}
.table-card :deep(.el-table) {
  flex: 1;
  min-height: 0;
}
.premium-table {
  --el-table-border-color: transparent;
  --el-table-header-bg-color: var(--mf-surface-muted);
}
.pagination-bar {
  display: flex;
  justify-content: flex-end;
  margin-top: 16px;
}
.profit-card {
  background: var(--mf-success-light);
  border-color: var(--mf-success-border);
}
.loss-card {
  background: var(--mf-danger-light);
  border-color: var(--mf-danger-border);
}
.symbol-cell {
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.asset-name {
  font-weight: 600;
  display: flex;
  align-items: center;
  gap: 6px;
  color: var(--mf-text-main);
}
.asset-type-icon {
  font-size: 16px;
  color: var(--mf-primary);
  flex-shrink: 0;
}
.symbol-code {
  font-size: 11px;
  color: var(--mf-text-muted);
  font-family: var(--mf-font-mono);
  padding-left: 22px;
}
.weight-cell {
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.weight-pct {
  font-size: 11px;
  color: var(--mf-text-muted);
  font-family: var(--mf-font-mono);
}
.weight-bar-track {
  width: 100%;
  height: 4px;
  background: var(--mf-surface-muted);
  border-radius: var(--mf-radius-pill);
  overflow: hidden;
}
.weight-bar-fill {
  height: 100%;
  background: linear-gradient(90deg, var(--mf-primary), var(--mf-accent));
  border-radius: var(--mf-radius-pill);
  transition: width 0.3s ease;
}

.type-pill {
  font-size: 11px;
  padding: 2px 8px;
  border-radius: var(--mf-radius-pill);
  background: var(--mf-primary-light);
  color: var(--mf-primary);
  border: 1px solid var(--mf-primary-border);
  font-weight: 500;
}
.type-pill.pill-crypto {
  background: var(--mf-accent-light);
  color: var(--mf-accent);
  border-color: var(--mf-accent);
}

.pnl-progress {
  margin-top: 6px;
  width: 100%;
}

.mf-mono {
  font-variant-numeric: tabular-nums;
  font-family: var(--mf-font-mono);
}
</style>