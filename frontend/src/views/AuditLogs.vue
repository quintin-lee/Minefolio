<template>
  <div class="audit-logs-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>资产变动日志</h2>
      </div>
    </div>

    <!-- 筛选 -->
    <div class="filter-panel">
      <el-form :inline="true" :model="filters" class="premium-filters">
        <el-form-item label="资产">
          <el-select v-model="filters.asset_id" placeholder="全部" clearable class="filter-select">
            <el-option v-for="a in assets" :key="a.id" :label="a.name" :value="String(a.id)" />
          </el-select>
        </el-form-item>
        <el-form-item class="filter-actions">
          <el-button type="primary" class="search-btn" @click="loadData">查询</el-button>
          <el-button class="reset-btn" @click="resetFilters">重置</el-button>
        </el-form-item>
      </el-form>
    </div>

    <!-- 表格 -->
    <div class="table-container">
      <el-table :data="logs" class="premium-table" row-class-name="premium-row" header-cell-class-name="premium-header">
        <el-table-column prop="created_at" label="时间" width="170">
          <template #default="{ row }">
            <span class="mono-text">{{ formatDateTime(row.created_at) }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="asset_name" label="资产" min-width="120">
          <template #default="{ row }">
            <span>{{ row.asset_name || '—' }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="delta" label="变动" width="140" align="right">
          <template #default="{ row }">
            <span :class="['mono-amount', row.delta > 0 ? 'income-text' : row.delta < 0 ? 'expense-text' : '']">
              {{ row.delta > 0 ? '+' : '' }}{{ formatCurrency(row.delta) }}
            </span>
          </template>
        </el-table-column>
        <el-table-column prop="balance_after" label="变动后余额" width="140" align="right">
          <template #default="{ row }">
            <span class="mono-text">{{ formatCurrency(row.balance_after) }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="source_type" label="来源" width="120">
          <template #default="{ row }">
            <el-tag :type="sourceTypeTag(row.source_type)" effect="light" class="type-badge" round>
              {{ sourceTypeLabel(row.source_type) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="source_id" label="关联ID" width="100" align="center">
          <template #default="{ row }">
            <span class="mono-text">{{ row.source_id }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="note" label="备注" min-width="150">
          <template #default="{ row }">
            <span class="muted-text">{{ row.note || '—' }}</span>
          </template>
        </el-table-column>
      </el-table>
      <div class="pagination-bar">
        <el-pagination v-model:current-page="page" v-model:page-size="pageSize" :total="total" :page-sizes="[10, 20, 50, 100]" layout="total, sizes, prev, pager, next, jumper" background @current-change="loadData" @size-change="handleSizeChange" />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { assetLogsApi } from '@/api/asset_logs'
import { assetsApi } from '@/api/assets'
import { formatCurrency } from '@/utils/format'
import type { AssetBalanceLog, Asset } from '@/types'

const logs = ref<AssetBalanceLog[]>([])
const assets = ref<Asset[]>([])
const filters = ref({ asset_id: '' as string })
const page = ref(1)
const pageSize = ref(20)
const total = ref(0)

const sourceTypeMap: Record<string, { label: string; tag: 'success' | 'warning' | 'info' | 'danger' }> = {
  daily_expense: { label: '日常收支', tag: 'danger' },
  transaction: { label: '交易', tag: 'info' },
  manual_adjust: { label: '手动调整', tag: 'warning' },
  transfer: { label: '转账', tag: 'success' },
}

function sourceTypeLabel(t: string) { return sourceTypeMap[t]?.label || t }
function sourceTypeTag(t: string): 'success' | 'warning' | 'info' | 'danger' { return sourceTypeMap[t]?.tag || 'info' }
function formatDateTime(s: string) { return s ? s.replace('T', ' ').slice(0, 16) : '—' }

async function loadData() {
  const params: any = { page: page.value, page_size: pageSize.value }
  if (filters.value.asset_id) params.asset_id = filters.value.asset_id
  const res = await assetLogsApi.list(params)
  logs.value = res.list
  total.value = res.total
}

function handleSizeChange() {
  page.value = 1
  loadData()
}

function resetFilters() {
  filters.value = { asset_id: '' }
  page.value = 1
  loadData()
}

onMounted(async () => {
  const res = await assetsApi.list({ page_size: 500 })
  assets.value = res.list
  loadData()
})
</script>

<style scoped>
.audit-logs-page {

  background-color: var(--mf-background);
  height: 100%;
  overflow: hidden;
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.filter-panel {
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  padding: 20px 24px;
  margin-bottom: 24px;
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
}

.premium-filters {
  display: flex;
  flex-wrap: wrap;
  gap: 16px;
  align-items: center;
}

.premium-filters :deep(.el-form-item) {
  margin-bottom: 0;
}

.filter-select {
  width: 170px;
}

.filter-actions {
  margin-left: auto;
}

.search-btn, .reset-btn {
  border-radius: var(--mf-radius-md);
}

.table-container {
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  padding: 16px;
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
  overflow: auto;
  display: flex;
  flex-direction: column;
  flex: 1;
  min-height: 0;
}

.table-container :deep(.el-table) {
  height: 100%;

  flex: 1;
  min-height: 0;
}

.table-container :deep(.el-table__body-wrapper) {
  overflow-y: auto;

  overflow-y: auto;
}

.premium-table {
  --el-table-border-color: transparent;
  --el-table-header-bg-color: rgba(0, 212, 255, 0.06);
}

.pagination-bar {
  display: flex;
  justify-content: flex-end;
  margin-top: 16px;
}

:deep(.premium-header th) {
  background-color: rgba(0, 212, 255, 0.06) !important;
  color: #94a3b8 !important;
  font-weight: 600;
  text-transform: uppercase;
  font-size: 12px;
  letter-spacing: 0.5px;
  padding: 12px 0;
  border-bottom: 1px solid var(--mf-border) !important;
}

:deep(.premium-row) {
  transition: all 0.2s ease;
}

:deep(.premium-row td) {
  border-bottom: 1px solid var(--mf-border);
  padding: 16px 0;
  color: var(--mf-text-main);
}

:deep(.premium-row:hover > td) {
  background-color: rgba(0, 212, 255, 0.04) !important;
}

.mono-text {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  color: #64748b;
}

.mono-amount {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-weight: 600;
  font-size: 15px;
}

.income-text { color: #10b981; }
.expense-text { color: #ef4444; }

.type-badge {
  font-weight: 600;
  padding: 4px 12px;
  border-radius: 12px;
}

.muted-text {
  color: #94a3b8;
}
</style>
