<template>
  <div class="tx-mobile">
    <div class="page-header"><h2>交易</h2></div>
    <div v-for="t in list" :key="t.id" class="tx-card">
      <div class="top"><span>{{ t.category_name || t.transaction_type }}</span><span :class="isIncome(t) ? 'income' : 'expense'">{{ isIncome(t) ? '+' : '-' }}{{ fmt(t.amount) }}</span></div>
      <div class="bottom"><span>{{ t.transaction_date }}</span><span>{{ t.asset_name }}</span></div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { transactionsApi } from '@/api/transactions'
import type { Transaction } from '@/types'

const list = ref<Transaction[]>([])
function fmt(v: number) { return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(v ?? 0) }
function isIncome(t: Transaction) { return ['deposit', 'transfer_in', 'income', 'interest', 'sell'].includes(t.transaction_type) }

onMounted(async () => {
  const res = await transactionsApi.list({ page_size: 50 })
  list.value = res.list
})
</script>

<style scoped>
.tx-card { background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 14px; margin-bottom: 10px; }
.tx-card .top { display: flex; justify-content: space-between; font-size: 16px; }
.tx-card .bottom { display: flex; justify-content: space-between; color: var(--mf-text-muted); font-size: 12px; margin-top: 6px; }
.income { color: #34d399; } .expense { color: #f87171; }
</style>
