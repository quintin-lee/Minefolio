<template>
  <div class="reports-mobile">
    <div class="page-header">
      <h2>{{ t('nav.reports') }}</h2>
      <span v-if="localOnly" class="cache-pill">离线报表（本地计算）</span>
    </div>

    <div class="report-tabs">
      <button
        v-for="tab in reportTabs"
        :key="tab.value"
        type="button"
        class="report-tab-btn"
        :class="{ active: activeTab === tab.value }"
        @click="switchTab(tab.value)"
      >
        {{ tab.label }}
      </button>
    </div>

    <el-alert
      v-if="loadError"
      title="无法获取报表数据（当前离线或服务端不可达）"
      type="warning"
      :closable="false"
      show-icon
      style="margin-bottom: 12px"
    />

    <!-- Tab 1: 月度收支 -->
    <template v-if="activeTab === 'monthly'">
      <el-date-picker
        v-model="month"
        type="month"
        value-format="YYYY-MM"
        :clearable="false"
        :editable="false"
        style="width: 100%"
        @change="load"
      />

      <div v-if="monthly" class="summary-strip">
        <div class="strip-col">
          <span class="strip-lbl">收入</span>
          <span class="strip-val income mf-amount-mask">{{ formatCurrency(monthly.total_income) }}</span>
        </div>
        <div class="strip-col">
          <span class="strip-lbl">支出</span>
          <span class="strip-val expense mf-amount-mask">{{ formatCurrency(monthly.total_expense) }}</span>
        </div>
        <div class="strip-col">
          <span class="strip-lbl">结余</span>
          <span class="strip-val mf-amount-mask">{{ formatCurrency(monthly.balance) }}</span>
        </div>
      </div>

      <div class="chart-block"><h4>分类占比</h4><div class="pie-wrap"><ExpenseCategoryPie :data="monthly?.by_category ?? []" /></div></div>
      <div class="chart-block"><h4>月度收支</h4><div class="monthly-wrap"><MonthlyChart :data="monthly" min-height="240px" /></div></div>

      <div v-if="monthly?.by_tag?.length" class="chart-block">
        <h4>标签统计</h4>
        <div class="tag-chips">
          <div v-for="tg in monthly.by_tag" :key="tg.tag_name" class="tag-stat-item">
            <span class="tag-name">{{ tg.tag_name }} ({{ tg.count }})</span>
            <span class="tag-amt mf-amount-mask">{{ formatCurrency(tg.amount) }}</span>
          </div>
        </div>
      </div>
    </template>

    <!-- Tab 2: 收支趋势 -->
    <template v-else-if="activeTab === 'trend'">
      <div class="chart-block" v-loading="tabLoading">
        <h4>收支趋势（近6月）</h4>
        <div v-if="trend?.labels?.length" class="trend-wrap">
          <ExpenseTrendBar :data="trend" />
        </div>
        <el-empty v-else-if="!tabLoading" description="暂无近 6 个月趋势数据" :image-size="64" />
      </div>
    </template>

    <!-- Tab 3: 年度对比 -->
    <template v-else-if="activeTab === 'yearly'">
      <el-date-picker
        v-model="reportYear"
        type="year"
        value-format="YYYY"
        :clearable="false"
        :editable="false"
        style="width: 100%"
        @change="loadYearly"
      />
      <div class="chart-block" v-loading="tabLoading">
        <h4>{{ reportYear }} 年度月收支对比</h4>
        <div v-if="yearly?.labels?.length" class="yearly-wrap">
          <YearlyChart :data="yearly" min-height="250px" />
        </div>
        <el-empty v-else-if="!tabLoading" description="暂无年度对比数据" :image-size="64" />
      </div>
    </template>

    <!-- Tab 4: 资产分析 -->
    <template v-else>
      <div class="period-row">
        <button
          v-for="p in periodOptions"
          :key="p.value"
          type="button"
          class="period-btn"
          :class="{ active: trendPeriod === p.value }"
          @click="selectPeriod(p.value)"
        >
          {{ p.label }}
        </button>
      </div>

      <div v-if="assetBreakdown" class="summary-strip">
        <div class="strip-col">
          <span class="strip-lbl">总资产</span>
          <span class="strip-val mf-amount-mask">{{ formatCurrency(assetBreakdown.total_assets) }}</span>
        </div>
        <div class="strip-col">
          <span class="strip-lbl">总负债</span>
          <span class="strip-val expense mf-amount-mask">{{ formatCurrency(Math.abs(assetBreakdown.total_liabilities)) }}</span>
        </div>
        <div class="strip-col">
          <span class="strip-lbl">净资产</span>
          <span class="strip-val income mf-amount-mask">{{ formatCurrency(assetBreakdown.net_worth) }}</span>
        </div>
      </div>

      <div class="chart-block" v-loading="tabLoading">
        <h4>净资产趋势</h4>
        <div v-if="assetTrend?.labels?.length" class="asset-chart-wrap">
          <AssetTrendLine :data="assetTrend" />
        </div>
        <el-empty v-else-if="!tabLoading" description="暂无净资产趋势数据" :image-size="64" />
      </div>

      <div class="chart-block" v-loading="tabLoading">
        <h4>资产分布</h4>
        <div v-if="assetPieData.length > 0" class="asset-chart-wrap">
          <AssetBreakdownPie :data="assetPieData" />
        </div>
        <el-empty v-else-if="!tabLoading" description="暂无资产分布数据" :image-size="64" />
      </div>
    </template>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { reportsApi, type ExpenseTrend, type ExpenseYearlyReport, type AssetTrend, type AssetBreakdown } from '@/api/reports'
