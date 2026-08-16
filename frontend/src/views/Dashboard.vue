<template>
  <div class="dashboard" v-loading="loading">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>仪表盘</h2>
      </div>
    </div>
    <!-- 资产概览卡片 -->
    <el-row :gutter="20" class="summary-cards">
      <el-col :xs="12" :sm="12" :md="6">
        <el-card shadow="hover" class="stat-card assets">
          <div class="stat-content">
            <div class="stat-label">总资产</div>
            <div class="stat-value">{{ formatCurrency(summary.total_assets) }}</div>
          </div>
        </el-card>
      </el-col>
      <el-col :xs="12" :sm="12" :md="6">
        <el-card shadow="hover" class="stat-card liabilities">
          <div class="stat-content">
            <div class="stat-label">总负债</div>
            <div class="stat-value">{{ formatCurrency(summary.total_liabilities) }}</div>
          </div>
        </el-card>
      </el-col>
      <el-col :xs="12" :sm="12" :md="6">
        <el-card shadow="hover" class="stat-card networth">
          <div class="stat-content">
            <div class="stat-label">净资产</div>
            <div class="stat-value">{{ formatCurrency(summary.net_worth) }}</div>
          </div>
        </el-card>
      </el-col>
      <el-col :xs="12" :sm="12" :md="6">
        <el-card shadow="hover" class="stat-card monthly">
          <div class="stat-content">
            <div class="stat-label">本月结余</div>
            <div class="stat-value">{{ formatCurrency(monthlyExpenses?.balance ?? 0) }}</div>
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
          <NetWorthChart :data="summary.trend" />
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
      <!-- 月度收支 -->
      <el-col :span="12">
        <el-card shadow="hover" class="chart-card">
          <template #header>
            <div class="card-header">
              <span class="header-title">月度收支</span>
              <el-date-picker
                v-model="currentMonth"
                type="month"
                placeholder="选择月份"
                size="small"
                class="header-date-picker"
                @change="loadMonthly"
              />
            </div>
          </template>
          <MonthlyChart :data="monthlyExpenses" />
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
            <el-table-column prop="expense_type" label="类型" width="70">
              <template #default="{ row }">
                <el-tag :type="row.expense_type === 'income' ? 'success' : 'danger'" size="small" effect="light" round>
                  {{ row.expense_type === 'income' ? '收入' : '支出' }}
                </el-tag>
              </template>
            </el-table-column>
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
import { summaryApi } from '@/api/summary'
import { dailyExpensesApi } from '@/api/daily_expenses'
import type { Summary, DailyExpense } from '@/types'
import NetWorthChart from '@/components/NetWorthChart.vue'
import AssetBreakdownPie from '@/components/AssetBreakdownPie.vue'
import MonthlyChart from '@/components/MonthlyChart.vue'

const loading = ref(true)
const summary = ref<Summary>({
  total_assets: 0, total_liabilities: 0, net_worth: 0,
  breakdown: [], trend: [],
})
const monthlyExpenses = ref<any>(null)
const recentExpenses = ref<DailyExpense[]>([])
const currentMonth = ref(new Date())

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
    await Promise.allSettled([loadMonthly(), loadRecent()])
  } catch (e: any) {
    ElMessage.error(e?.response?.data?.message || '仪表盘数据加载失败')
  } finally {
    loading.value = false
  }
}

async function loadMonthly() {
  const d = currentMonth.value
  const res = await dailyExpensesApi.monthly(d.getFullYear(), d.getMonth() + 1)
  monthlyExpenses.value = res
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
}

.stat-label {
  font-size: 13px;
  color: var(--mf-text-muted);
  margin-bottom: 6px;
  font-weight: 500;
}

.stat-value {
  font-size: 26px;
  font-weight: 700;
  letter-spacing: -0.5px;
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
</style>
