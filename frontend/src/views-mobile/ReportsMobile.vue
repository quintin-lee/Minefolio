<template>
  <div class="reports-mobile">
    <div class="page-header"><h2>{{ t('nav.reports') }}</h2></div>
    <el-alert
      v-if="loadError"
      title="无法获取月度报表数据（当前离线或服务端不可达）"
      type="warning"
      :closable="false"
      show-icon
      style="margin-bottom: 12px"
    />
    <el-date-picker
      v-model="month"
      type="month"
      value-format="YYYY-MM"
      :clearable="false"
      :editable="false"
      style="width: 100%"
      @change="load"
    />
    <div class="chart-block"><h4>分类占比</h4><div class="pie-wrap"><ExpenseCategoryPie :data="monthly?.by_category ?? []" /></div></div>
    <div class="chart-block"><h4>月度收支</h4><MonthlyChart :data="monthly" /></div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { dailyExpensesApi } from '@/api/daily_expenses'
import ExpenseCategoryPie from '@/components/ExpenseCategoryPie.vue'
import MonthlyChart from '@/components/MonthlyChart.vue'
import { localThisMonth } from '@/utils/format'
import { notifyMobileReadError } from '@/utils/http'
import { t } from '@/utils/locale'
import type { ExpenseMonthly } from '@/types'

const month = ref(localThisMonth())
const monthly = ref<ExpenseMonthly | null>(null)
const loadError = ref(false)
let loadReqSeq = 0

async function load() {
  if (!month.value) return
  const reqId = ++loadReqSeq
  loadError.value = false
  try {
    const y = Number(month.value.slice(0, 4))
    const m = Number(month.value.slice(5, 7))
    const res = await dailyExpensesApi.monthly(y, m)
    if (reqId !== loadReqSeq) return
    monthly.value = res
  } catch (e) {
    if (reqId !== loadReqSeq) return
    loadError.value = true
    notifyMobileReadError(e, '报表数据')
    console.error('[ReportsMobile] load failed:', e)
  }
}
onMounted(() => {
  load()
})
</script>

<style scoped>
.chart-block { background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 16px; margin: 12px 0; }
.chart-block h4 { margin: 0 0 12px; color: var(--mf-text-muted); }
.pie-wrap { height: 300px; } /* 🟡 ExpenseCategoryPie 图表是 height:100%，父容器必须有确定高度 */
</style>