import { useLedgerStore } from '@/stores/ledger'
import ExpenseCategoryPie from '@/components/ExpenseCategoryPie.vue'
import MonthlyChart from '@/components/MonthlyChart.vue'
import ExpenseTrendBar from '@/components/ExpenseTrendBar.vue'
import YearlyChart from '@/components/YearlyChart.vue'
import AssetTrendLine from '@/components/AssetTrendLine.vue'
import AssetBreakdownPie from '@/components/AssetBreakdownPie.vue'
import { localThisMonth, formatCurrency } from '@/utils/format'
import { aggregateMonthlyReport } from '@/composables/mobile/useLocalMonthlyReport'
import { loadViewCache, saveViewCache } from '@/composables/mobile/useViewCache'
import { notifyMobileReadError } from '@/utils/http'
import { t } from '@/utils/locale'
import type { ExpenseMonthly } from '@/types'

type ReportTab = 'monthly' | 'trend' | 'yearly' | 'asset'

const reportTabs: { label: string; value: ReportTab }[] = [
  { label: '月度收支', value: 'monthly' },
  { label: '收支趋势', value: 'trend' },
  { label: '年度对比', value: 'yearly' },
  { label: '资产分析', value: 'asset' },
]

const periodOptions = [
  { label: '近30天', value: '30d' },
  { label: '近90天', value: '90d' },
  { label: '近1年', value: '365d' },
]

const ledgerStore = useLedgerStore()
const activeTab = ref<ReportTab>('monthly')
const month = ref(localThisMonth())
const reportYear = ref(String(new Date().getFullYear()))
const trendPeriod = ref('30d')

const monthly = ref<ExpenseMonthly | null>(null)
const trend = ref<ExpenseTrend | null>(null)
const yearly = ref<ExpenseYearlyReport | null>(null)
const assetTrend = ref<AssetTrend | null>(null)
const assetBreakdown = ref<AssetBreakdown | null>(null)

const loadError = ref(false)
const localOnly = ref(false)
const tabLoading = ref(false)
let loadReqSeq = 0

const assetPieData = computed(() => {
  const rows = assetBreakdown.value?.assets ?? []
  return rows.map((a: any) => ({
    category_name: a.category_name ?? a.name ?? '未分类',
    value: Number(a.value) || 0,
    pct: Number(a.pct) || 0,
  }))
})

function activeLedgerId(): number {
  return ledgerStore.currentLedgerId || Number(localStorage.getItem('minefolio_active_ledger_id') || 0)
}

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
    localOnly.value = hasLocalData
    console.error('[ReportsMobile] remote load failed, using local aggregate:', e)
  }
}

async function loadTrend() {
  const key = `report_trend:${activeLedgerId()}`
  const cached = loadViewCache<ExpenseTrend>(key)
  if (cached.data) trend.value = cached.data
  tabLoading.value = true
  try {
    if (typeof reportsApi.expenseTrend === 'function') {
      const res = await reportsApi.expenseTrend(6)
      trend.value = res
      saveViewCache(key, res)
    }
  } catch (e) {
    if (!trend.value) notifyMobileReadError(e, '收支趋势')
  } finally {
    tabLoading.value = false
  }
}

async function loadYearly() {
  const y = Number(reportYear.value) || new Date().getFullYear()
  const key = `report_yearly:${activeLedgerId()}:${y}`
  const cached = loadViewCache<ExpenseYearlyReport>(key)
  if (cached.data) yearly.value = cached.data
  tabLoading.value = true
  try {
    if (typeof reportsApi.expenseYearly === 'function') {
      const res = await reportsApi.expenseYearly(y)
      yearly.value = res
      saveViewCache(key, res)
    }
  } catch (e) {
    if (!yearly.value) notifyMobileReadError(e, '年度报表')
  } finally {
    tabLoading.value = false
  }
}

