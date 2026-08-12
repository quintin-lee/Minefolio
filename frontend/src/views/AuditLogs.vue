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
  const params: any = {}
  if (filters.value.asset_id) params.asset_id = filters.value.asset_id
  logs.value = await assetLogsApi.list(params)
}

function resetFilters() {
  filters.value = { asset_id: '' }
  loadData()
}

onMounted(async () => {
  assets.value = await assetsApi.list()
  loadData()
})
</script>

<style scoped>
.audit-logs-page {
  padding: 24px;
  background-color: #f8fafc;
  min-height: 100%;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 24px;
}

.header-title {
  display: flex;
  align-items: center;
  gap: 12px;
}

.title-accent {
  width: 4px;
  height: 24px;
  background: linear-gradient(180deg, #8b5cf6 0%, #6d28d9 100%);
  border-radius: 4px;
}

.header-title h2 {
  margin: 0;
  font-size: 20px;
  font-weight: 600;
  color: #1e293b;
  letter-spacing: 0.5px;
}

.filter-panel {
  background: #ffffff;
  border-radius: 16px;
  padding: 20px 24px;
  margin-bottom: 24px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.04);
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

.filter-date {
  width: 260px;
}

.filter-actions {
  margin-left: auto;
}

.search-btn, .reset-btn {
  border-radius: 8px;
}

.table-container {
  background: #ffffff;
  border-radius: 16px;
  padding: 16px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.04);
}

.premium-table {
  --el-table-border-color: transparent;
  --el-table-header-bg-color: #f8fafc;
}

:deep(.premium-header th) {
  background-color: #f8fafc !important;
  color: #64748b;
  font-weight: 600;
  text-transform: uppercase;
  font-size: 12px;
  letter-spacing: 0.5px;
  padding: 12px 0;
  border-bottom: none !important;
}

:deep(.premium-row) {
  transition: all 0.2s ease;
}

:deep(.premium-row td) {
  border-bottom: 1px solid #f1f5f9;
  padding: 16px 0;
}

:deep(.premium-row:hover > td) {
  background-color: #f8fafc !important;
}

.mono-text {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  color: #475569;
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
