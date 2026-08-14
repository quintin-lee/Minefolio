<template>
  <div class="holdings-page">
    <div class="page-header">
      <h2>持仓管理</h2>
      <div class="header-actions">
        <el-button :icon="Refresh" circle @click="load" />
      </div>
    </div>

    <!-- 汇总卡片 -->
    <el-row :gutter="24">
      <el-col :span="8">
        <div class="summary-card">
          <div class="summary-label">总市值</div>
          <div class="summary-value">{{ formatCurrency(report?.summary.total_market_value ?? 0) }}</div>
        </div>
      </el-col>
      <el-col :span="8">
        <div class="summary-card">
          <div class="summary-label">总浮动盈亏</div>
          <div class="summary-value" :class="pnlClass(report?.summary.total_floating_pnl ?? 0)">
            {{ formatSigned(report?.summary.total_floating_pnl ?? 0) }}
            <span class="summary-sub">({{ (report?.summary.floating_pct ?? 0).toFixed(2) }}%)</span>
          </div>
        </div>
      </el-col>
      <el-col :span="8">
        <div class="summary-card">
          <div class="summary-label">总已实现盈亏</div>
          <div class="summary-value" :class="pnlClass(report?.summary.total_realized_pnl ?? 0)">
            {{ formatSigned(report?.summary.total_realized_pnl ?? 0) }}
          </div>
        </div>
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
        <el-table-column label="名称" min-width="140">
          <template #default="{ row }">
            <span class="asset-name">{{ ASSET_ICONS[row.asset_type] ?? '📦' }} {{ row.name }}</span>
          </template>
        </el-table-column>
        <el-table-column label="类型" width="100">
          <template #default="{ row }">
            <el-tag size="small" :type="typeTag(row.asset_type)">{{ typeLabel(row.asset_type) }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="份额" width="110" align="right">
          <template #default="{ row }">{{ row.quantity.toFixed(2) }}</template>
        </el-table-column>
        <el-table-column label="净值" width="110" align="right">
          <template #default="{ row }">{{ row.net_value.toFixed(4) }}</template>
        </el-table-column>
        <el-table-column label="成本" width="120" align="right">
          <template #default="{ row }">{{ formatCurrency(row.cost_basis) }}</template>
        </el-table-column>
        <el-table-column label="市值" width="120" align="right">
          <template #default="{ row }">{{ formatCurrency(row.current_value) }}</template>
        </el-table-column>
        <el-table-column label="浮动盈亏" width="120" align="right">
          <template #default="{ row }">
            <span :class="pnlClass(row.floating_pnl)">{{ formatSigned(row.floating_pnl) }}</span>
          </template>
        </el-table-column>
        <el-table-column label="盈亏率" width="100" align="right">
          <template #default="{ row }">
            <span :class="pnlClass(row.floating_pnl)">{{ row.floating_pct.toFixed(2) }}%</span>
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
import { reportsApi, type HoldingsReport } from '@/api/reports'
import HoldingsTypePie from '@/components/HoldingsTypePie.vue'
import HoldingsCostBar from '@/components/HoldingsCostBar.vue'

const report = ref<HoldingsReport | null>(null)

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
  stock: '📈',
  fund: '📊',
  bond: '📉',
  crypto: '🪙',
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
function pnlClass(v: number): string {
  return v > 0 ? 'income-text' : v < 0 ? 'expense-text' : ''
}
function formatCurrency(v: number): string {
  return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(v)
}
function formatSigned(v: number): string {
  const s = new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(Math.abs(v))
  return v > 0 ? `+${s}` : v < 0 ? `-${s}` : s
}

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
    report.value = await reportsApi.holdings()
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
  padding: 24px;
  height: 100%;
  overflow: hidden;
  display: flex;
  flex-direction: column;
  gap: 16px;
}
.summary-sub {
  font-size: 12px;
  color: var(--text-secondary, #94a3b8);
  margin-left: 4px;
}
.chart-row {
  flex: 0 0 auto;
}
.chart-card,
.table-card {
  background: var(--surface, #141a2e);
  border: 1px solid var(--border-color, #1f2a4a);
  border-radius: 12px;
  padding: 16px;
}
.chart-title {
  font-size: 14px;
  font-weight: 600;
  color: #e2e8f0;
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
.pagination-bar {
  display: flex;
  justify-content: flex-end;
  margin-top: 16px;
}
.asset-name {
  font-weight: 500;
}
</style>