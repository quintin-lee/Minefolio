<template>
  <div class="reports-mobile">
    <div class="page-header"><h2>报表</h2></div>
    <el-date-picker v-model="month" type="month" value-format="YYYY-MM" @change="load" />
    <div class="chart-block"><h4>分类占比</h4><div class="pie-wrap"><ExpenseCategoryPie :data="monthly?.by_category ?? []" /></div></div>
    <div class="chart-block"><h4>月度收支</h4><MonthlyChart :data="monthly" /></div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { dailyExpensesApi } from '@/api/daily_expenses'
import ExpenseCategoryPie from '@/components/ExpenseCategoryPie.vue'
import MonthlyChart from '@/components/MonthlyChart.vue'
import type { ExpenseMonthly } from '@/types'

const month = ref(new Date().toISOString().slice(0, 7))
const monthly = ref<ExpenseMonthly | null>(null)

async function load() {
  const y = Number(month.value.slice(0, 4))
  const m = Number(month.value.slice(5, 7))
  monthly.value = await dailyExpensesApi.monthly(y, m)
}
onMounted(load)
</script>

<style scoped>
.chart-block { background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 16px; margin: 12px 0; }
.chart-block h4 { margin: 0 0 12px; color: var(--mf-text-muted); }
.pie-wrap { height: 260px; } /* 🟡 ExpenseCategoryPie 图表是 height:100%，父容器必须有确定高度 */
</style>
