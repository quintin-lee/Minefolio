<template>
  <div class="dashboard" v-loading="loading">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>仪表盘</h2>
      </div>
      <div class="header-currency-selector">
        <span class="curr-label">基准折算:</span>
        <el-radio-group v-model="selectedCurrency" size="small" @change="onCurrencyChange">
          <el-radio-button label="CNY">CNY ¥</el-radio-button>
          <el-radio-button label="USD">USD $</el-radio-button>
          <el-radio-button label="EUR">EUR €</el-radio-button>
          <el-radio-button label="HKD">HKD HK$</el-radio-button>
        </el-radio-group>
      </div>
    </div>

    <!-- 待办定投灵动胶囊 -->
    <div v-if="pendingDcaTasks.length > 0" class="floating-alert-capsule">
      <div class="capsule-left">
        <span class="pulse-dot-amber"></span>
        <span>您有 <strong>{{ pendingDcaTasks.length }}</strong> 项定投计划待执行</span>
      </div>
      <el-button type="primary" link size="small" class="capsule-btn" @click="$router.push('/plans')">
        立即处理 <el-icon><ArrowRight /></el-icon>
      </el-button>
    </div>

    <!-- 顶部 Bento Grid 资产概览 -->
    <el-row :gutter="16" class="bento-top-row">
      <!-- 左侧 Hero 主卡：净资产核心 (约 58% 宽) -->
      <el-col :xs="24" :sm="24" :md="14" class="bento-col mf-stagger-1">
        <div class="bento-hero-card hero-networth-card">
          <div class="hero-top">
            <div class="hero-label-wrap">
              <span class="hero-badge">NET WORTH</span>
              <span class="hero-label">核心净资产</span>
            </div>
            <div v-if="netWorthChange" :class="['trend-capsule', netWorthChange.isPositive ? 'trend-up' : 'trend-down']">
              <Icon :icon="netWorthChange.isPositive ? 'ph:trend-up' : 'ph:trend-down'" class="capsule-icon" />
              <span>{{ netWorthChange.isPositive ? '+' : '' }}{{ netWorthChange.pct }}%</span>
            </div>
          </div>

          <div class="hero-amount-row">
            <div class="hero-val-wrap">
              <span class="currency-symbol">{{ splitCurrency(summary.net_worth).symbol }}</span>
              <span class="hero-main-val tabular-nums">{{ splitCurrency(summary.net_worth).value }}</span>
            </div>
            <!-- 平滑发光 Sparkline 迷你走势 -->
            <div v-if="sparklinePoints && sparklineData.length > 1" class="hero-sparkline-wrap">
              <svg class="hero-sparkline" viewBox="0 0 100 28" preserveAspectRatio="none">
                <defs>
                  <linearGradient id="heroSparklineGrad" x1="0" y1="0" x2="1" y2="0">
                    <stop offset="0%" stop-color="#3b82f6" />
                    <stop offset="100%" stop-color="#6366f1" />
                  </linearGradient>
                </defs>
                <polyline :points="sparklinePoints" fill="none" stroke="url(#heroSparklineGrad)" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" />
              </svg>
              <div class="sparkline-pulse-dot"></div>
            </div>
          </div>

          <!-- 一体化多币种微型分段条 -->
          <div v-if="multiCurrency && multiCurrency.currencies && multiCurrency.currencies.length > 0" class="mini-fx-section">
            <div class="mini-fx-bar">
              <div
                v-for="(c, idx) in multiCurrency.currencies"
                :key="c.currency"
                class="mini-fx-segment"
                :style="{ width: `${c.percentage}%`, backgroundColor: fxColors[idx % fxColors.length] }"
                :title="`${c.currency}: ${c.percentage.toFixed(1)}%`"
              ></div>
            </div>
            <div class="mini-fx-chips">
              <div
                v-for="(c, idx) in multiCurrency.currencies"
                :key="c.currency"
                class="mini-fx-chip"
                :class="{ active: selectedCurrency === c.currency }"
                @click="selectCurrency(c.currency)"
              >
                <span class="fx-dot" :style="{ backgroundColor: fxColors[idx % fxColors.length] }"></span>
                <span class="fx-name">{{ c.currency }}</span>
                <span class="fx-pct tabular-nums">{{ c.percentage.toFixed(0) }}%</span>
              </div>
            </div>
          </div>
        </div>
      </el-col>

      <!-- 右侧次级 Bento 指标组 (约 42% 宽) -->
      <el-col :xs="24" :sm="24" :md="10" class="bento-col mf-stagger-2">
        <div class="bento-secondary-group">
          <!-- 总资产 -->
          <div class="bento-sub-card assets-card">
            <div class="sub-header">
              <span class="sub-title">总资产规模</span>
              <div class="sub-icon-wrap assets">
                <Icon icon="ph:wallet" />
              </div>
            </div>
            <div class="sub-val tabular-nums text-primary">{{ formatCurrency(summary.total_assets) }}</div>
          </div>

          <!-- 总负债 -->
          <div class="bento-sub-card liabilities-card">
            <div class="sub-header">
              <div class="sub-title-row">
                <span class="sub-title">负债总额</span>
                <span class="debt-ratio-badge tabular-nums">负债率 {{ debtRatio }}</span>
              </div>
              <div class="sub-icon-wrap liabilities">
                <Icon icon="ph:credit-card" />
              </div>
            </div>
            <div class="sub-val tabular-nums text-danger">{{ formatCurrency(summary.total_liabilities) }}</div>
          </div>

          <!-- 本月结余 -->
          <div class="bento-sub-card monthly-card">
            <div class="sub-header">
              <span class="sub-title">本月净现金流</span>
              <div class="sub-icon-wrap monthly">
                <Icon icon="ph:scales" />
              </div>
            </div>
            <div
              class="sub-val tabular-nums"
              :class="(currentMonthBalance?.balance ?? 0) >= 0 ? 'text-success' : 'text-danger'"
            >
              {{ formatCurrency(currentMonthBalance?.balance ?? 0) }}
            </div>
          </div>
        </div>
      </el-col>
    </el-row>

    <!-- 中部图表：净资产走势 (15列) + 资产配置分布 (9列) -->
    <el-row :gutter="16" class="charts-row">
      <el-col :xs="24" :md="15">
        <el-card shadow="hover" class="chart-card">
          <template #header>
            <div class="card-header">
              <span class="header-title">净资产走势</span>
            </div>
          </template>
          <div class="nw-chart-wrap">
            <NetWorthChart :data="summary.trend" />
          </div>
        </el-card>
      </el-col>
      <el-col :xs="24" :md="9">
        <el-card shadow="hover" class="chart-card">
          <template #header>
            <div class="card-header">
              <span class="header-title">资产配置分布</span>
            </div>
          </template>
          <AssetBreakdownPie :data="assetBreakdownData" />
        </el-card>
      </el-col>
    </el-row>

    <!-- 下部图表：年度收支 (12列) + 最近收支流水 (12列) -->
    <el-row :gutter="16" class="charts-row">
      <el-col :xs="24" :md="12">
        <el-card shadow="hover" class="chart-card">
          <template #header>
            <div class="card-header">
              <span class="header-title">年度收支概况</span>
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

      <el-col :xs="24" :md="12">
        <el-card shadow="hover" class="chart-card">
          <template #header>
            <div class="card-header">
              <span class="header-title">最近收支流水</span>
              <el-button link type="primary" size="small" @click="$router.push('/daily-expenses')">
                查看全部
              </el-button>
            </div>
          </template>
          <el-table :data="recentExpenses" size="small" height="285" class="stream-table">
            <el-table-column prop="expense_date" label="日期" width="100" />
            <el-table-column prop="category_name" label="分类" width="110">
              <template #default="{ row }">
                <span class="stream-category-pill">{{ row.category_name || '未分类' }}</span>
              </template>
            </el-table-column>
            <el-table-column prop="note" label="备注" min-width="120" show-overflow-tooltip>
              <template #default="{ row }">
                <span class="stream-note">{{ row.note || '—' }}</span>
              </template>
            </el-table-column>
            <el-table-column prop="amount" label="金额" width="130" align="right">
              <template #default="{ row }">
                <span :class="['mono-amount', row.expense_type === 'income' ? 'income-text' : 'expense-text']">
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
import { ArrowRight } from '@element-plus/icons-vue'
import { Icon } from '@iconify/vue'
import { summaryApi } from '@/api/summary'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { reportsApi } from '@/api/reports'
import { dcaApi } from '@/api/dca'
import { marketApi } from '@/api/market'
import type { Summary, DailyExpense, MultiCurrencySummary } from '@/types'
import NetWorthChart from '@/components/NetWorthChart.vue'
import AssetBreakdownPie from '@/components/AssetBreakdownPie.vue'
import YearlyChart from '@/components/YearlyChart.vue'

