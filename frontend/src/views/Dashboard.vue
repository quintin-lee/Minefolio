<template>
  <div class="dashboard" v-loading="loading">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>仪表盘</h2>
      </div>
    </div>
    <!-- 待办定投提醒 -->
    <div v-if="pendingDcaTasks.length > 0" class="dashboard-alert-banner">
      <div class="alert-left">
        <el-icon class="alert-icon"><BellFilled /></el-icon>
        <span>您有 <strong>{{ pendingDcaTasks.length }}</strong> 项定投计划待执行</span>
      </div>
      <el-button type="primary" size="small" @click="$router.push('/plans')">
        前往处理
      </el-button>
    </div>

    <!-- 资产概览卡片 -->
    <el-row :gutter="20" class="summary-cards">
      <el-col :xs="12" :sm="12" :md="6" class="mf-stagger-1">
        <el-card shadow="hover" class="stat-card assets">
          <div class="stat-content">
            <div class="stat-label">总资产</div>
            <div class="stat-value">{{ formatCurrency(summary.total_assets) }}</div>
            <div v-if="sparklinePoints && sparklineData.length > 1" class="sparkline-wrap">
              <svg class="sparkline" viewBox="0 0 60 20" preserveAspectRatio="none">
                <polyline :points="sparklinePoints" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" />
              </svg>
            </div>
          </div>
        </el-card>
      </el-col>
      <el-col :xs="12" :sm="12" :md="6" class="mf-stagger-2">
        <el-card shadow="hover" class="stat-card liabilities">
          <div class="stat-content">
            <div class="stat-label">总负债</div>
            <div class="stat-value">{{ formatCurrency(summary.total_liabilities) }}</div>
          </div>
        </el-card>
      </el-col>
      <el-col :xs="12" :sm="12" :md="6" class="mf-stagger-3">
        <el-card shadow="hover" class="stat-card networth">
          <div class="stat-content">
            <div class="stat-label">净资产</div>
            <div class="stat-value">{{ formatCurrency(summary.net_worth) }}</div>
            <div v-if="sparklinePoints && sparklineData.length > 1" class="sparkline-wrap">
              <svg class="sparkline" viewBox="0 0 60 20" preserveAspectRatio="none">
                <polyline :points="sparklinePoints" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" />
              </svg>
            </div>
          </div>
        </el-card>
      </el-col>
      <el-col :xs="12" :sm="12" :md="6" class="mf-stagger-4">
        <el-card shadow="hover" class="stat-card monthly">
          <div class="stat-content">
            <div class="stat-label">本月结余</div>
            <div class="stat-value">{{ formatCurrency(currentMonthBalance?.balance ?? 0) }}</div>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="20" class="charts-row">
      <!-- 净资产趋势 -->
      <el-col :span="16">
        <el-card shadow="hover" class="chart-card">
          <template #header>
            <div class="card-header">
              <span class="header-title">净资产趋势</span>
            </div>
          </template>
          <div class="nw-chart-wrap">
            <NetWorthChart :data="summary.trend" />
          </div>
        </el-card>
      </el-col>
      <!-- 分类占比 -->
      <el-col :span="8">
        <el-card shadow="hover" class="chart-card">
          <template #header>
            <div class="card-header">
              <span class="header-title">资产分布</span>
            </div>
          </template>
          <AssetBreakdownPie :data="summary.breakdown" />
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="20" class="charts-row">
      <!-- 年度收支 -->
      <el-col :span="12">
        <el-card shadow="hover" class="chart-card">
          <template #header>
            <div class="card-header">
              <span class="header-title">年度收支</span>
              <el-date-picker
                v-model="currentYear"
                type="year"
                placeholder="选择年份"
                size="small"
                class="header-date-picker"
                @change="loadYearly"
              />
            </div>
          </template>
          <YearlyChart :data="yearlyExpenses" />
        </el-card>
      </el-col>
      <!-- 近期收支记录 -->
      <el-col :span="12">
        <el-card shadow="hover" class="chart-card">
          <template #header>
            <div class="card-header">
              <span class="header-title">最近收支</span>
            </div>
          </template>
            <el-table :data="recentExpenses" stripe size="small" height="280" class="premium-table">
              <el-table-column prop="expense_date" label="日期" width="100" />
              <el-table-column prop="category_name" label="分类" />
              <el-table-column prop="amount" label="金额" width="120" align="right">
              <template #default="{ row }">
                <span :class="row.expense_type === 'income' ? 'income-text' : 'expense-text'">
                  {{ row.expense_type === 'income' ? '+' : '-' }}{{ formatCurrency(row.amount) }}
                </span>
              </template>
            </el-table-column>
          </el-table>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, computed } from 'vue'
import { ElMessage } from 'element-plus'
import { BellFilled } from '@element-plus/icons-vue'
import { summaryApi } from '@/api/summary'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { reportsApi } from '@/api/reports'
import { dcaApi } from '@/api/dca'
import type { Summary, DailyExpense } from '@/types'
import NetWorthChart from '@/components/NetWorthChart.vue'
import AssetBreakdownPie from '@/components/AssetBreakdownPie.vue'
import YearlyChart from '@/components/YearlyChart.vue'

