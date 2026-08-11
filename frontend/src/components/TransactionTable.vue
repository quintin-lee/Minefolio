<template>
  <el-table :data="transactions" stripe>
    <el-table-column prop="transaction_date" label="日期" width="120" />
    <el-table-column prop="asset_name" label="资产" />
    <el-table-column prop="category_name" label="分类" />
    <el-table-column prop="transaction_type" label="类型" width="80" />
    <el-table-column prop="amount" label="金额" width="120">
      <template #default="{ row }">{{ formatCurrency(row.amount) }}</template>
    </el-table-column>
    <el-table-column prop="note" label="备注" />
  </el-table>
</template>

<script setup lang="ts">
import type { Transaction } from '@/types'

defineProps<{ transactions: Transaction[] }>()

function formatCurrency(v: number) {
  return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(v)
}
</script>
