<template>
  <div class="reports-page">
    <el-row :gutter="16">
      <!-- 月度收支报表 -->
      <el-col :span="12">
        <el-card>
          <template #header>
            月度收支报表
            <el-date-picker v-model="reportMonth" type="month" value-format="YYYY-MM" size="small" style="margin-left: 12px" @change="loadMonthlyReport" />
          </template>
          <el-descriptions :column="2" border>
            <el-descriptions-item label="总收入">{{ formatCurrency(monthly?.total_income ?? 0) }}</el-descriptions-item>
            <el-descriptions-item label="总支出">{{ formatCurrency(monthly?.total_expense ?? 0) }}</el-descriptions-item>
            <el-descriptions-item label="结余" :span="2">
              <span :style="{ color: (monthly?.balance ?? 0) >= 0 ? '#67c23a' : '#f56c6c', fontWeight: 'bold' }">
                {{ formatCurrency(monthly?.balance ?? 0) }}
              </span>
            </el-descriptions-item>
          </el-descriptions>
          <ExpenseCategoryPie :data="monthly?.by_category ?? []" style="margin-top: 16px" />
        </el-card>
      </el-col>

      <!-- 收支趋势 -->
      <el-col :span="12">
        <el-card>
          <template #header>收支趋势（近6月）</template>
          <ExpenseTrendBar :data="trend" />
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="16" style="margin-top: 16px">
      <!-- 资产趋势 -->
      <el-col :span="16">
        <el-card>
          <template #header>
            净资产趋势
            <el-radio-group v-model="trendPeriod" size="small" style="margin-left: 12px">
              <el-radio-button value="30d">近30天</el-radio-button>
              <el-radio-button value="90d">近90天</el-radio-button>
              <el-radio-button value="365d">近1年</el-radio-button>
            </el-radio-group>
          </template>
          <AssetTrendLine :data="assetTrend" />
        </el-card>
      </el-col>

      <!-- 资产分布 -->
      <el-col :span="8">
        <el-card>
          <template #header>资产分布</template>
          <AssetBreakdownPie :data="assetBreakdown?.assets ?? []" />
          <el-divider />
          <el-descriptions :column="1" size="small">
            <el-descriptions-item label="总资产">{{ formatCurrency(assetBreakdown?.total_assets ?? 0) }}</el-descriptions-item>
            <el-descriptions-item label="总负债">{{ formatCurrency(assetBreakdown?.total_liabilities ?? 0) }}</el-descriptions-item>
            <el-descriptions-item label="净资产">
              <span style="color: #67c23a; font-weight: bold">{{ formatCurrency(assetBreakdown?.net_worth ?? 0) }}</span>
            </el-descriptions-item>
          </el-descriptions>
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="16" style="margin-top: 16px">
      <el-col :span="12">
        <el-card>
          <template #header>标签支出分析</template>
          <el-table :data="tagBreakdown?.items ?? []" size="small">
            <el-table-column prop="tag_name" label="标签" />
            <el-table-column prop="amount" label="金额" width="120">
              <template #default="{ row }">{{ formatCurrency(row.amount) }}</template>
            </el-table-column>
            <el-table-column prop="pct" label="占比" width="80">
              <template #default="{ row }">{{ row.pct.toFixed(1) }}%</template>
            </el-table-column>
            <el-table-column prop="count" label="次数" width="80" />
          </el-table>
        </el-card>
      </el-col>
      <el-col :span="12">
        <el-card>
          <template #header>交易表现</template>
          <el-descriptions :column="2" border size="small">
            <el-descriptions-item label="总交易笔数">{{ perf?.total_trades ?? 0 }}</el-descriptions-item>
            <el-descriptions-item label="总收益">{{ formatCurrency(perf?.total_gain ?? 0) }}</el-descriptions-item>
            <el-descriptions-item label="总亏损">{{ formatCurrency(perf?.total_loss ?? 0) }}</el-descriptions-item>
            <el-descriptions-item label="净收益">
              <span :style="{ color: (perf?.net_gain ?? 0) >= 0 ? '#67c23a' : '#f56c6c' }">{{ formatCurrency(perf?.net_gain ?? 0) }}</span>
            </el-descriptions-item>
          </el-descriptions>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { reportsApi } from '@/api/reports'
import ExpenseCategoryPie from '@/components/ExpenseCategoryPie.vue'
import ExpenseTrendBar from '@/components/ExpenseTrendBar.vue'
import AssetTrendLine from '@/components/AssetTrendLine.vue'
import AssetBreakdownPie from '@/components/AssetBreakdownPie.vue'

const reportMonth = ref(new Date().toISOString().slice(0, 7))
const trendPeriod = ref('30d')
const monthly = ref<any>(null)
const trend = ref<any>(null)
const assetTrend = ref<any>(null)
const assetBreakdown = ref<any>(null)
const tagBreakdown = ref<any>(null)
const perf = ref<any>(null)

function formatCurrency(v: number) { return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(v) }

async function loadAll() {
  const [m, t, at, ab, tb, p] = await Promise.all([
    reportsApi.expenseMonthly(
      parseInt(reportMonth.value.slice(0, 4)), parseInt(reportMonth.value.slice(5, 7))
    ),
    reportsApi.expenseTrend(6),
    reportsApi.assetTrend(trendPeriod.value),
    reportsApi.assetBreakdown(),
    reportsApi.expenseTag(
      parseInt(reportMonth.value.slice(0, 4)), parseInt(reportMonth.value.slice(5, 7))
    ),
    reportsApi.transactionPerformance(),
  ])
  monthly.value = m; trend.value = t; assetTrend.value = at
  assetBreakdown.value = ab; tagBreakdown.value = tb; perf.value = p
}

function loadMonthlyReport() { loadAll() }

onMounted(loadAll)
</script>

<style scoped>
.reports-page { display: flex; flex-direction: column; gap: 16px; }
</style>
