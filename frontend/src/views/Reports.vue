<template>
  <div class="reports-page" v-loading="loading">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>报表</h2>
      </div>
    </div>

    <el-tabs class="premium-tabs" type="card">
      <el-tab-pane label="收支分析">
        <el-empty v-if="!monthly" description="暂无收支数据，完成第一笔交易后这里会出现图表" :image-size="120" />
        <template v-else>
          <div class="tab-layout">
            <el-row :gutter="16" class="tab-row">
              <!-- 月度收支报表 -->
              <el-col :span="12">
                <div class="report-card">
                  <div class="card-header">
                    <h3>月度收支</h3>
                    <el-date-picker v-model="reportMonth" type="month" value-format="YYYY-MM" size="small" class="date-picker-subtle" @change="loadMonthlyReport" :clearable="false" />
                  </div>
                
                  <div class="metric-cards">
                    <div class="metric-card">
                      <div class="metric-label">总收入</div>
                      <div class="metric-value income-text">{{ formatCurrency(monthly?.total_income ?? 0) }}</div>
                    </div>
                    <div class="metric-card">
                      <div class="metric-label">总支出</div>
                      <div class="metric-value expense-text">{{ formatCurrency(monthly?.total_expense ?? 0) }}</div>
                    </div>
                    <div class="metric-card highlight">
                      <div class="metric-label">结余</div>
                      <div class="metric-value" :class="(monthly?.balance ?? 0) >= 0 ? 'income-text' : 'expense-text'">
                        {{ formatCurrency(monthly?.balance ?? 0) }}
                      </div>
                    </div>
                  </div>
                
                  <div class="chart-container">
                    <ExpenseCategoryPie :data="monthly?.by_category ?? []" />
                  </div>
                </div>
              </el-col>

              <!-- 收支趋势 -->
              <el-col :span="12">
                <div class="report-card">
                  <div class="card-header">
                    <h3>收支趋势（近6月）</h3>
                  </div>
                  <div class="chart-container">
                    <ExpenseTrendBar :data="trend" />
                  </div>
                </div>
              </el-col>
            </el-row>

            <el-row :gutter="16" class="tab-row">
              <el-col :span="12">
                <div class="report-card">
                  <div class="card-header">
                    <h3>标签支出分析</h3>
                  </div>
                  <div class="table-container">
                    <el-table
                      :data="tagBreakdown?.items ?? []"
                      height="100%"
                      class="premium-table-small"
                      :header-cell-style="{ background: 'rgba(0, 212, 255, 0.06)' }"
                    >
                      <el-table-column prop="tag_name" label="标签" min-width="120">
                        <template #default="{ row }">
                          <el-tag size="small" effect="plain" class="tag-plain">{{ row.tag_name }}</el-tag>
                        </template>
                      </el-table-column>
                      <el-table-column prop="amount" label="金额" width="120" align="right">
                        <template #default="{ row }">
                          <span class="mono-text">{{ formatCurrency(row.amount) }}</span>
                        </template>
                      </el-table-column>
                      <el-table-column prop="pct" label="占比" width="100" align="right">
                        <template #default="{ row }">
                          <div class="pct-cell">
                            <span>{{ (row.pct ?? 0).toFixed(1) }}%</span>
                            <el-progress :percentage="row.pct ?? 0" :show-text="false" :stroke-width="4" />
                          </div>
                        </template>
                      </el-table-column>
                      <el-table-column prop="count" label="次数" width="80" align="center" />
                      <template #empty>
                        <el-empty description="暂无标签支出数据" :image-size="60" />
                      </template>
                    </el-table>
                  </div>
                </div>
              </el-col>
              <el-col :span="12">
                <div class="report-card">
                  <div class="card-header">
                    <h3>交易表现</h3>
                  </div>
                  <div class="perf-grid">
                    <div class="perf-item">
                      <div class="perf-label">总交易笔数</div>
                      <div class="perf-value">{{ perf?.total_trades ?? 0 }}</div>
                    </div>
                    <div class="perf-item">
                      <div class="perf-label">总收益</div>
                      <div class="perf-value income-text">{{ formatCurrency(perf?.total_gain ?? 0) }}</div>
                    </div>
                    <div class="perf-item">
                      <div class="perf-label">总亏损</div>
                      <div class="perf-value expense-text">{{ formatCurrency(perf?.total_loss ?? 0) }}</div>
                    </div>
                    <div class="perf-item highlight">
                      <div class="perf-label">净收益</div>
                      <div class="perf-value" :class="(perf?.net_gain ?? 0) >= 0 ? 'income-text' : 'expense-text'">
                        {{ formatCurrency(perf?.net_gain ?? 0) }}
                      </div>
                    </div>
                    <div class="perf-item">
                      <div class="perf-label">已实现盈亏</div>
                      <div class="perf-value" :class="((perf?.realized_pnl ?? 0) >= 0 ? 'income-text' : 'expense-text')">
                        {{ formatCurrency(perf?.realized_pnl ?? 0) }}
                      </div>
                    </div>
                    <div class="perf-item">
                      <div class="perf-label">浮动盈亏</div>
                      <div class="perf-value" :class="((perf?.floating_pnl ?? 0) >= 0 ? 'income-text' : 'expense-text')">
                        {{ formatCurrency(perf?.floating_pnl ?? 0) }}
                      </div>
                    </div>
                    <div class="perf-item">
                      <div class="perf-label">持仓市值</div>
                      <div class="perf-value">{{ formatCurrency(perf?.total_market_value ?? 0) }}</div>
                    </div>
                    <div class="perf-item">
                      <div class="perf-label">持仓成本</div>
                      <div class="perf-value">{{ formatCurrency(perf?.total_cost_basis_remaining ?? 0) }}</div>
                    </div>
                  </div>
                </div>
              </el-col>
            </el-row>
          </div>
        </template>
      </el-tab-pane>

      <el-tab-pane label="资产分析">
        <el-empty v-if="!assetBreakdown" description="暂无资产数据，先添加资产账户" :image-size="120" />
        <template v-else>
          <div class="tab-layout">
            <el-row :gutter="16" class="tab-row">
              <!-- 资产趋势 -->
              <el-col :span="16">
                <div class="report-card">
                  <div class="card-header">
                    <h3>净资产趋势</h3>
                    <div class="radio-group-subtle">
                      <el-radio-group v-model="trendPeriod" size="small">
                        <el-radio-button value="30d">近30天</el-radio-button>
                        <el-radio-button value="90d">近90天</el-radio-button>
                        <el-radio-button value="365d">近1年</el-radio-button>
                      </el-radio-group>
                    </div>
                  </div>
                  <div class="chart-container">
                    <AssetTrendLine :data="assetTrend" />
                  </div>
                </div>
              </el-col>

              <!-- 资产分布 -->
              <el-col :span="8">
                <div class="report-card">
                  <div class="card-header">
                    <h3>资产分布</h3>
                  </div>
                
                  <div class="asset-summary-list">
                    <div class="summary-item">
                      <span class="label">总资产</span>
                      <span class="value">{{ formatCurrency(assetBreakdown?.total_assets ?? 0) }}</span>
                    </div>
                    <div class="summary-item">
                      <span class="label">总负债</span>
                      <span class="value expense-text">{{ formatCurrency(assetBreakdown?.total_liabilities ?? 0) }}</span>
                    </div>
                    <div class="summary-item total">
                      <span class="label">净资产</span>
                      <span class="value income-text">{{ formatCurrency(assetBreakdown?.net_worth ?? 0) }}</span>
                    </div>
                  </div>
                
                  <el-divider border-style="dashed" style="margin: 12px 0" />
                
                  <div class="chart-container">
                    <AssetBreakdownPie :data="assetBreakdown?.assets ?? []" />
                  </div>
                </div>
              </el-col>
            </el-row>
          </div>
        </template>
      </el-tab-pane>
    </el-tabs>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import { reportsApi } from '@/api/reports'