const loading = ref(true)
const selectedCurrency = ref('CNY')
const multiCurrency = ref<MultiCurrencySummary | null>(null)
const fxColors = ['#3b82f6', '#8b5cf6', '#10b981', '#f59e0b', '#ec4899', '#06b6d4']

const summary = ref<Summary>({
  total_assets: 0, total_liabilities: 0, net_worth: 0,
  category_breakdown: [], trend: [],
})

const assetBreakdownData = computed(() => {
  const rows = summary.value.category_breakdown ?? []
  const total = rows.reduce((sum, r) => sum + (Number(r.value) || 0), 0)
  return rows.map((r) => ({
    category_name: r.category || '未分类',
    value: Number(r.value) || 0,
    pct: total > 0 ? ((Number(r.value) || 0) / total) * 100 : 0,
  }))
})
const yearlyExpenses = ref<any>(null)
const currentMonthBalance = ref<any>(null)
const recentExpenses = ref<DailyExpense[]>([])
const pendingDcaTasks = ref<any[]>([])
const currentYear = ref(new Date())

const debtRatio = computed(() => {
  const assets = Number(summary.value.total_assets) || 0
  const liabilities = Number(summary.value.total_liabilities) || 0
  if (assets <= 0) return '0.0%'
  return `${((liabilities / assets) * 100).toFixed(1)}%`
})

