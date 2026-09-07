<template>
  <div class="expenses-mobile">
    <div class="page-header">
      <h2>收支</h2>
      <el-button size="small" @click="loadMore" :loading="loading">加载更多</el-button>
    </div>
    <div class="summary-row">
      <span>收入 {{ formatCurrency(month?.total_income ?? 0) }}</span>
      <span>支出 {{ formatCurrency(month?.total_expense ?? 0) }}</span>
    </div>
    <div v-for="e in list" :key="e.id" class="expense-card" @click="edit(e)">
      <div class="top"><span class="cat">{{ e.category_name }}</span><span :class="e.expense_type === 'income' ? 'income' : 'expense'">{{ e.expense_type === 'income' ? '+' : '-' }}{{ formatCurrency(e.amount) }}</span></div>
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
import { query, rowsFrom } from '@/db/local'
import { formatCurrency } from '@/utils/format'
import type { DailyExpense, ExpenseMonthly } from '@/types'
import ExpenseQuickSheet from './ExpenseQuickSheet.vue'

const PAGE_SIZE = 20
const list = ref<DailyExpense[]>([])
const month = ref<ExpenseMonthly | null>(null)
const page = ref(1)
const loading = ref(false)
const sheetOpen = ref(false)
const editing = ref<DailyExpense | null>(null)

/**
 * 离线读取：从本地 sql.js 镜像分页查询收支记录，并关联出分类/资产名称。
 * 仅在网络请求失败时调用，避免覆盖线上更新的数据。
 */
function loadLocalList(reset: boolean): void {
  const offset = (page.value - 1) * PAGE_SIZE
  const res = query(
    `SELECT e.id, e.category_id, e.asset_id, e.expense_type, e.amount, e.currency,
            e.expense_date, e.note, e.created_at, e.updated_at,
            c.name AS category_name, a.name AS asset_name
       FROM daily_expenses e
       LEFT JOIN categories c ON c.id = e.category_id
       LEFT JOIN assets a ON a.id = e.asset_id
      WHERE e.__deleted = 0
      ORDER BY e.expense_date DESC, e.id DESC
      LIMIT ? OFFSET ?`,
    [PAGE_SIZE, offset]
  )
  const rows = rowsFrom(res) as unknown as DailyExpense[]
  list.value = reset ? rows : [...list.value, ...rows]
}

/** 离线读取：从本地 sql.js 镜像聚合指定月份的收支合计 */
function loadLocalMonth(year: number, monthNum: number): void {
  const prefix = `${String(year).padStart(4, '0')}-${String(monthNum).padStart(2, '0')}`
  const rows = rowsFrom(query(
    `SELECT expense_type, SUM(amount) AS total
       FROM daily_expenses
      WHERE __deleted = 0 AND substr(expense_date, 1, 7) = ?
      GROUP BY expense_type`,
    [prefix]
  ))
  let totalIncome = 0
  let totalExpense = 0
  for (const r of rows) {
    if (r.expense_type === 'income') totalIncome = Number(r.total) || 0
    else if (r.expense_type === 'expense') totalExpense = Number(r.total) || 0
  }
  month.value = {
    total_income: totalIncome,
    total_expense: totalExpense,
    balance: totalIncome - totalExpense,
  } as unknown as ExpenseMonthly
}

async function loadData(reset = false) {
  if (reset) page.value = 1
  loading.value = true
  try {
    const now = new Date()
    const year = now.getFullYear()
    const monthNum = now.getMonth() + 1
    const [res, m] = await Promise.allSettled([
      dailyExpensesApi.list({ page: page.value, page_size: PAGE_SIZE }),
      dailyExpensesApi.monthly(year, monthNum),
    ])
    if (res.status === 'fulfilled') {
      list.value = reset ? res.value.list : [...list.value, ...res.value.list]
    } else {
      // 离线/网络失败 → 兜底读取本地离线库，避免列表空白
      try {
        loadLocalList(reset)
      } catch (e) {
        console.error('[DailyExpensesMobile] loadLocalList failed:', e)
      }
    }
    if (m.status === 'fulfilled') {
      month.value = m.value
    } else {
      try {
        loadLocalMonth(year, monthNum)
      } catch (e) {
        console.error('[DailyExpensesMobile] loadLocalMonth failed:', e)
      }
    }
  } catch (err) {
    console.error('[DailyExpensesMobile] loadData failed:', err)
    try {
      loadLocalList(reset)
    } catch (e) {
      console.error('[DailyExpensesMobile] loadLocalList failed:', e)
    }
  } finally {
    loading.value = false
  }
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
