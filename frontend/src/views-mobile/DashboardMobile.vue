<template>
  <div class="dashboard-mobile" v-loading="loading">
    <div class="page-header"><h2>首页</h2></div>
    <div class="kpi-row">
      <div class="kpi-card cyan"><span>总资产</span><b>{{ fmt(summary.total_assets) }}</b></div>
      <div class="kpi-card red"><span>总负债</span><b>{{ fmt(summary.total_liabilities) }}</b></div>
      <div class="kpi-card green"><span>净资产</span><b>{{ fmt(summary.net_worth) }}</b></div>
    </div>
    <h3 class="section-title">本月收支</h3>
    <div class="mini-row">
      <div><span>收入</span><b class="income">{{ fmt(month?.total_income ?? 0) }}</b></div>
      <div><span>支出</span><b class="expense">{{ fmt(month?.total_expense ?? 0) }}</b></div>
      <div><span>结余</span><b>{{ fmt(month?.balance ?? 0) }}</b></div>
    </div>
    <h3 class="section-title">最近记录</h3>
    <div v-if="recent.length === 0 && !loading" class="empty-state">暂无交易记录</div>
    <div v-for="e in recent" :key="e.id" class="record-card">
      <span class="cat">{{ e.category_name }}</span>
      <span :class="e.expense_type === 'income' ? 'income' : 'expense'">
        {{ e.expense_type === 'income' ? '+' : '-' }}{{ fmt(e.amount) }}
      </span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { summaryApi } from '@/api/summary'
import { dailyExpensesApi } from '@/api/daily_expenses'
import type { Summary, DailyExpense, ExpenseMonthly } from '@/types'

const summary = ref<Summary>({ total_assets: 0, total_liabilities: 0, net_worth: 0, breakdown: [], trend: [] })
const month = ref<ExpenseMonthly | null>(null)
const recent = ref<DailyExpense[]>([])
const loading = ref(false)

function fmt(v: number) {
  return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(v ?? 0)
}

onMounted(async () => {
  loading.value = true
  try {
    const now = new Date()
    const [sRes, mRes, rRes] = await Promise.allSettled([
      summaryApi.get(),
      dailyExpensesApi.monthly(now.getFullYear(), now.getMonth() + 1),
      dailyExpensesApi.list({ page_size: 5 }),
    ])
    if (sRes.status === 'fulfilled' && sRes.value) {
      summary.value = sRes.value
    }
    if (mRes.status === 'fulfilled' && mRes.value) {
      month.value = mRes.value
    }
    if (rRes.status === 'fulfilled' && rRes.value) {
      recent.value = rRes.value.list
    }
  } catch (e) {
    console.error('[DashboardMobile] onMounted error:', e)
  } finally {
    loading.value = false
  }
})
</script>

<style scoped>
.kpi-row { display: flex; gap: 12px; overflow-x: auto; }
.kpi-card { flex: 0 0 120px; background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 16px; display: flex; flex-direction: column; gap: 8px; }
.kpi-card.cyan b { color: var(--mf-info); } .kpi-card.red b { color: var(--mf-danger); } .kpi-card.green b { color: var(--mf-success); }
.kpi-card span { color: var(--mf-text-muted); font-size: 12px; }
.kpi-card b { font-size: 18px; font-family: 'JetBrains Mono', monospace; }
.mini-row { display: flex; justify-content: space-between; margin: 12px 0; }
.mini-row > div { display: flex; flex-direction: column; gap: 4px; }
.mini-row span { color: var(--mf-text-muted); font-size: 12px; }
.mini-row b { font-family: 'JetBrains Mono', monospace; }
.section-title { margin: 16px 0 8px; font-size: 14px; color: var(--mf-text-muted); }
.record-card { display: flex; justify-content: space-between; padding: 12px; border-bottom: 1px solid var(--mf-border); }
.income { color: var(--mf-success); } .expense { color: var(--mf-danger); }
.empty-state { text-align: center; color: var(--mf-text-muted); padding: 20px 0; font-size: 13px; }
</style>
