<template>
  <div class="dashboard">
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
import { summaryApi } from '@/api/summary'
import { dailyExpensesApi } from '@/api/daily_expenses'
import type { Summary, DailyExpense } from '@/types'
import NetWorthChart from '@/components/NetWorthChart.vue'
import AssetBreakdownPie from '@/components/AssetBreakdownPie.vue'
import MonthlyChart from '@/components/MonthlyChart.vue'

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
  const res = await summaryApi.get()
  summary.value = res
  loadMonthly()
  loadRecent()
}

async function loadMonthly() {
  const d = currentMonth.value
  const res = await dailyExpensesApi.monthly(d.getFullYear(), d.getMonth() + 1)
  monthlyExpenses.value = res
}

async function loadRecent() {
  const res = await dailyExpensesApi.list({ start_date: '2026-01-01' })
  recentExpenses.value = res.slice(0, 10)
}

onMounted(loadDashboard)
</script>

<style scoped>
.dashboard {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.summary-cards {
  margin-bottom: 0;
}

.stat-card {
  position: relative;
  border-radius: 12px;
  overflow: hidden;
  border: none;
  transition: transform 0.3s ease, box-shadow 0.3s ease;
}

.stat-card:hover {
  transform: translateY(-4px);
  box-shadow: 0 12px 24px rgba(0, 0, 0, 0.08) !important;
}

.stat-card::before {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 4px;
}

.stat-card.assets::before { background: linear-gradient(90deg, #409eff, #79bbff); }
.stat-card.liabilities::before { background: linear-gradient(90deg, #f56c6c, #f89898); }
.stat-card.networth::before { background: linear-gradient(90deg, #67c23a, #95d475); }
.stat-card.monthly::before { background: linear-gradient(90deg, #e6a23c, #eebe77); }

.stat-content {
  padding: 16px 8px;
}

.stat-label {
  font-size: 14px;
  color: #909399;
  margin-bottom: 8px;
  font-weight: 500;
}

.stat-value {
  font-size: 28px;
  font-weight: 700;
  letter-spacing: -0.5px;
}

.stat-card.assets .stat-value { color: #409eff; }
.stat-card.liabilities .stat-value { color: #f56c6c; }
.stat-card.networth .stat-value { color: #67c23a; }
.stat-card.monthly .stat-value { color: #e6a23c; }

.charts-row {
  margin-top: 0;
}

.chart-card {
  border-radius: 12px;
  border: none;
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.04) !important;
}

:deep(.el-card__header) {
  padding: 16px 20px;
  border-bottom: 1px solid #f0f2f5;
}

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.header-title {
  font-size: 16px;
  font-weight: 600;
  color: #303133;
}

.header-date-picker {
  width: 140px;
}

.income-text {
  color: #67c23a;
  font-weight: 600;
}

.expense-text {
  color: #f56c6c;
  font-weight: 600;
}

.premium-table {
  --el-table-border-color: #f0f2f5;
  --el-table-header-bg-color: #fafafa;
}

:deep(.el-table th.el-table__cell) {
  font-weight: 600;
  color: #606266;
}
</style>