const netWorthChange = computed(() => {
  const trend = summary.value.trend || []
  if (trend.length < 2) return null
  const current = Number(summary.value.net_worth) || 0
  const prevItem = trend[trend.length - 2] ?? trend[0]
  if (!prevItem) return null
  const prev = Number(prevItem.net_worth) || 0
  const diff = current - prev
  const pct = prev !== 0 ? (diff / Math.abs(prev)) * 100 : 0
  return {
    diff,
    pct: pct.toFixed(2),
    isPositive: diff >= 0
  }
})

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
  const w = 100, h = 26
  return data.map((v, i) => `${(i / (data.length - 1)) * w},${h - ((v - min) / range) * (h - 6) - 3}`).join(' ')
})

function formatCurrency(val: number) {
  return formatCurrencyValue(val, selectedCurrency.value)
}

function splitCurrency(val: number) {
  const curUpper = (selectedCurrency.value || 'CNY').toUpperCase()
  const map: Record<string, { symbol: string; digits: number }> = {
    CNY: { symbol: '¥', digits: 2 },
    USD: { symbol: '$', digits: 2 },
    EUR: { symbol: '€', digits: 2 },
    HKD: { symbol: 'HK$', digits: 2 },
    JPY: { symbol: '¥', digits: 0 },
    GBP: { symbol: '£', digits: 2 },
    USDT: { symbol: '₮', digits: 2 },
  }
  const meta = map[curUpper] || { symbol: `${curUpper} `, digits: 2 }
  const numStr = Number(val || 0).toLocaleString('zh-CN', {
    minimumFractionDigits: meta.digits,
    maximumFractionDigits: meta.digits,
  })
  return { symbol: meta.symbol, value: numStr }
}

function formatCurrencyValue(val: number, cur = 'CNY') {
  const curUpper = (cur || 'CNY').toUpperCase()
  const map: Record<string, { symbol: string; digits: number }> = {
    CNY: { symbol: '¥', digits: 2 },
    USD: { symbol: '$', digits: 2 },
    EUR: { symbol: '€', digits: 2 },
    HKD: { symbol: 'HK$', digits: 2 },
    JPY: { symbol: '¥', digits: 0 },
    GBP: { symbol: '£', digits: 2 },
    USDT: { symbol: '₮', digits: 2 },
  }
  const meta = map[curUpper] || { symbol: `${curUpper} `, digits: 2 }
  return `${meta.symbol}${Number(val || 0).toLocaleString('zh-CN', {
    minimumFractionDigits: meta.digits,
    maximumFractionDigits: meta.digits,
  })}`
}