async function loadAssetAnalysis() {
  const lid = activeLedgerId()
  const trendKey = `report_asset_trend:${lid}:${trendPeriod.value}`
  const breakdownKey = `report_asset_breakdown:${lid}`
  const cachedTrend = loadViewCache<AssetTrend>(trendKey)
  const cachedBreakdown = loadViewCache<AssetBreakdown>(breakdownKey)
  if (cachedTrend.data) assetTrend.value = cachedTrend.data
  if (cachedBreakdown.data) assetBreakdown.value = cachedBreakdown.data
  tabLoading.value = true
  try {
    const [atRes, abRes] = await Promise.allSettled([
      typeof reportsApi.assetTrend === 'function' ? reportsApi.assetTrend(trendPeriod.value) : Promise.resolve(null),
      typeof reportsApi.assetBreakdown === 'function' ? reportsApi.assetBreakdown() : Promise.resolve(null),
    ])
    if (atRes.status === 'fulfilled' && atRes.value) {
      assetTrend.value = atRes.value
      saveViewCache(trendKey, atRes.value)
    }
    if (abRes.status === 'fulfilled' && abRes.value) {
      assetBreakdown.value = abRes.value
      saveViewCache(breakdownKey, abRes.value)
    }
  } catch (e) {
    if (!assetTrend.value && !assetBreakdown.value) {
      notifyMobileReadError(e, '资产分析')
    }
  } finally {
    tabLoading.value = false
  }
}

function switchTab(tab: ReportTab) {
  activeTab.value = tab
  loadError.value = false
  if (tab === 'trend' && !trend.value) void loadTrend()
  else if (tab === 'yearly' && !yearly.value) void loadYearly()
  else if (tab === 'asset' && (!assetTrend.value || !assetBreakdown.value)) void loadAssetAnalysis()
}

function selectPeriod(p: string) {
  trendPeriod.value = p
  void loadAssetAnalysis()
}

onMounted(() => {
  load()
})
</script>

<style scoped>
.page-header { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
.cache-pill { font-size: 11px; color: var(--mf-warning); border: 1px solid var(--mf-warning); border-radius: 999px; padding: 2px 8px; white-space: nowrap; }
.report-tabs {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 6px;
  margin: 10px 0 12px;
  padding: 4px;
  background: var(--mf-surface-muted);
  border-radius: 10px;
}
.report-tab-btn {
  border: none;
  background: transparent;
  color: var(--mf-text-muted);
  font-size: 12px;
  padding: 7px 4px;
  border-radius: 8px;
  cursor: pointer;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.report-tab-btn.active {
  background: var(--mf-surface);
  color: var(--mf-primary);
  font-weight: 600;
  box-shadow: var(--mf-shadow-sm);
}
.summary-strip {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 8px;
  margin: 12px 0;
  padding: 12px;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 12px;
}
.strip-col { display: flex; flex-direction: column; gap: 4px; min-width: 0; }
.strip-lbl { font-size: 11px; color: var(--mf-text-muted); }
.strip-val { font-family: 'JetBrains Mono', monospace; font-size: 13px; font-weight: 600; color: var(--mf-text-main); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.income { color: var(--mf-income); }
.expense { color: var(--mf-expense); }
.chart-block { background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 14px; margin: 12px 0; }
.chart-block h4 { margin: 0 0 12px; color: var(--mf-text-muted); font-size: 13px; }
.pie-wrap { height: 280px; }
.monthly-wrap { height: 250px; }
.trend-wrap { height: 250px; }
.yearly-wrap { height: 255px; }
.asset-chart-wrap { height: 240px; }
.tag-chips { display: flex; flex-direction: column; gap: 8px; }
.tag-stat-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 8px 10px;
  border-radius: 8px;
  background: var(--mf-surface-muted);
  font-size: 13px;
}
.tag-amt { font-family: 'JetBrains Mono', monospace; font-weight: 600; }
.period-row { display: flex; gap: 8px; margin-bottom: 10px; }
.period-btn {
  flex: 1;
  padding: 6px 8px;
  border-radius: 8px;
  border: 1px solid var(--mf-border);
  background: var(--mf-surface);
  color: var(--mf-text-muted);
  font-size: 12px;
  cursor: pointer;
}
.period-btn.active {
  color: var(--mf-primary);
  border-color: var(--mf-primary-border);
  background: var(--mf-primary-light);
  font-weight: 600;
}
</style>
