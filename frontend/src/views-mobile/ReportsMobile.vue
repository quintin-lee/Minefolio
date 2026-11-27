<template>
  <div class="reports-mobile">
    <div class="page-header">
      <h2>{{ t('nav.reports') }}</h2>
      <span v-if="localOnly" class="cache-pill">离线报表（本地计算）</span>
    </div>
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
import { aggregateMonthlyReport } from '@/composables/mobile/useLocalMonthlyReport'
import { notifyMobileReadError } from '@/utils/http'
import { t } from '@/utils/locale'
import type { ExpenseMonthly } from '@/types'

const month = ref(localThisMonth())
const monthly = ref<ExpenseMonthly | null>(null)
const loadError = ref(false)
const localOnly = ref(false)
let loadReqSeq = 0

async function load() {
  if (!month.value) return
  const reqId = ++loadReqSeq
  loadError.value = false
  const y = Number(month.value.slice(0, 4))
  const m = Number(month.value.slice(5, 7))

  // 本地优先：先用本地 SQLite 聚合渲染，避免首屏空白
  let hasLocalData = false
  try {
    const local = aggregateMonthlyReport(y, m)
    hasLocalData = local.total_income > 0 || local.total_expense > 0
    monthly.value = local
    localOnly.value = !hasLocalData ? false : true
  } catch (e) {
    console.warn('[ReportsMobile] aggregateMonthlyReport failed, falling back to remote:', e)
  }

  try {
    const res = await dailyExpensesApi.monthly(y, m)
    if (reqId !== loadReqSeq) return
    monthly.value = res
    localOnly.value = false
  } catch (e) {
    if (reqId !== loadReqSeq) return
    if (!hasLocalData) {
      loadError.value = true
      notifyMobileReadError(e, '报表数据')
    }
    // 有本地数据时静默保留，仅通过 localOnly 标记提示
    localOnly.value = hasLocalData
    console.error('[ReportsMobile] remote load failed, using local aggregate:', e)
  }
}
onMounted(() => {
  load()
})
</script>

<style scoped>
.page-header { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
.cache-pill { font-size: 11px; color: var(--mf-warning); border: 1px solid var(--mf-warning); border-radius: 999px; padding: 2px 8px; white-space: nowrap; }
.chart-block { background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 16px; margin: 12px 0; }
.chart-block h4 { margin: 0 0 12px; color: var(--mf-text-muted); }
.pie-wrap { height: 300px; } /* 🟡 ExpenseCategoryPie 图表是 height:100%，父容器必须有确定高度 */
</style>
