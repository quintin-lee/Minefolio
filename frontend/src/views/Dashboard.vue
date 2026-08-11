<template>
  <div class="dashboard">
    <!-- 资产概览卡片 -->
    <el-row :gutter="16" class="summary-cards">
      <el-col :span="6">
        <el-card shadow="hover" class="stat-card assets">
          <div class="stat-value">{{ formatCurrency(summary.total_assets) }}</div>
          <div class="stat-label">总资产</div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="hover" class="stat-card liabilities">
          <div class="stat-value">{{ formatCurrency(summary.total_liabilities) }}</div>
          <div class="stat-label">总负债</div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="hover" class="stat-card networth">
          <div class="stat-value">{{ formatCurrency(summary.net_worth) }}</div>
          <div class="stat-label">净资产</div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="hover" class="stat-card monthly">
          <div class="stat-value">
            {{ formatCurrency(monthlyExpenses?.balance ?? 0) }}
          </div>
          <div class="stat-label">本月结余</div>
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="16" class="charts-row">
      <!-- 净资产趋势 -->
      <el-col :span="16">
        <el-card shadow="hover">
          <template #header>净资产趋势</template>
          <NetWorthChart :data="summary.trend" />
        </el-card>
      </el-col>
      <!-- 分类占比 -->
      <el-col :span="8">
        <el-card shadow="hover">
          <template #header>资产分布</template>
          <AssetBreakdownPie :data="summary.breakdown" />
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="16" class="charts-row">
      <!-- 月度收支 -->
      <el-col :span="12">
        <el-card shadow="hover">
          <template #header>
            月度收支
            <el-date-picker
              v-model="currentMonth"
              type="month"
              placeholder="选择月份"
              size="small"
              style="margin-left: 12px"
              @change="loadMonthly"
            />
          </template>
          <MonthlyChart :data="monthlyExpenses" />
        </el-card>
      </el-col>
      <!-- 近期收支记录 -->
      <el-col :span="12">
        <el-card shadow="hover">
          <template #header>最近收支</template>
          <el-table :data="recentExpenses" size="small" max-height="300">
            <el-table-column prop="expense_date" label="日期" width="110" />
            <el-table-column prop="category_name" label="分类" />
            <el-table-column prop="expense_type" label="类型" width="60">
              <template #default="{ row }">
                <el-tag :type="row.expense_type === 'income' ? 'success' : 'danger'" size="small">
                  {{ row.expense_type === 'income' ? '收' : '支' }}
                </el-tag>
              </template>
            </el-table-column>
            <el-table-column prop="amount" label="金额" width="100">
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
  summary.value = res.data
  loadMonthly()
  loadRecent()
}

async function loadMonthly() {
  const d = currentMonth.value
  const res = await dailyExpensesApi.monthly(d.getFullYear(), d.getMonth() + 1)
  monthlyExpenses.value = res.data
}

async function loadRecent() {
  const res = await dailyExpensesApi.list({ start_date: '2026-01-01' })
  recentExpenses.value = res.data.slice(0, 10)
}

onMounted(loadDashboard)
</script>

<style scoped>
.dashboard { display: flex; flex-direction: column; gap: 16px; }
.summary-cards { margin-bottom: 0; }
.stat-card { text-align: center; padding: 8px 0; }
.stat-value { font-size: 28px; font-weight: bold; color: #303133; }
.stat-label { font-size: 14px; color: #909399; margin-top: 4px; }
.stat-card.assets .stat-value { color: #409eff; }
.stat-card.liabilities .stat-value { color: #f56c6c; }
.stat-card.networth .stat-value { color: #67c23a; }
.stat-card.monthly .stat-value { color: #e6a23c; }
.charts-row { margin-top: 0; }
.income-text { color: #67c23a; font-weight: bold; }
.expense-text { color: #f56c6c; font-weight: bold; }
</style>