function selectCurrency(curr: string) {
  if (selectedCurrency.value !== curr) {
    selectedCurrency.value = curr
    onCurrencyChange()
  }
}

async function onCurrencyChange() {
  await loadMultiCurrency()
}

async function loadMultiCurrency() {
  try {
    const res = await marketApi.getMultiCurrencySummary(selectedCurrency.value)
    multiCurrency.value = res
    if (res && res.total_net_worth !== undefined) {
      summary.value.net_worth = res.total_net_worth
      summary.value.total_assets = res.total_assets
      summary.value.total_liabilities = res.total_liabilities
    }
  } catch {}
}

async function loadDashboard() {
  loading.value = true
  try {
    const res = await summaryApi.get()
    summary.value = res
    await Promise.allSettled([loadYearly(), loadCurrentMonth(), loadRecent(), loadPendingDca(), loadMultiCurrency()])
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

/* 顶部 Bento 布局 */
.bento-top-row {
  margin-bottom: 4px;
}

.bento-col {
  margin-bottom: 12px;
}

/* Hero 净资产大卡 */
.hero-networth-card {
  padding: 24px;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  min-height: 232px;
}

.hero-top {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.hero-label-wrap {
  display: flex;
  align-items: center;
  gap: 8px;
}

.hero-badge {
  font-size: 10px;
  font-weight: 700;
  letter-spacing: 0.08em;
  padding: 2px 8px;
  border-radius: var(--mf-radius-pill);
  background: rgba(59, 130, 246, 0.2);
  color: var(--mf-primary);
  border: 1px solid rgba(59, 130, 246, 0.3);
}

.hero-label {
  font-size: 14px;
  font-weight: 600;
  color: var(--mf-text-regular);
}

.trend-capsule {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 12px;
  font-weight: 700;
  font-family: var(--mf-font-mono);
  padding: 3px 10px;
  border-radius: var(--mf-radius-pill);
}

.trend-up {
  background: var(--mf-success-light);
  color: var(--mf-success);
  border: 1px solid var(--mf-success-border);
}

.trend-down {
  background: var(--mf-danger-light);
  color: var(--mf-danger);
  border: 1px solid var(--mf-danger-border);
}

.hero-amount-row {
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  margin: 16px 0;
}

.hero-val-wrap {
  display: flex;
  align-items: baseline;
  gap: 4px;
}

.currency-symbol {
  font-size: 22px;
  font-weight: 600;
  color: var(--mf-primary);
  opacity: 0.75;
}

.hero-main-val {
  font-size: 34px;
  font-weight: 800;
  letter-spacing: -0.8px;
  line-height: 1.1;
  color: var(--mf-text-main);
}

.hero-sparkline-wrap {
  position: relative;
  width: 120px;
  height: 32px;
  margin-bottom: 4px;
}

.hero-sparkline {
  width: 100%;
  height: 100%;
}

.sparkline-pulse-dot {
  position: absolute;
  right: 0;
  top: 50%;
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #6366f1;
  box-shadow: 0 0 8px #6366f1;
  animation: pulse-glow 2s infinite ease-in-out;
}

/* 一体化微型多币种配比条 */
.mini-fx-section {
  display: flex;
  flex-direction: column;
  gap: 8px;
  margin-top: auto;
  padding-top: 14px;
  border-top: 1px solid var(--mf-border-subtle);
}

.mini-fx-bar {
  display: flex;
  height: 4px;
  border-radius: 2px;
  overflow: hidden;
  background: rgba(255, 255, 255, 0.05);
}

.mini-fx-segment {
  height: 100%;
  transition: width 0.3s ease;
}

.mini-fx-chips {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
}

.mini-fx-chip {
  display: flex;
  align-items: center;
  gap: 6px;
  cursor: pointer;
  padding: 2px 6px;
  border-radius: 4px;
  transition: var(--mf-transition);
}

.mini-fx-chip:hover {
  background: rgba(255, 255, 255, 0.05);
}

.mini-fx-chip.active {
  background: rgba(59, 130, 246, 0.15);
}

.fx-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
}

.fx-name {
  font-size: 11px;
  font-weight: 600;
  color: var(--mf-text-regular);
}

.fx-pct {
  font-size: 11px;
  color: var(--mf-text-muted);
}

/* 右侧次级 Bento 指标组 */
.bento-secondary-group {
  display: flex;
  flex-direction: column;
  gap: 10px;
  height: 100%;
}

.bento-sub-card {
  flex: 1;
  padding: 14px 18px;
  border-radius: var(--mf-radius-lg);
  border: 1px solid var(--mf-border-subtle);
  background: var(--mf-surface-card);
  backdrop-filter: blur(16px);
  -webkit-backdrop-filter: blur(16px);
  transition: var(--mf-transition);
  display: flex;
  flex-direction: column;
  justify-content: space-between;
}

.bento-sub-card:hover {
  transform: translateY(-2px);
  border-color: var(--mf-border-hover);
  box-shadow: var(--mf-shadow-glow);
}

.sub-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 6px;
}

.sub-title-row {
  display: flex;
  align-items: center;
  gap: 8px;
}

.sub-title {
  font-size: 12px;
  color: var(--mf-text-muted);
  font-weight: 500;
}

.debt-ratio-badge {
  font-size: 10px;
  padding: 1px 6px;
  border-radius: 4px;
  background: rgba(244, 63, 94, 0.12);
  color: var(--mf-danger);
  border: 1px solid var(--mf-danger-border);
}

.sub-icon-wrap {
  width: 26px;
  height: 26px;
  border-radius: var(--mf-radius-sm);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 14px;
}

.sub-icon-wrap.assets {
  background: var(--mf-primary-light);
  color: var(--mf-primary);
}

.sub-icon-wrap.liabilities {
  background: var(--mf-danger-light);
  color: var(--mf-danger);
}

.sub-icon-wrap.monthly {
  background: var(--mf-warning-light);
  color: var(--mf-warning);
}

.sub-val {
  font-size: 20px;
  font-weight: 700;
  letter-spacing: -0.3px;
  line-height: 1.2;
}

/* 灵动微胶囊待办 */
.floating-alert-capsule {
  display: flex;
  justify-content: space-between;
  align-items: center;
  background: linear-gradient(135deg, rgba(245, 158, 11, 0.12), rgba(245, 158, 11, 0.05));
  border: 1px solid var(--mf-warning-border);
  border-radius: var(--mf-radius-pill);
  padding: 6px 16px;
  margin-bottom: 4px;
}

.capsule-left {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 13px;
  color: var(--mf-text-regular);
}

.pulse-dot-amber {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--mf-warning);
  box-shadow: 0 0 8px var(--mf-warning);
  animation: pulse-glow 2s infinite ease-in-out;
}