import ExpenseCategoryPie from '@/components/ExpenseCategoryPie.vue'
import ExpenseTrendBar from '@/components/ExpenseTrendBar.vue'
import AssetTrendLine from '@/components/AssetTrendLine.vue'
import AssetBreakdownPie from '@/components/AssetBreakdownPie.vue'
import { formatCurrency } from '@/utils/format'
import SummaryCard from '@/components/SummaryCard.vue'

const loading = ref(true)
const reportMonth = ref(new Date().toISOString().slice(0, 7))
const trendPeriod = ref('30d')
const monthly = ref<any>(null)
const trend = ref<any>(null)
const assetTrend = ref<any>(null)
const assetBreakdown = ref<any>(null)
const tagBreakdown = ref<any>(null)
const perf = ref<any>(null)

async function loadAll() {
  loading.value = true
  try {
    const [m, t, at, ab, tb, p] = await Promise.allSettled([
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
    if (m.status === 'fulfilled') monthly.value = m.value
    if (t.status === 'fulfilled') trend.value = t.value
    if (at.status === 'fulfilled') assetTrend.value = at.value
    if (ab.status === 'fulfilled') assetBreakdown.value = ab.value
    if (tb.status === 'fulfilled') tagBreakdown.value = tb.value
    if (p.status === 'fulfilled') perf.value = p.value

    const failures = [m, t, at, ab, tb, p].filter(r => r.status === 'rejected')
    if (failures.length > 0) {
      ElMessage.warning(`部分报表数据加载失败（${failures.length}/${failures.length + 6 - failures.length}）`)
    }
  } catch (e: any) {
    ElMessage.error(e?.response?.data?.message || '报表数据加载失败')
  } finally {
    loading.value = false
  }
}

function loadMonthlyReport() { loadAll() }

onMounted(loadAll)
</script>

<style scoped>
.reports-page {

  background-color: var(--mf-background);
  height: 100%;
  overflow: hidden;
  display: flex;
  flex-direction: column;
  box-sizing: border-box;
}

.premium-tabs {
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

:deep(.premium-tabs.el-tabs--card > .el-tabs__header) {
  border-bottom: none;
  margin-bottom: 16px;
  flex-shrink: 0;
}

:deep(.premium-tabs.el-tabs--card > .el-tabs__header .el-tabs__nav) {
  border: none;
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  padding: 4px;
  box-shadow: var(--mf-shadow-sm);
}

:deep(.premium-tabs.el-tabs--card > .el-tabs__header .el-tabs__item) {
  border: none !important;
  border-radius: var(--mf-radius-md);
  margin: 0 4px;
  font-weight: 500;
  color: var(--mf-text-muted);
  transition: all 0.3s;
}

:deep(.premium-tabs.el-tabs--card > .el-tabs__header .el-tabs__item.is-active) {
  background: var(--mf-surface-muted);
  color: var(--mf-text-main);
}

:deep(.premium-tabs > .el-tabs__content) {
  flex: 1;
  min-height: 0;
  overflow: hidden;
}

:deep(.premium-tabs .el-tab-pane) {
  height: 100%;
  min-height: 0;
  overflow: hidden;
}

.tab-layout {
  height: 100%;
  display: flex;
  flex-direction: column;
  gap: 16px;
  min-height: 0;
  overflow: hidden;
}

.tab-row {
  flex: 1;
  min-height: 0;
  display: flex;
}

.tab-row > .el-col {
  display: flex;
  flex-direction: column;
  min-height: 0;
  height: 100%;
}

.report-card {
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  padding: 16px 20px;
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
  display: flex;
  flex-direction: column;
  flex: 1;
  min-height: 0;
  overflow: hidden;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
  flex-shrink: 0;
}

.card-header h3 {
  margin: 0;
  font-size: 15px;
  font-weight: 600;
  color: var(--mf-text-main);
}

.metric-cards {
  display: flex;
  gap: 12px;
  margin-bottom: 12px;
  flex-shrink: 0;
}

.metric-card {
  flex: 1;
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border);
  border-radius: var(--mf-radius-md);
  padding: 6px 12px;
}

.metric-label {
  font-size: 12px;
  color: var(--mf-text-muted);
  margin-bottom: 2px;
}

.metric-value {
  font-size: 16px;
  font-weight: 700;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
}

.income-text { color: #34d399; text-shadow: 0 0 8px rgba(52,211,153,0.4); }
.expense-text { color: #f87171; text-shadow: 0 0 8px rgba(248,113,113,0.3); }

.chart-container {
  flex: 1;
  min-height: 0;
  width: 100%;
  position: relative;
  overflow: hidden;
}

.date-picker-subtle :deep(.el-input__wrapper) {
  box-shadow: none;
  background: var(--mf-surface-muted);
  border-radius: var(--mf-radius-md);
}

.radio-group-subtle :deep(.el-radio-button__inner) {
  border: none;
  background: transparent;
  box-shadow: none !important;
}

.radio-group-subtle :deep(.el-radio-button.is-active .el-radio-button__inner) {
  background-color: var(--mf-surface-muted);
  color: var(--mf-text-main);
  border-radius: var(--mf-radius-md);
}

.asset-summary-list {
  display: flex;
  flex-direction: column;
  gap: 12px;
  margin-bottom: 12px;
  flex-shrink: 0;
}

.summary-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.summary-item .label {
  color: var(--mf-text-muted);
  font-size: 13px;
}

.summary-item .value {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-weight: 600;
  font-size: 15px;
}

.summary-item.total .label {
  font-weight: 600;
  color: var(--mf-text-main);
}

.summary-item.total .value {
  font-size: 16px;
  font-weight: 700;
}

.perf-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px 12px;
  flex: 1;
  min-height: 0;
  align-content: stretch;
}

.perf-item {
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border);
  border-radius: var(--mf-radius-md);
  padding: 6px 12px;
  display: flex;
  flex-direction: column;
  justify-content: center;
}

.perf-label {
  font-size: 11px;
  color: var(--mf-text-muted);
  margin-bottom: 2px;
}

.perf-value {
  font-size: 15px;
  font-weight: 700;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
}

.premium-table-small {
  --el-table-border-color: transparent;
  --el-table-header-bg-color: rgba(0, 212, 255, 0.06);
}

.table-container {
  flex: 1;
  min-height: 0;
  width: 100%;
  overflow: hidden;
  display: flex;
  flex-direction: column;
}

.table-container :deep(.el-table) {
  flex: 1;
  min-height: 0;
  height: 100%;
}

.table-container :deep(.el-table__body-wrapper) {
  overflow-y: auto;
}

.premium-table-small :deep(.el-table th.el-table__cell) {
  background-color: rgba(0, 212, 255, 0.06) !important;
  color: #94a3b8 !important;
  border-bottom: 1px solid var(--mf-border) !important;
}

.premium-table-small :deep(.el-table td.el-table__cell) {
  border-bottom: 1px solid var(--mf-border);
  color: var(--mf-text-main);
}

.premium-table-small :deep(.el-table__body tr:hover > td) {
  background-color: rgba(0, 212, 255, 0.04) !important;
}

.mono-text {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-weight: 500;
}

.pct-cell {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.pct-cell span {
  font-size: 12px;
  color: var(--mf-text-muted);
}

.tag-plain {
  border: none;
  background: var(--mf-surface-muted);
}
</style>
