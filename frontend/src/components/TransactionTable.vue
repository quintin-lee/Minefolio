<template>
  <div class="table-container">
    <el-table :data="transactions" class="premium-table" row-class-name="premium-row" header-cell-class-name="premium-header">
      <el-table-column prop="transaction_date" label="日期" width="120" />
      <el-table-column prop="asset_name" label="资产" min-width="120" />
      <el-table-column prop="category_name" label="分类" min-width="120" />
      <el-table-column prop="transaction_type" label="类型" width="100">
        <template #default="{ row }">
          <el-tag size="small" effect="light" class="type-badge" round>
            {{ row.transaction_type }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column prop="amount" label="金额" width="120" align="right">
        <template #default="{ row }">
          <span class="mono-amount">{{ formatCurrency(row.amount) }}</span>
        </template>
      </el-table-column>
      <el-table-column prop="note" label="备注" min-width="150" />
    </el-table>
  </div>
</template>

<script setup lang="ts">
import type { Transaction } from '@/types'

defineProps<{ transactions: Transaction[] }>()

function formatCurrency(v: number) {
  return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(v)
}
</script>

<style scoped>
.table-container {
  background: var(--mf-surface);
  border-radius: 12px;
  padding: 12px;
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
}

.premium-table {
  --el-table-border-color: transparent;
  --el-table-header-bg-color: var(--mf-primary-light);
}

:deep(.premium-header th) {
  background-color: var(--mf-primary-light) !important;
  color: var(--mf-text-muted) !important;
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
  padding: 12px 0;
  color: var(--mf-text-main);
}

:deep(.premium-row:hover > td) {
  background-color: var(--mf-primary-light) !important;
}

.type-badge {
  font-weight: 600;
  padding: 4px 12px;
  border-radius: 12px;
}

.mono-amount {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-weight: 600;
  font-size: 14px;
  color: #e2e8f0;
}
</style>