.capsule-btn {
  font-weight: 600;
}

/* 图表与通用卡片 */
.chart-card {
  border-radius: var(--mf-radius-lg);
  border: 1px solid var(--mf-border-subtle);
  background: var(--mf-surface-card);
  backdrop-filter: blur(16px);
  -webkit-backdrop-filter: blur(16px);
  box-shadow: var(--mf-shadow-sm);
  transition: var(--mf-transition);
}

.chart-card:hover {
  border-color: var(--mf-border-hover);
  box-shadow: var(--mf-shadow-glow);
}

:deep(.el-card__header) {
  padding: 12px 18px;
  border-bottom: 1px solid var(--mf-border-subtle);
  background: var(--mf-surface);
}

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.header-title {
  font-size: 14px;
  font-weight: 600;
  color: var(--mf-text-main);
}

.header-date-picker {
  width: 120px;
}

.nw-chart-wrap {
  height: 310px;
}

.income-text {
  color: #34d399;
  text-shadow: 0 0 8px rgba(52, 211, 153, 0.35);
  font-weight: 600;
}

.expense-text {
  color: #f87171;
  text-shadow: 0 0 8px rgba(248, 113, 113, 0.3);
  font-weight: 600;
}

.stream-category-pill {
  font-size: 11px;
  font-weight: 500;
  padding: 2px 8px;
  border-radius: 4px;
  background: rgba(59, 130, 246, 0.12);
  color: var(--mf-primary);
  border: 1px solid rgba(59, 130, 246, 0.2);
}

.stream-note {
  font-size: 12px;
  color: var(--mf-text-muted);
}
</style>