const loading = ref(true)
const summary = ref<Summary>({
  total_assets: 0, total_liabilities: 0, net_worth: 0,
  breakdown: [], trend: [],
})
const yearlyExpenses = ref<any>(null)
const currentMonthBalance = ref<any>(null)
const recentExpenses = ref<DailyExpense[]>([])
const pendingDcaTasks = ref<any[]>([])
const currentYear = ref(new Date())

const sparklineData = computed(() => {
  const t = summary.value.trend ?? []
  return t.slice(-7).map(d => d.net_worth)
})

const sparklinePoints = computed(() => {
  const data = sparklineData.value
  if (data.length < 2) return ''
  const max = Math.max(...data)
  const min = Math.min(...data)
  const range = max - min || 1
  const w = 60, h = 20
  return data.map((v, i) => `${(i / (data.length - 1)) * w},${h - ((v - min) / range) * h}`).join(' ')
})

function formatCurrency(val: number) {
  return new Intl.NumberFormat('zh-CN', {
    style: 'currency', currency: 'CNY',
  }).format(val)
}

async function loadDashboard() {
  loading.value = true
  try {
    const res = await summaryApi.get()
    summary.value = res
    await Promise.allSettled([loadYearly(), loadCurrentMonth(), loadRecent(), loadPendingDca()])
  } catch (e: any) {
    ElMessage.error(e?.response?.data?.message || '仪表盘数据加载失败')
  } finally {
    loading.value = false
  }
}

async function loadPendingDca() {
  try {
    const tasks = await dcaApi.listPendingExecutions()
    pendingDcaTasks.value = tasks || []
  } catch (err) {
    console.error('load pending dca error:', err)
  }
}

async function loadYearly() {
  const year = currentYear.value.getFullYear()
  yearlyExpenses.value = await reportsApi.expenseYearly(year)
}

async function loadCurrentMonth() {
  const now = new Date()
  currentMonthBalance.value = await dailyExpensesApi.monthly(now.getFullYear(), now.getMonth() + 1)
}

async function loadRecent() {
  const res = await dailyExpensesApi.list({ page_size: 10 })
  recentExpenses.value = res.list
}

onMounted(loadDashboard)
</script>

<style scoped>
.dashboard {
  display: flex;
  flex-direction: column;
  gap: var(--mf-spacing-md);
  height: 100%;
  overflow: auto;
}

.stat-content {
  padding: 4px 4px;
  position: relative;
}

.sparkline-wrap {
  position: absolute;
  top: 8px;
  right: 12px;
  width: 60px;
  height: 20px;
  opacity: 0.6;
  color: currentColor;
}
.sparkline {
  width: 100%;
  height: 100%;
}

.stat-label {
  font-size: 13px;
  color: var(--mf-text-muted);
  margin-bottom: 6px;
  font-weight: 500;
}

.stat-value {
  font-size: 36px;
  font-weight: 700;
  letter-spacing: -1px;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
}

.stat-card.assets .stat-value      { color: #00d4ff; text-shadow: 0 0 12px rgba(0,212,255,0.5); }
.stat-card.liabilities .stat-value { color: #f87171; text-shadow: 0 0 12px rgba(248,113,113,0.4); }
.stat-card.networth .stat-value    { color: #34d399; text-shadow: 0 0 12px rgba(52,211,153,0.5); }
.stat-card.monthly .stat-value     { color: #fbbf24; text-shadow: 0 0 12px rgba(251,191,36,0.4); }

.chart-card {
  border-radius: var(--mf-radius-lg);
  border: 1px solid var(--mf-border);
  background: var(--mf-surface);
  backdrop-filter: blur(12px);
  box-shadow: var(--mf-shadow-sm);
}

.chart-card:hover {
  border-color: var(--mf-border-hover);
  box-shadow: var(--mf-shadow-glow);
}

:deep(.el-card__header) {
  padding: 14px 20px;
  border-bottom: 1px solid var(--mf-border);
  background: var(--mf-surface);
}

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.header-title {
  font-size: 15px;
  font-weight: 600;
  color: var(--mf-text-main);
}

.header-date-picker {
  width: 140px;
}

.nw-chart-wrap {
  height: 320px;
}

.income-text { color: #34d399; text-shadow: 0 0 8px rgba(52,211,153,0.4); font-weight: 600; }
.expense-text { color: #f87171; text-shadow: 0 0 8px rgba(248,113,113,0.3); font-weight: 600; }

.premium-table {
  --el-table-border-color: transparent;
  --el-table-header-bg-color: rgba(0, 212, 255, 0.06);
}

:deep(.el-table th.el-table__cell) {
  font-weight: 600;
  color: #94a3b8 !important;
  background-color: rgba(0, 212, 255, 0.06) !important;
  border-bottom: 1px solid var(--mf-border) !important;
}

.dashboard-alert-banner {
  background: linear-gradient(135deg, rgba(234, 179, 8, 0.15), rgba(245, 158, 11, 0.08));
  border: 1px solid rgba(234, 179, 8, 0.4);
  border-radius: var(--mf-radius-md);
  padding: 10px 16px;
  margin-bottom: 20px;
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.alert-left {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 14px;
  color: #d97706;
}

.alert-icon {
  font-size: 18px;
  color: #f59e0b;
}

:deep(.el-table .el-table__row) {
  height: 40px;
}
:deep(.el-table .el-table__cell) {
  padding: 8px 0;
  font-size: 12px;
}
</style>
