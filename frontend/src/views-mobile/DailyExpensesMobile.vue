<template>
  <div class="expenses-mobile">
    <div class="page-header">
      <h2>收支</h2>
      <el-button size="small" @click="loadMore" :loading="loading">加载更多</el-button>
    </div>
    <div class="summary-row">
      <span>收入 {{ fmt(month?.total_income ?? 0) }}</span>
      <span>支出 {{ fmt(month?.total_expense ?? 0) }}</span>
    </div>
    <div v-for="e in list" :key="e.id" class="expense-card" @click="edit(e)">
      <div class="top"><span class="cat">{{ e.category_name }}</span><span :class="e.expense_type === 'income' ? 'income' : 'expense'">{{ e.expense_type === 'income' ? '+' : '-' }}{{ fmt(e.amount) }}</span></div>
      <div class="bottom"><span>{{ e.expense_date }}</span><span>{{ e.asset_name }}</span></div>
    </div>

    <el-button class="fab" type="primary" circle :icon="Plus" @click="create" />
    <ExpenseQuickSheet v-model="sheetOpen" :record="editing" @saved="onSaved" />
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { Plus } from '@element-plus/icons-vue'
import { dailyExpensesApi } from '@/api/daily_expenses'
import type { DailyExpense, ExpenseMonthly } from '@/types'
import ExpenseQuickSheet from './ExpenseQuickSheet.vue'

const list = ref<DailyExpense[]>([])
const month = ref<ExpenseMonthly | null>(null)
const page = ref(1)
const loading = ref(false)
const sheetOpen = ref(false)
const editing = ref<DailyExpense | null>(null)

function fmt(v: number) {
  return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(v ?? 0)
}

async function loadData(reset = false) {
  if (reset) page.value = 1
  loading.value = true
  const now = new Date()
  const [res, m] = await Promise.all([
    dailyExpensesApi.list({ page: page.value, page_size: 20 }),
    dailyExpensesApi.monthly(now.getFullYear(), now.getMonth() + 1),
  ])
  list.value = reset ? res.list : [...list.value, ...res.list]
  month.value = m
  loading.value = false
}

function loadMore() { page.value++; loadData() }
function create() { editing.value = null; sheetOpen.value = true }
function edit(e: DailyExpense) { editing.value = e; sheetOpen.value = true }
function onSaved() { sheetOpen.value = false; loadData(true) }

onMounted(() => loadData(true))
</script>

<style scoped>
.expenses-mobile { padding-bottom: 80px; }
.summary-row { display: flex; gap: 16px; margin: 12px 0; color: var(--mf-text-muted); font-size: 13px; }
.expense-card { background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 14px; margin-bottom: 10px; cursor: pointer; }
.expense-card .top { display: flex; justify-content: space-between; font-size: 16px; }
.expense-card .bottom { display: flex; justify-content: space-between; color: var(--mf-text-muted); font-size: 12px; margin-top: 6px; }
.income { color: var(--mf-success); } .expense { color: var(--mf-danger); }
.fab { position: fixed; right: 20px; bottom: 80px; width: 56px; height: 56px; box-shadow: var(--mf-shadow-glow); }
</style>
